#include <sobi/net.h>
#include <sobi/string.h>
#include <sobi/timer.h>
#include <sobi/log.h>
#include <sobi/panic.h>

#define SOCK_MAX 12
#define TCP_MSS 200
#define TCP_SEGS 4
#define UDP_Q 4
#define UDP_MAX 256
#define ARP_MAX 16
#define ARP_TTL 30000
#define LB_MAX 16
#define TX_DEPTH 6
#define FRAME_MAX 1600
#define RTO_MS 200

#define PROTO_ICMP 1
#define PROTO_TCP 6
#define PROTO_UDP 17

#define TH_FIN 0x01
#define TH_SYN 0x02
#define TH_RST 0x04
#define TH_PSH 0x08
#define TH_ACK 0x10

#define SOCK_UDP 1
#define SOCK_TCP 2

struct tseg {
    uint32_t seq;
    uint16_t len;
    uint8_t flags;
    uint8_t live;
    uint64_t tstamp;
    uint8_t data[TCP_MSS];
};

struct sock {
    uint8_t used;
    uint8_t proto;
    uint8_t state;
    uint8_t fin_rx;
    int parent;
    uint32_t lip;
    uint32_t rip;
    uint16_t lport;
    uint16_t rport;
    uint16_t peer_win;
    uint32_t snd_una;
    uint32_t snd_nxt;
    uint32_t snd_iss;
    uint32_t rcv_nxt;
    struct tseg tx[TCP_SEGS];
    uint8_t rx[TCP_SEGS][TCP_MSS];
    uint16_t rxlen[TCP_SEGS];
    uint8_t rxh;
    uint8_t rxt;
    uint8_t rxn;
    uint8_t uq[UDP_Q][UDP_MAX];
    uint16_t ulen[UDP_Q];
    uint32_t usrc[UDP_Q];
    uint16_t usport[UDP_Q];
    uint8_t uh;
    uint8_t ut;
    uint8_t un;
};

struct arp_ent {
    uint32_t ip;
    uint8_t mac[6];
    uint64_t expire;
    uint8_t valid;
};

net_tx_fn net_tx_capture;

static struct sock socks[SOCK_MAX];
static struct arp_ent arp_tab[ARP_MAX];
static uint8_t host_mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x15 };
static uint32_t host_ip = 0x0A00020Fu;
static uint32_t host_mask = 0xFFFFFF00u;
static uint32_t host_gw = 0x0A000202u;
static int net_up;
static uint16_t ip_ident = 1;
static uint16_t eph = 49152;
static uint32_t tcp_isn = 1000;
static uint64_t drops_short;
static uint64_t drops_csum;
static uint8_t txbuf[TX_DEPTH][FRAME_MAX];
static int tx_depth;
static uint8_t lb_frame[LB_MAX][FRAME_MAX];
static uint16_t lb_len[LB_MAX];
static int lb_h;
static int lb_t;
static int lb_n;
static int lb_run;

static void be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t csum_add(uint32_t sum, const uint8_t *p, size_t len)
{
    while (len > 1) {
        sum += ((uint32_t)p[0] << 8) | p[1];
        p += 2;
        len -= 2;
    }
    if (len)
        sum += (uint32_t)p[0] << 8;
    return sum;
}

static uint16_t csum_fold(uint32_t sum)
{
    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

static int csum_good(uint32_t sum)
{
    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    return sum == 0xFFFFu;
}

static uint32_t pseudo_sum(uint32_t src, uint32_t dst, uint8_t proto, uint16_t len)
{
    uint32_t sum = 0;
    sum += (src >> 16) & 0xFFFFu;
    sum += src & 0xFFFFu;
    sum += (dst >> 16) & 0xFFFFu;
    sum += dst & 0xFFFFu;
    sum += proto;
    sum += len;
    return sum;
}

static void drop_short(void)
{
    drops_short++;
    pr_info("net", "drop short");
}

static void drop_csum(void)
{
    drops_csum++;
    pr_info("net", "drop checksum");
}

static int ip_loop(uint32_t ip)
{
    return (ip & 0xFF000000u) == 0x7F000000u;
}

static int ip_local(uint32_t ip)
{
    return ip == host_ip || ip_loop(ip);
}

static uint32_t ip_src_for(uint32_t dst)
{
    if (ip_loop(dst))
        return 0x7F000001u;
    return host_ip;
}

static uint32_t ip_hop(uint32_t dst)
{
    if (ip_local(dst))
        return dst;
    if ((dst & host_mask) == (host_ip & host_mask))
        return dst;
    return host_gw;
}

static int mac_ok(const uint8_t *mac)
{
    static const uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    if (memcmp(mac, host_mac, 6) == 0)
        return 1;
    if (memcmp(mac, bcast, 6) == 0)
        return 1;
    return 0;
}

static int seq_lt(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) < 0;
}

static int seq_le(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) <= 0;
}

static int seq_gt(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
}

static void net_ensure(void)
{
    if (net_up)
        return;
    host_ip = 0x0A00020Fu;
    host_mask = 0xFFFFFF00u;
    host_gw = 0x0A000202u;
    host_mac[0] = 0x02;
    host_mac[1] = 0x00;
    host_mac[2] = 0x00;
    host_mac[3] = 0x00;
    host_mac[4] = 0x00;
    host_mac[5] = 0x15;
    net_up = 1;
}

void net_set_addr(uint32_t ip, uint32_t mask, uint32_t gw)
{
    net_ensure();
    host_ip = ip;
    host_mask = mask;
    host_gw = gw;
}

void net_get_mac(uint8_t mac[6])
{
    net_ensure();
    if (mac)
        memcpy(mac, host_mac, 6);
}

void net_set_mac(const uint8_t mac[6])
{
    net_ensure();
    if (mac)
        memcpy(host_mac, mac, 6);
}

uint64_t net_stat_short(void)
{
    return drops_short;
}

uint64_t net_stat_csum(void)
{
    return drops_csum;
}

static void arp_learn(uint32_t ip, const uint8_t mac[6])
{
    int slot = -1;
    uint64_t exp;
    if (ip == 0 || !mac)
        return;
    exp = uptime_ms() + ARP_TTL;
    for (int i = 0; i < ARP_MAX; i++) {
        if (arp_tab[i].valid && arp_tab[i].ip == ip) {
            memcpy(arp_tab[i].mac, mac, 6);
            arp_tab[i].expire = exp;
            return;
        }
        if (!arp_tab[i].valid && slot < 0)
            slot = i;
    }
    if (slot < 0)
        slot = 0;
    arp_tab[slot].valid = 1;
    arp_tab[slot].ip = ip;
    memcpy(arp_tab[slot].mac, mac, 6);
    arp_tab[slot].expire = exp;
}

static int arp_resolve(uint32_t ip, uint8_t mac[6])
{
    uint64_t now = uptime_ms();
    for (int i = 0; i < ARP_MAX; i++) {
        if (!arp_tab[i].valid || arp_tab[i].ip != ip)
            continue;
        if (now >= arp_tab[i].expire) {
            arp_tab[i].valid = 0;
            return -1;
        }
        memcpy(mac, arp_tab[i].mac, 6);
        return 0;
    }
    return -1;
}

static void arp_sweep(void)
{
    uint64_t now = uptime_ms();
    for (int i = 0; i < ARP_MAX; i++) {
        if (arp_tab[i].valid && now >= arp_tab[i].expire)
            arp_tab[i].valid = 0;
    }
}

static uint8_t *tx_grab(void)
{
    if (tx_depth >= TX_DEPTH)
        return NULL;
    return txbuf[tx_depth++];
}

static void tx_drop(void)
{
    if (tx_depth > 0)
        tx_depth--;
}

static void lb_put(const uint8_t *frame, size_t len);
static void lb_drain(void);
static int net_tx_frame(const uint8_t *frame, size_t len);
static void tcp_input(uint32_t src, uint32_t dst, const uint8_t *tcp, size_t len);
static int tcp_output(struct sock *s, uint8_t flags, const uint8_t *data, uint16_t dlen, int record);

static int net_tx_frame(const uint8_t *frame, size_t len)
{
    if (len >= 14 && net_tx_capture)
        net_tx_capture(frame, len);
    if (len >= 34 && rd16(frame + 12) == 0x0800) {
        uint32_t dst = rd32(frame + 30);
        if (ip_local(dst)) {
            lb_put(frame, len);
            lb_drain();
            return 0;
        }
    }
    return virtio_net_tx(frame, len);
}

static void lb_put(const uint8_t *frame, size_t len)
{
    if (lb_n >= LB_MAX || len > FRAME_MAX || len == 0)
        return;
    memcpy(lb_frame[lb_t], frame, len);
    lb_len[lb_t] = (uint16_t)len;
    lb_t = (lb_t + 1) % LB_MAX;
    lb_n++;
}

static void lb_drain(void)
{
    if (lb_run)
        return;
    lb_run = 1;
    while (lb_n > 0) {
        int i = lb_h;
        uint16_t n = lb_len[i];
        lb_h = (lb_h + 1) % LB_MAX;
        lb_n--;
        net_rx_inject(lb_frame[i], n);
    }
    lb_run = 0;
}

static int arp_send(uint16_t op, const uint8_t *dmac, uint32_t tpa, const uint8_t *tha)
{
    uint8_t *frame = tx_grab();
    uint8_t *a;
    static const uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    const uint8_t *dst = dmac ? dmac : bcast;
    int rc;
    if (!frame)
        return -ENOMEM;
    memset(frame, 0, 42);
    memcpy(frame, dst, 6);
    memcpy(frame + 6, host_mac, 6);
    be16(frame + 12, 0x0806);
    a = frame + 14;
    be16(a + 0, 1);
    be16(a + 2, 0x0800);
    a[4] = 6;
    a[5] = 4;
    be16(a + 6, op);
    memcpy(a + 8, host_mac, 6);
    be32(a + 14, host_ip);
    if (tha)
        memcpy(a + 18, tha, 6);
    be32(a + 24, tpa);
    rc = net_tx_frame(frame, 42);
    tx_drop();
    return rc;
}

static void arp_input(const uint8_t *frame, size_t len)
{
    const uint8_t *a;
    uint16_t op;
    uint32_t spa;
    uint32_t tpa;
    if (len < 42)
        return;
    a = frame + 14;
    if (rd16(a) != 1 || rd16(a + 2) != 0x0800 || a[4] != 6 || a[5] != 4)
        return;
    op = rd16(a + 6);
    spa = rd32(a + 14);
    tpa = rd32(a + 24);
    arp_learn(spa, a + 8);
    if (op == 1 && tpa == host_ip)
        arp_send(2, a + 8, spa, a + 8);
}

static int ip_send(uint32_t dst, uint8_t proto, const uint8_t *payload, size_t plen,
                   const uint8_t *dmac_in)
{
    uint8_t dmac[6];
    uint8_t *frame;
    uint8_t *ip;
    uint32_t src;
    uint16_t total;
    int rc;
    if (plen > 1480)
        return -EMSGSIZE;
    src = ip_src_for(dst);
    if (dmac_in) {
        memcpy(dmac, dmac_in, 6);
    } else if (ip_local(dst)) {
        memcpy(dmac, host_mac, 6);
    } else if (arp_resolve(ip_hop(dst), dmac) != 0) {
        arp_send(1, NULL, ip_hop(dst), NULL);
        return -EAGAIN;
    }
    frame = tx_grab();
    if (!frame)
        return -ENOMEM;
    memcpy(frame, dmac, 6);
    memcpy(frame + 6, host_mac, 6);
    be16(frame + 12, 0x0800);
    ip = frame + 14;
    memset(ip, 0, 20);
    ip[0] = 0x45;
    total = (uint16_t)(20 + plen);
    be16(ip + 2, total);
    be16(ip + 4, ip_ident++);
    ip[8] = 64;
    ip[9] = proto;
    be32(ip + 12, src);
    be32(ip + 16, dst);
    be16(ip + 10, csum_fold(csum_add(0, ip, 20)));
    if (plen && payload)
        memcpy(ip + 20, payload, plen);
    rc = net_tx_frame(frame, (size_t)14 + total);
    tx_drop();
    return rc;
}

static void icmp_input(const uint8_t *frame, uint32_t src, uint32_t dst, const uint8_t *icmp,
                       size_t len)
{
    uint8_t reply[1500];
    uint16_t sum;
    if (!ip_local(dst))
        return;
    if (len < 8) {
        drop_short();
        return;
    }
    if (!csum_good(csum_add(0, icmp, len))) {
        drop_csum();
        return;
    }
    if (icmp[0] != 8 || icmp[1] != 0)
        return;
    if (8 + (len - 8) > sizeof(reply))
        return;
    memset(reply, 0, len);
    reply[0] = 0;
    reply[1] = 0;
    memcpy(reply + 4, icmp + 4, len - 4);
    sum = csum_fold(csum_add(0, reply, len));
    be16(reply + 2, sum);
    ip_send(src, PROTO_ICMP, reply, len, frame + 6);
}

static struct sock *sock_fd(int fd, int proto)
{
    if (fd < 0 || fd >= SOCK_MAX)
        return NULL;
    if (!socks[fd].used)
        return NULL;
    if (proto && socks[fd].proto != proto)
        return NULL;
    return &socks[fd];
}

static int sock_alloc(int proto)
{
    for (int i = 0; i < SOCK_MAX; i++) {
        if (socks[i].used)
            continue;
        memset(&socks[i], 0, sizeof(socks[i]));
        socks[i].used = 1;
        socks[i].proto = (uint8_t)proto;
        socks[i].parent = -1;
        socks[i].peer_win = TCP_MSS * TCP_SEGS;
        return i;
    }
    return -1;
}

static int port_busy(uint16_t port, int proto)
{
    for (int i = 0; i < SOCK_MAX; i++) {
        if (!socks[i].used || socks[i].proto != proto)
            continue;
        if (socks[i].lport == port)
            return 1;
    }
    return 0;
}

static uint16_t alloc_port(void)
{
    for (int n = 0; n < 2000; n++) {
        uint16_t port = eph++;
        if (eph < 49152)
            eph = 49152;
        if (!port_busy(port, SOCK_UDP) && !port_busy(port, SOCK_TCP))
            return port;
    }
    return 0;
}

static int udp_queue(struct sock *s, uint32_t src, uint16_t sport, const uint8_t *data, uint16_t len)
{
    if (s->un >= UDP_Q || len > UDP_MAX)
        return -1;
    memcpy(s->uq[s->ut], data, len);
    s->ulen[s->ut] = len;
    s->usrc[s->ut] = src;
    s->usport[s->ut] = sport;
    s->ut = (uint8_t)((s->ut + 1) % UDP_Q);
    s->un++;
    return 0;
}

static void udp_input(uint32_t src, uint32_t dst, const uint8_t *udp, size_t len)
{
    uint16_t ulen;
    uint16_t sum;
    uint16_t dport;
    uint16_t sport;
    uint32_t csum;
    int si;
    if (!ip_local(dst))
        return;
    if (len < 8) {
        drop_short();
        return;
    }
    ulen = rd16(udp + 4);
    if (ulen < 8 || ulen > len) {
        drop_short();
        return;
    }
    sum = rd16(udp + 6);
    if (sum != 0) {
        csum = pseudo_sum(src, dst, PROTO_UDP, ulen);
        csum = csum_add(csum, udp, ulen);
        if (!csum_good(csum)) {
            drop_csum();
            return;
        }
    }
    dport = rd16(udp + 2);
    sport = rd16(udp);
    for (si = 0; si < SOCK_MAX; si++) {
        if (!socks[si].used || socks[si].proto != SOCK_UDP)
            continue;
        if (socks[si].lport == dport)
            break;
    }
    if (si == SOCK_MAX)
        return;
    udp_queue(&socks[si], src, sport, udp + 8, (uint16_t)(ulen - 8));
}

static uint16_t tcp_build(uint8_t *seg, uint16_t sport, uint16_t dport, uint32_t seq, uint32_t ack,
                          uint8_t flags, uint16_t win, const uint8_t *data, uint16_t dlen,
                          uint32_t src, uint32_t dst)
{
    uint16_t tlen = (uint16_t)(20 + dlen);
    uint32_t sum;
    memset(seg, 0, 20);
    be16(seg + 0, sport);
    be16(seg + 2, dport);
    be32(seg + 4, seq);
    be32(seg + 8, ack);
    seg[12] = 0x50;
    seg[13] = flags;
    be16(seg + 14, win);
    if (dlen && data)
        memcpy(seg + 20, data, dlen);
    sum = pseudo_sum(src, dst, PROTO_TCP, tlen);
    sum = csum_add(sum, seg, tlen);
    be16(seg + 16, csum_fold(sum));
    return tlen;
}

static int tcp_csum_ok(uint32_t src, uint32_t dst, const uint8_t *seg, uint16_t len)
{
    uint32_t sum = pseudo_sum(src, dst, PROTO_TCP, len);
    sum = csum_add(sum, seg, len);
    return csum_good(sum);
}

static int tx_slot(struct sock *s)
{
    int free_i = -1;
    int live = 0;
    for (int i = 0; i < TCP_SEGS; i++) {
        if (s->tx[i].live)
            live++;
        else if (free_i < 0)
            free_i = i;
    }
    if (live >= TCP_SEGS)
        return -1;
    return free_i;
}

static uint32_t seg_span(const struct tseg *t)
{
    uint32_t n = t->len;
    if (t->flags & TH_SYN)
        n++;
    if (t->flags & TH_FIN)
        n++;
    return n;
}

static void tcp_retire(struct sock *s, uint32_t ack)
{
    for (int i = 0; i < TCP_SEGS; i++) {
        struct tseg *t = &s->tx[i];
        uint32_t end;
        if (!t->live)
            continue;
        end = t->seq + seg_span(t);
        if (seq_le(end, ack))
            t->live = 0;
    }
}

static void tcp_apply_ack(struct sock *s, uint32_t ack, uint16_t win)
{
    if (seq_gt(ack, s->snd_nxt))
        return;
    if (seq_gt(ack, s->snd_una)) {
        tcp_retire(s, ack);
        s->snd_una = ack;
    }
    s->peer_win = win;
    if (s->snd_una == s->snd_nxt) {
        if (s->state == TCP_FIN_WAIT1)
            s->state = TCP_FIN_WAIT2;
        else if (s->state == TCP_CLOSING)
            s->state = TCP_TIME_WAIT;
        else if (s->state == TCP_LAST_ACK) {
            s->state = TCP_CLOSED;
            s->used = 0;
        }
    }
}

static int tcp_output(struct sock *s, uint8_t flags, const uint8_t *data, uint16_t dlen, int record)
{
    uint8_t seg[TCP_MSS + 24];
    uint32_t seq = s->snd_nxt;
    uint32_t src = s->lip ? s->lip : ip_src_for(s->rip);
    uint16_t tlen;
    uint32_t consume = dlen;
    int slot = -1;
    if (flags & TH_SYN)
        consume++;
    if (flags & TH_FIN)
        consume++;
    if (dlen > TCP_MSS)
        return -EMSGSIZE;
    if (record) {
        slot = tx_slot(s);
        if (slot < 0)
            return -EAGAIN;
        s->tx[slot].seq = seq;
        s->tx[slot].len = dlen;
        s->tx[slot].flags = flags;
        s->tx[slot].live = 1;
        s->tx[slot].tstamp = uptime_ms();
        if (dlen && data)
            memcpy(s->tx[slot].data, data, dlen);
    }
    tlen = tcp_build(seg, s->lport, s->rport, seq, s->rcv_nxt, flags, TCP_MSS * TCP_SEGS, data, dlen,
                     src, s->rip);
    s->snd_nxt += consume;
    return ip_send(s->rip, PROTO_TCP, seg, tlen, NULL);
}

static void tcp_resend(struct sock *s, struct tseg *t)
{
    uint8_t seg[TCP_MSS + 24];
    uint32_t src = s->lip ? s->lip : ip_src_for(s->rip);
    uint16_t tlen = tcp_build(seg, s->lport, s->rport, t->seq, s->rcv_nxt, t->flags,
                              TCP_MSS * TCP_SEGS, t->data, t->len, src, s->rip);
    t->tstamp = uptime_ms();
    ip_send(s->rip, PROTO_TCP, seg, tlen, NULL);
}

static void tcp_tick(void)
{
    uint64_t now = uptime_ms();
    for (int i = 0; i < SOCK_MAX; i++) {
        struct sock *s = &socks[i];
        if (!s->used || s->proto != SOCK_TCP)
            continue;
        for (int k = 0; k < TCP_SEGS; k++) {
            if (!s->tx[k].live)
                continue;
            if (now - s->tx[k].tstamp < RTO_MS)
                continue;
            tcp_resend(s, &s->tx[k]);
        }
    }
}

static void tcp_rst(uint32_t dst, uint32_t src, uint16_t dport, uint16_t sport, uint32_t seq,
                    uint32_t ack, uint8_t flags, uint32_t seglen)
{
    uint8_t seg[24];
    uint16_t tlen;
    if (flags & TH_ACK) {
        tlen = tcp_build(seg, dport, sport, ack, 0, TH_RST, 0, NULL, 0, dst, src);
    } else {
        tlen = tcp_build(seg, dport, sport, 0, seq + seglen, TH_RST | TH_ACK, 0, NULL, 0, dst, src);
    }
    ip_send(src, PROTO_TCP, seg, tlen, NULL);
}

static int tcp_lookup(uint32_t dst, uint16_t dport, uint32_t src, uint16_t sport)
{
    int listen = -1;
    for (int i = 0; i < SOCK_MAX; i++) {
        struct sock *s = &socks[i];
        if (!s->used || s->proto != SOCK_TCP || s->lport != dport)
            continue;
        if (s->lip != 0 && s->lip != dst)
            continue;
        if (s->state != TCP_LISTEN && s->rip == src && s->rport == sport)
            return i;
        if (s->state == TCP_LISTEN)
            listen = i;
    }
    return listen;
}

static int tcp_rx_put(struct sock *s, const uint8_t *data, uint16_t len)
{
    if (s->rxn >= TCP_SEGS || len > TCP_MSS)
        return -1;
    memcpy(s->rx[s->rxt], data, len);
    s->rxlen[s->rxt] = len;
    s->rxt = (uint8_t)((s->rxt + 1) % TCP_SEGS);
    s->rxn++;
    return 0;
}

static void tcp_in_data(struct sock *s, uint32_t seq, uint8_t flags, const uint8_t *data, uint16_t plen)
{
    int need = 0;
    if (s->state != TCP_ESTABLISHED && s->state != TCP_FIN_WAIT1 && s->state != TCP_FIN_WAIT2 &&
        s->state != TCP_CLOSE_WAIT)
        return;
    if (plen > 0) {
        if (seq == s->rcv_nxt) {
            if (tcp_rx_put(s, data, plen) == 0) {
                s->rcv_nxt += plen;
                need = 1;
            }
        } else if (seq_lt(seq, s->rcv_nxt)) {
            need = 1;
        }
    }
    if ((flags & TH_FIN) && !s->fin_rx && seq + plen == s->rcv_nxt) {
        s->rcv_nxt++;
        s->fin_rx = 1;
        need = 1;
        if (s->state == TCP_ESTABLISHED)
            s->state = TCP_CLOSE_WAIT;
        else if (s->state == TCP_FIN_WAIT2)
            s->state = TCP_TIME_WAIT;
        else if (s->state == TCP_FIN_WAIT1) {
            if (s->snd_una == s->snd_nxt)
                s->state = TCP_TIME_WAIT;
            else
                s->state = TCP_CLOSING;
        }
    }
    if (need && s->used)
        tcp_output(s, TH_ACK, NULL, 0, 0);
}

static void tcp_accept_syn(struct sock *listen, uint32_t src, uint32_t dst, uint16_t sport,
                           uint32_t seq, uint16_t win)
{
    int c = sock_alloc(SOCK_TCP);
    struct sock *n;
    if (c < 0)
        return;
    n = &socks[c];
    n->state = TCP_SYN_RECV;
    n->lip = dst;
    n->lport = listen->lport;
    n->rip = src;
    n->rport = sport;
    n->parent = (int)(listen - socks);
    n->rcv_nxt = seq + 1;
    n->snd_iss = tcp_isn;
    n->snd_una = tcp_isn;
    n->snd_nxt = tcp_isn;
    tcp_isn += 100000;
    if (win)
        n->peer_win = win;
    tcp_output(n, TH_SYN | TH_ACK, NULL, 0, 1);
}

static void tcp_input(uint32_t src, uint32_t dst, const uint8_t *tcp, size_t len)
{
    uint16_t sport;
    uint16_t dport;
    uint32_t seq;
    uint32_t ack;
    uint8_t flags;
    uint16_t win;
    unsigned off;
    uint16_t plen;
    uint32_t seglen;
    int si;
    struct sock *s;
    if (len < 20)
        return;
    if (!tcp_csum_ok(src, dst, tcp, (uint16_t)len)) {
        drop_csum();
        return;
    }
    sport = rd16(tcp);
    dport = rd16(tcp + 2);
    seq = rd32(tcp + 4);
    ack = rd32(tcp + 8);
    off = (unsigned)(tcp[12] >> 4) * 4u;
    flags = tcp[13];
    win = rd16(tcp + 14);
    if (off < 20 || off > len)
        return;
    plen = (uint16_t)(len - off);
    seglen = plen;
    if (flags & TH_SYN)
        seglen++;
    if (flags & TH_FIN)
        seglen++;
    si = tcp_lookup(dst, dport, src, sport);
    if (si < 0) {
        if (!(flags & TH_RST))
            tcp_rst(dst, src, dport, sport, seq, ack, flags, seglen);
        return;
    }
    s = &socks[si];
    if (s->state == TCP_LISTEN) {
        if ((flags & TH_SYN) && !(flags & TH_ACK))
            tcp_accept_syn(s, src, dst, sport, seq, win);
        return;
    }
    if (flags & TH_RST) {
        s->state = TCP_CLOSED;
        s->used = 0;
        return;
    }
    if (s->state == TCP_SYN_SENT) {
        if ((flags & (TH_SYN | TH_ACK)) == (TH_SYN | TH_ACK) && ack == s->snd_iss + 1) {
            s->rcv_nxt = seq + 1;
            tcp_apply_ack(s, ack, win);
            if (s->used) {
                s->state = TCP_ESTABLISHED;
                tcp_output(s, TH_ACK, NULL, 0, 0);
            }
        }
        return;
    }
    if (flags & TH_ACK)
        tcp_apply_ack(s, ack, win);
    if (!s->used)
        return;
    if (s->state == TCP_SYN_RECV && s->snd_una == s->snd_iss + 1)
        s->state = TCP_ESTABLISHED;
    tcp_in_data(s, seq, flags, tcp + off, plen);
}

static int dns_skip_name(const uint8_t *pkt, size_t len, size_t off, size_t *next)
{
    size_t pos = off;
    int jumps = 0;
    int jumped = 0;
    size_t end = off;
    if (off >= len)
        return -1;
    for (;;) {
        uint8_t lab;
        if (pos >= len)
            return -1;
        lab = pkt[pos];
        if (lab == 0) {
            if (!jumped)
                end = pos + 1;
            *next = end;
            return 0;
        }
        if ((lab & 0xC0) == 0xC0) {
            uint16_t ptr;
            if (pos + 1 >= len)
                return -1;
            if (!jumped)
                end = pos + 2;
            ptr = (uint16_t)(((lab & 0x3F) << 8) | pkt[pos + 1]);
            if (ptr >= len)
                return -1;
            pos = ptr;
            jumped = 1;
            if (++jumps > 8)
                return -1;
            continue;
        }
        if (lab & 0xC0)
            return -1;
        if (pos + 1u + lab > len)
            return -1;
        pos += 1u + lab;
        if (!jumped)
            end = pos;
    }
}

static int dns_put_name(uint8_t *dst, size_t cap, size_t off, const char *name)
{
    size_t i = 0;
    if (!name || !name[0])
        return -1;
    while (name[i]) {
        size_t start = i;
        size_t lab;
        while (name[i] && name[i] != '.')
            i++;
        lab = i - start;
        if (lab == 0 || lab > 63 || off + 1 + lab >= cap)
            return -1;
        dst[off++] = (uint8_t)lab;
        memcpy(dst + off, name + start, lab);
        off += lab;
        if (name[i] == '.')
            i++;
    }
    if (off >= cap)
        return -1;
    dst[off++] = 0;
    return (int)off;
}

int dns_parse_a(const uint8_t *msg, size_t len, uint32_t *addr)
{
    uint16_t qd;
    uint16_t an;
    size_t off = 12;
    if (!msg || !addr || len < 12)
        return -1;
    qd = rd16(msg + 4);
    an = rd16(msg + 6);
    for (uint16_t i = 0; i < qd; i++) {
        if (dns_skip_name(msg, len, off, &off) != 0)
            return -1;
        if (off + 4 > len)
            return -1;
        off += 4;
    }
    for (uint16_t i = 0; i < an; i++) {
        uint16_t typ;
        uint16_t cls;
        uint16_t rdlen;
        if (dns_skip_name(msg, len, off, &off) != 0)
            return -1;
        if (off + 10 > len)
            return -1;
        typ = rd16(msg + off);
        cls = rd16(msg + off + 2);
        rdlen = rd16(msg + off + 8);
        off += 10;
        if (off + rdlen > len)
            return -1;
        if (typ == 1 && cls == 1 && rdlen == 4) {
            *addr = rd32(msg + off);
            return 0;
        }
        off += rdlen;
    }
    return -1;
}

int dns_query(const char *name, uint32_t *addr)
{
    uint8_t q[512];
    uint8_t resp[512];
    int end;
    int sock;
    int n;
    uint32_t sip = 0;
    uint16_t sport = 0;
    uint16_t id = 0xBEEF;
    net_ensure();
    if (!name || !addr)
        return -EINVAL;
    memset(q, 0, 12);
    be16(q + 0, id);
    be16(q + 2, 0x0100);
    be16(q + 4, 1);
    end = dns_put_name(q, sizeof(q), 12, name);
    if (end < 0 || (size_t)end + 4 > sizeof(q))
        return -EINVAL;
    be16(q + end, 1);
    be16(q + end + 2, 1);
    end += 4;
    sock = udp_bind(0);
    if (sock < 0)
        return sock;
    n = udp_sendto(sock, 0x0A000203u, 53, q, (size_t)end);
    if (n < 0) {
        udp_close(sock);
        return n;
    }
    n = udp_recvfrom(sock, resp, sizeof(resp), &sip, &sport);
    udp_close(sock);
    if (n < 0)
        return n;
    return dns_parse_a(resp, (size_t)n, addr);
}

static void ip_input(const uint8_t *frame, size_t len)
{
    const uint8_t *ip;
    uint32_t ihl;
    uint16_t total;
    uint16_t frag;
    uint32_t src;
    uint32_t dst;
    uint8_t proto;
    const uint8_t *payload;
    size_t plen;
    if (len < 34) {
        drop_short();
        return;
    }
    ip = frame + 14;
    if ((ip[0] >> 4) != 4)
        return;
    ihl = (uint32_t)(ip[0] & 0x0F) * 4u;
    if (ihl < 20 || 14u + ihl > len) {
        drop_short();
        return;
    }
    total = rd16(ip + 2);
    if (total < ihl || 14u + total > len) {
        drop_short();
        return;
    }
    if (!csum_good(csum_add(0, ip, ihl))) {
        drop_csum();
        return;
    }
    frag = rd16(ip + 6);
    if (frag & 0x3FFFu)
        return;
    src = rd32(ip + 12);
    dst = rd32(ip + 16);
    proto = ip[9];
    payload = ip + ihl;
    plen = (size_t)total - ihl;
    arp_learn(src, frame + 6);
    if (proto == PROTO_ICMP)
        icmp_input(frame, src, dst, payload, plen);
    else if (proto == PROTO_UDP)
        udp_input(src, dst, payload, plen);
    else if (proto == PROTO_TCP)
        tcp_input(src, dst, payload, plen);
}

void net_rx_inject(const void *frame, size_t len)
{
    const uint8_t *f = frame;
    uint16_t et;
    net_ensure();
    if (!f || len < 14) {
        drop_short();
        return;
    }
    if (!mac_ok(f))
        return;
    et = rd16(f + 12);
    if (et == 0x0806) {
        arp_input(f, len);
        return;
    }
    if (et != 0x0800)
        return;
    ip_input(f, len);
}

int udp_bind(uint16_t port)
{
    int fd;
    net_ensure();
    if (port == 0)
        port = alloc_port();
    if (port == 0)
        return -EAGAIN;
    if (port_busy(port, SOCK_UDP))
        return -EADDRINUSE;
    fd = sock_alloc(SOCK_UDP);
    if (fd < 0)
        return -ENOMEM;
    socks[fd].lport = port;
    socks[fd].lip = host_ip;
    return fd;
}

int udp_close(int sock)
{
    struct sock *s = sock_fd(sock, SOCK_UDP);
    if (!s)
        return -EINVAL;
    s->used = 0;
    return 0;
}

int udp_sendto(int sock, uint32_t ip, uint16_t port, const void *data, size_t len)
{
    struct sock *s = sock_fd(sock, SOCK_UDP);
    uint8_t pkt[UDP_MAX + 8];
    uint16_t ulen;
    uint32_t src;
    uint32_t sum;
    uint16_t csum;
    if (!s || !data)
        return -EINVAL;
    if (len > UDP_MAX)
        return -EMSGSIZE;
    ulen = (uint16_t)(8 + len);
    src = ip_src_for(ip);
    memset(pkt, 0, 8);
    be16(pkt + 0, s->lport);
    be16(pkt + 2, port);
    be16(pkt + 4, ulen);
    memcpy(pkt + 8, data, len);
    sum = pseudo_sum(src, ip, PROTO_UDP, ulen);
    sum = csum_add(sum, pkt, ulen);
    csum = csum_fold(sum);
    if (csum == 0)
        csum = 0xFFFF;
    be16(pkt + 6, csum);
    if (ip_send(ip, PROTO_UDP, pkt, ulen, NULL) < 0)
        return -EAGAIN;
    return (int)len;
}

int udp_recvfrom(int sock, void *buf, size_t len, uint32_t *src_ip, uint16_t *src_port)
{
    struct sock *s = sock_fd(sock, SOCK_UDP);
    uint16_t have;
    size_t n;
    if (!s || !buf)
        return -EINVAL;
    if (s->un == 0)
        return -EAGAIN;
    have = s->ulen[s->uh];
    n = len < have ? len : have;
    memcpy(buf, s->uq[s->uh], n);
    if (src_ip)
        *src_ip = s->usrc[s->uh];
    if (src_port)
        *src_port = s->usport[s->uh];
    s->uh = (uint8_t)((s->uh + 1) % UDP_Q);
    s->un--;
    return (int)n;
}

int tcp_listen(uint16_t port)
{
    int fd;
    net_ensure();
    if (port == 0)
        return -EINVAL;
    if (port_busy(port, SOCK_TCP))
        return -EADDRINUSE;
    fd = sock_alloc(SOCK_TCP);
    if (fd < 0)
        return -ENOMEM;
    socks[fd].state = TCP_LISTEN;
    socks[fd].lport = port;
    socks[fd].lip = 0;
    return fd;
}

int tcp_accept(int sock)
{
    struct sock *s = sock_fd(sock, SOCK_TCP);
    if (!s || s->state != TCP_LISTEN)
        return -EINVAL;
    for (int i = 0; i < SOCK_MAX; i++) {
        if (!socks[i].used || socks[i].proto != SOCK_TCP)
            continue;
        if (socks[i].parent != sock)
            continue;
        if (socks[i].state != TCP_ESTABLISHED && socks[i].state != TCP_CLOSE_WAIT)
            continue;
        socks[i].parent = -1;
        return i;
    }
    return -EAGAIN;
}

int tcp_connect(uint32_t ip, uint16_t port)
{
    int fd;
    struct sock *s;
    uint16_t local;
    net_ensure();
    if (port == 0)
        return -EINVAL;
    local = alloc_port();
    if (local == 0)
        return -EAGAIN;
    fd = sock_alloc(SOCK_TCP);
    if (fd < 0)
        return -ENOMEM;
    s = &socks[fd];
    s->lip = ip_src_for(ip);
    s->lport = local;
    s->rip = ip;
    s->rport = port;
    s->snd_iss = tcp_isn;
    s->snd_una = tcp_isn;
    s->snd_nxt = tcp_isn;
    tcp_isn += 100000;
    s->state = TCP_SYN_SENT;
    s->peer_win = TCP_MSS * TCP_SEGS;
    if (tcp_output(s, TH_SYN, NULL, 0, 1) < 0) {
        s->used = 0;
        return -EAGAIN;
    }
    if (!s->used || s->state == TCP_CLOSED)
        return -ECONNREFUSED;
    if (s->state == TCP_ESTABLISHED)
        return fd;
    return -EAGAIN;
}

int tcp_send(int sock, const void *data, size_t len)
{
    struct sock *s = sock_fd(sock, SOCK_TCP);
    const uint8_t *p = data;
    size_t off = 0;
    if (!s || !data)
        return -EINVAL;
    if (s->state != TCP_ESTABLISHED && s->state != TCP_CLOSE_WAIT)
        return -ENOTCONN;
    while (off < len) {
        uint16_t chunk;
        uint32_t inflight;
        size_t left = len - off;
        if (tx_slot(s) < 0)
            return off ? (int)off : -EAGAIN;
        chunk = left > TCP_MSS ? TCP_MSS : (uint16_t)left;
        inflight = s->snd_nxt - s->snd_una;
        if ((uint32_t)chunk + inflight > s->peer_win)
            return off ? (int)off : -EAGAIN;
        if (tcp_output(s, TH_PSH | TH_ACK, p + off, chunk, 1) < 0)
            return off ? (int)off : -EAGAIN;
        off += chunk;
    }
    return (int)off;
}

int tcp_recv(int sock, void *buf, size_t len)
{
    struct sock *s = sock_fd(sock, SOCK_TCP);
    uint16_t have;
    size_t n;
    if (!s || !buf)
        return -EINVAL;
    if (s->state == TCP_LISTEN || s->state == TCP_SYN_SENT || s->state == TCP_SYN_RECV)
        return -ENOTCONN;
    if (s->rxn == 0) {
        if (s->fin_rx)
            return 0;
        return -EAGAIN;
    }
    have = s->rxlen[s->rxh];
    n = len < have ? len : have;
    memcpy(buf, s->rx[s->rxh], n);
    if (n < have) {
        memmove(s->rx[s->rxh], s->rx[s->rxh] + n, have - n);
        s->rxlen[s->rxh] = (uint16_t)(have - n);
    } else {
        s->rxh = (uint8_t)((s->rxh + 1) % TCP_SEGS);
        s->rxn--;
    }
    return (int)n;
}

int tcp_close(int sock)
{
    struct sock *s = sock_fd(sock, SOCK_TCP);
    if (!s)
        return -EINVAL;
    if (s->state == TCP_ESTABLISHED || s->state == TCP_SYN_RECV) {
        s->state = TCP_FIN_WAIT1;
        tcp_output(s, TH_FIN | TH_ACK, NULL, 0, 1);
    } else if (s->state == TCP_CLOSE_WAIT) {
        s->state = TCP_LAST_ACK;
        tcp_output(s, TH_FIN | TH_ACK, NULL, 0, 1);
    } else if (s->state == TCP_LISTEN || s->state == TCP_SYN_SENT) {
        s->state = TCP_CLOSED;
        s->used = 0;
    }
    return 0;
}

int tcp_sock_state(int sock)
{
    if (sock < 0 || sock >= SOCK_MAX || !socks[sock].used || socks[sock].proto != SOCK_TCP)
        return -1;
    return socks[sock].state;
}

void net_poll(void)
{
    arp_sweep();
    tcp_tick();
    virtio_net_poll();
}

static int dns_check(void)
{
    uint8_t pkt[96];
    uint32_t ip = 0;
    size_t o = 12;
    memset(pkt, 0, sizeof(pkt));
    be16(pkt + 0, 0x1234);
    be16(pkt + 2, 0x8180);
    be16(pkt + 4, 1);
    be16(pkt + 6, 1);
    pkt[o++] = 3;
    memcpy(pkt + o, "www", 3);
    o += 3;
    pkt[o++] = 7;
    memcpy(pkt + o, "example", 7);
    o += 7;
    pkt[o++] = 3;
    memcpy(pkt + o, "com", 3);
    o += 3;
    pkt[o++] = 0;
    be16(pkt + o, 1);
    o += 2;
    be16(pkt + o, 1);
    o += 2;
    pkt[o++] = 0xC0;
    pkt[o++] = 12;
    be16(pkt + o, 1);
    o += 2;
    be16(pkt + o, 1);
    o += 2;
    be32(pkt + o, 60);
    o += 4;
    be16(pkt + o, 4);
    o += 2;
    pkt[o++] = 1;
    pkt[o++] = 2;
    pkt[o++] = 3;
    pkt[o++] = 4;
    if (dns_parse_a(pkt, o, &ip) != 0 || ip != 0x01020304u)
        return -1;
    return 0;
}

static uint8_t seen[FRAME_MAX];
static size_t seen_len;
static int seen_got;

static void on_tx(const void *frame, size_t len)
{
    if (len > sizeof(seen))
        len = sizeof(seen);
    memcpy(seen, frame, len);
    seen_len = len;
    seen_got = 1;
}

static void craft_echo(uint8_t *frame, size_t *len, const uint8_t mac[6])
{
    uint8_t *ip;
    uint8_t *icmp;
    const char payload[] = "ping";
    memset(frame, 0, 64);
    memcpy(frame, mac, 6);
    frame[6] = 0x52;
    frame[7] = 0x54;
    frame[8] = 0x00;
    frame[9] = 0x12;
    frame[10] = 0x34;
    frame[11] = 0x56;
    be16(frame + 12, 0x0800);
    ip = frame + 14;
    ip[0] = 0x45;
    be16(ip + 2, 32);
    be16(ip + 4, 1);
    ip[8] = 64;
    ip[9] = PROTO_ICMP;
    be32(ip + 12, 0x0A000202u);
    be32(ip + 16, host_ip);
    be16(ip + 10, csum_fold(csum_add(0, ip, 20)));
    icmp = ip + 20;
    icmp[0] = 8;
    memcpy(icmp + 8, payload, 4);
    be16(icmp + 2, csum_fold(csum_add(0, icmp, 12)));
    *len = 46;
}

void net_self_test(void)
{
    uint8_t mac[6];
    uint8_t frame[128];
    size_t flen = 0;
    uint64_t s0;
    uint64_t c0;
    uint8_t tiny[4];
    int udp;
    uint8_t ubuf[8];
    uint32_t sip = 0;
    uint16_t sport = 0;
    int n;
    uint8_t *ip;
    uint16_t ulen;
    uint32_t sum;
    uint16_t csum;
    net_ensure();
    (void)virtio_net_init();
    net_get_mac(mac);
    s0 = drops_short;
    memset(tiny, 0, sizeof(tiny));
    net_rx_inject(tiny, sizeof(tiny));
    if (drops_short != s0 + 1)
        panic("net: short");
    c0 = drops_csum;
    memset(frame, 0, sizeof(frame));
    memcpy(frame, mac, 6);
    be16(frame + 12, 0x0800);
    ip = frame + 14;
    ip[0] = 0x45;
    be16(ip + 2, 20);
    ip[8] = 64;
    ip[9] = PROTO_ICMP;
    be32(ip + 12, 0x0A000202u);
    be32(ip + 16, host_ip);
    be16(ip + 10, csum_fold(csum_add(0, ip, 20)));
    ip[10] ^= 0xFF;
    net_rx_inject(frame, 34);
    if (drops_csum != c0 + 1)
        panic("net: checksum");
    seen_got = 0;
    net_tx_capture = on_tx;
    craft_echo(frame, &flen, mac);
    net_rx_inject(frame, flen);
    net_tx_capture = NULL;
    if (!seen_got || seen_len < 46)
        panic("net: no reply");
    if (seen[23] != PROTO_ICMP || seen[34] != 0)
        panic("net: icmp type");
    if (rd32(seen + 26) != host_ip || rd32(seen + 30) != 0x0A000202u)
        panic("net: icmp addr");
    if (memcmp(seen + 42, "ping", 4) != 0)
        panic("net: icmp payload");
    if (!csum_good(csum_add(0, seen + 34, 12)))
        panic("net: icmp csum");
    pr_info("net", "icmp reply");
    udp = udp_bind(12345);
    if (udp < 0)
        panic("net: udp bind");
    memset(frame, 0, sizeof(frame));
    memcpy(frame, mac, 6);
    frame[6] = 0x52;
    be16(frame + 12, 0x0800);
    ip = frame + 14;
    ip[0] = 0x45;
    be16(ip + 2, 28);
    ip[8] = 64;
    ip[9] = PROTO_UDP;
    be32(ip + 12, 0x0A000202u);
    be32(ip + 16, host_ip);
    be16(ip + 10, csum_fold(csum_add(0, ip, 20)));
    be16(ip + 20, 9999);
    be16(ip + 22, 12345);
    ulen = 10;
    be16(ip + 24, ulen);
    ip[28] = 'h';
    ip[29] = 'i';
    sum = pseudo_sum(0x0A000202u, host_ip, PROTO_UDP, ulen);
    sum = csum_add(sum, ip + 20, ulen);
    csum = csum_fold(sum);
    if (csum == 0)
        csum = 0xFFFF;
    be16(ip + 26, csum);
    net_rx_inject(frame, 42);
    n = udp_recvfrom(udp, ubuf, sizeof(ubuf), &sip, &sport);
    udp_close(udp);
    if (n != 2 || ubuf[0] != 'h' || ubuf[1] != 'i' || sip != 0x0A000202u || sport != 9999)
        panic("net: udp");
    pr_info("net", "udp ok");
    if (dns_check() != 0)
        panic("net: dns");
    pr_info("net", "all checks passed");
}

void tcp_self_test(void)
{
    int refused;
    int ln;
    int client;
    int accepted;
    char buf[8];
    int n;
    int st;
    net_ensure();
    refused = tcp_connect(0x7F000001u, 9);
    if (refused >= 0)
        panic("net: rst");
    ln = tcp_listen(80);
    if (ln < 0)
        panic("net: listen");
    client = tcp_connect(0x7F000001u, 80);
    if (client < 0 || tcp_sock_state(client) != TCP_ESTABLISHED)
        panic("net: connect");
    accepted = tcp_accept(ln);
    if (accepted < 0 || tcp_sock_state(accepted) != TCP_ESTABLISHED)
        panic("net: accept");
    if (tcp_send(client, "ping", 4) != 4)
        panic("net: send");
    n = tcp_recv(accepted, buf, sizeof(buf));
    if (n != 4 || memcmp(buf, "ping", 4) != 0)
        panic("net: recv");
    if (tcp_send(accepted, "pong", 4) != 4)
        panic("net: reply");
    n = tcp_recv(client, buf, sizeof(buf));
    if (n != 4 || memcmp(buf, "pong", 4) != 0)
        panic("net: echo");
    if (tcp_close(client) != 0)
        panic("net: close client");
    st = tcp_sock_state(client);
    if (st != TCP_FIN_WAIT2 && st != TCP_TIME_WAIT)
        panic("net: fin wait");
    if (tcp_close(accepted) != 0)
        panic("net: close server");
    if (tcp_sock_state(accepted) != -1)
        panic("net: server linger");
    st = tcp_sock_state(client);
    if (st != TCP_TIME_WAIT && st != TCP_CLOSED)
        panic("net: time wait");
    net_poll();
    pr_info("net", "tcp loopback ok");
}
