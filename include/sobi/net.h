#ifndef SOBI_NET_H
#define SOBI_NET_H

#include <sobi/types.h>

#ifndef EAGAIN
#define EAGAIN 11
#endif
#ifndef ENOMEM
#define ENOMEM 12
#endif
#ifndef ENODEV
#define ENODEV 19
#endif
#ifndef EINVAL
#define EINVAL 22
#endif
#ifndef EPIPE
#define EPIPE 32
#endif
#ifndef EMSGSIZE
#define EMSGSIZE 90
#endif
#ifndef EADDRINUSE
#define EADDRINUSE 98
#endif
#ifndef ECONNRESET
#define ECONNRESET 104
#endif
#ifndef ENOTCONN
#define ENOTCONN 107
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT 110
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED 111
#endif

enum tcp_state {
    TCP_CLOSED = 0,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECV,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT1,
    TCP_FIN_WAIT2,
    TCP_CLOSING,
    TCP_TIME_WAIT,
    TCP_CLOSE_WAIT,
    TCP_LAST_ACK
};

typedef void (*net_tx_fn)(const void *frame, size_t len);

extern net_tx_fn net_tx_capture;

void net_set_addr(uint32_t ip, uint32_t mask, uint32_t gw);
void net_get_mac(uint8_t mac[6]);
void net_set_mac(const uint8_t mac[6]);
void net_rx_inject(const void *frame, size_t len);
void net_poll(void);
uint64_t net_stat_short(void);
uint64_t net_stat_csum(void);

int udp_bind(uint16_t port);
int udp_sendto(int sock, uint32_t ip, uint16_t port, const void *data, size_t len);
int udp_recvfrom(int sock, void *buf, size_t len, uint32_t *src_ip, uint16_t *src_port);
int udp_close(int sock);

int tcp_listen(uint16_t port);
int tcp_accept(int sock);
int tcp_connect(uint32_t ip, uint16_t port);
int tcp_send(int sock, const void *data, size_t len);
int tcp_recv(int sock, void *buf, size_t len);
int tcp_close(int sock);
int tcp_sock_state(int sock);

int dns_query(const char *name, uint32_t *addr);
int dns_parse_a(const uint8_t *msg, size_t len, uint32_t *addr);

int virtio_net_init(void);
int virtio_net_tx(const void *frame, size_t len);
void virtio_net_poll(void);

void net_self_test(void);
void tcp_self_test(void);

#endif
