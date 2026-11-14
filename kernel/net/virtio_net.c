#include <sobi/net.h>
#include <sobi/io.h>
#include <sobi/vmm.h>
#include <sobi/string.h>
#include <sobi/log.h>
#include <sobi/compiler.h>

#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC
#define VIRTIO_VENDOR 0x1AF4
#define VIRTIO_QMAX 256
#define VIRTIO_QMEM 16384
#define VIRTIO_BUFS 8
#define VIRTIO_BUFSZ 2048
#define VRING_DESC_F_WRITE 2

static uint8_t __aligned(4096) vq_area[2][VIRTIO_QMEM];
static uint8_t __aligned(16) pkt_buf[2][VIRTIO_BUFS][VIRTIO_BUFSZ];

struct virtq {
    uint16_t qsz;
    uint16_t last_used;
    uint16_t notify;
    uint8_t *base;
    uint8_t *avail;
    uint8_t *used;
    int ready;
};

static struct virtq vq[2];
static uint16_t io_base;
static int dev_ready;
static uint8_t tx_used[VIRTIO_BUFS];

static uint32_t pci_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off)
{
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | (off & 0xFCu);
    outl(PCI_ADDR, addr);
    return inl(PCI_DATA);
}

static void pci_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t val)
{
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | (off & 0xFCu);
    outl(PCI_ADDR, addr);
    outl(PCI_DATA, val);
}

static uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off)
{
    uint32_t v = pci_read(bus, dev, func, off);
    return (uint16_t)((v >> ((off & 2) * 8)) & 0xFFFFu);
}

static int find_io_bar(uint8_t bus, uint8_t dev, uint8_t func, uint16_t *port)
{
    for (uint8_t bar = 0; bar < 6; bar++) {
        uint32_t raw = pci_read(bus, dev, func, (uint8_t)(0x10 + bar * 4));
        if ((raw & 1u) == 0)
            continue;
        if ((raw & ~3u) == 0)
            continue;
        *port = (uint16_t)(raw & ~3u);
        return 0;
    }
    return -1;
}

static int translate(void *ptr, uint64_t *phys)
{
    if (!vmm_translate((uint64_t)ptr, phys, NULL))
        return -1;
    return 0;
}

static int contig(void *ptr, size_t bytes, uint64_t *phys)
{
    uint64_t base;
    size_t pages;
    if (translate(ptr, &base) != 0)
        return -1;
    pages = (bytes + 4095u) / 4096u;
    for (size_t i = 1; i < pages; i++) {
        uint64_t page = 0;
        if (translate((uint8_t *)ptr + i * 4096u, &page) != 0)
            return -1;
        if (page != base + i * 4096u)
            return -1;
    }
    if (base > 0xFFFFF000ull)
        return -1;
    *phys = base;
    return 0;
}

static void wr16(uint8_t *p, uint16_t v)
{
    memcpy(p, &v, sizeof(v));
}

static void wr32(uint8_t *p, uint32_t v)
{
    memcpy(p, &v, sizeof(v));
}

static void wr64(uint8_t *p, uint64_t v)
{
    memcpy(p, &v, sizeof(v));
}

static uint16_t rd16(const uint8_t *p)
{
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static uint32_t rd32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static size_t vring_need(uint16_t qsz)
{
    size_t desc = (size_t)qsz * 16u;
    size_t avail = 2u * (3u + (size_t)qsz);
    size_t used_at = (desc + avail + 4095u) & ~4095u;
    size_t used = 6u + (size_t)qsz * 8u;
    return used_at + used;
}

static int vq_setup(int index, uint16_t qsz)
{
    struct virtq *q = &vq[index];
    uint64_t phys = 0;
    size_t desc;
    size_t avail;
    size_t used_at;
    if (qsz == 0 || qsz > VIRTIO_QMAX)
        return -1;
    if (vring_need(qsz) > VIRTIO_QMEM)
        return -1;
    memset(vq_area[index], 0, VIRTIO_QMEM);
    if (contig(vq_area[index], vring_need(qsz), &phys) != 0)
        return -1;
    desc = (size_t)qsz * 16u;
    avail = 2u * (3u + (size_t)qsz);
    used_at = (desc + avail + 4095u) & ~4095u;
    q->qsz = qsz;
    q->last_used = 0;
    q->notify = (uint16_t)index;
    q->base = vq_area[index];
    q->avail = vq_area[index] + desc;
    q->used = vq_area[index] + used_at;
    q->ready = 1;
    outw((uint16_t)(io_base + 14), (uint16_t)index);
    outl((uint16_t)(io_base + 8), (uint32_t)(phys >> 12));
    return 0;
}

static void vq_kick(struct virtq *q, uint16_t desc)
{
    uint16_t idx = rd16(q->avail + 2);
    wr16(q->avail + 4 + ((size_t)(idx % q->qsz) * 2), desc);
    barrier();
    wr16(q->avail + 2, (uint16_t)(idx + 1));
    barrier();
    outw((uint16_t)(io_base + 16), q->notify);
}

static int post_rx(void)
{
    struct virtq *q = &vq[0];
    uint16_t n = q->qsz < VIRTIO_BUFS ? q->qsz : VIRTIO_BUFS;
    for (uint16_t i = 0; i < n; i++) {
        uint64_t phys = 0;
        if (translate(pkt_buf[0][i], &phys) != 0)
            return -1;
        wr64(q->base + (size_t)i * 16, phys);
        wr32(q->base + (size_t)i * 16 + 8, VIRTIO_BUFSZ);
        wr16(q->base + (size_t)i * 16 + 12, VRING_DESC_F_WRITE);
        wr16(q->base + (size_t)i * 16 + 14, 0);
        wr16(q->avail + 4 + (size_t)i * 2, i);
    }
    barrier();
    wr16(q->avail + 2, n);
    barrier();
    outw((uint16_t)(io_base + 16), 0);
    return 0;
}

static uint16_t tx_alloc(void)
{
    uint16_t n = vq[1].qsz < VIRTIO_BUFS ? vq[1].qsz : VIRTIO_BUFS;
    for (uint16_t i = 0; i < n; i++) {
        if (!tx_used[i]) {
            tx_used[i] = 1;
            return i;
        }
    }
    return 0xFFFF;
}

static void tx_reclaim(void)
{
    struct virtq *q = &vq[1];
    uint16_t idx;
    if (!q->ready)
        return;
    idx = rd16(q->used + 2);
    while (q->last_used != idx) {
        uint32_t id = rd32(q->used + 4 + ((size_t)(q->last_used % q->qsz) * 8));
        if (id < VIRTIO_BUFS)
            tx_used[id] = 0;
        q->last_used++;
    }
}

int virtio_net_tx(const void *frame, size_t len)
{
    struct virtq *q = &vq[1];
    uint16_t id;
    uint64_t phys = 0;
    uint8_t *buf;
    if (!dev_ready || !q->ready || !frame || len == 0 || len + 10 > VIRTIO_BUFSZ)
        return -1;
    tx_reclaim();
    id = tx_alloc();
    if (id == 0xFFFF) {
        tx_reclaim();
        id = tx_alloc();
    }
    if (id == 0xFFFF)
        return -1;
    buf = pkt_buf[1][id];
    memset(buf, 0, 10);
    memcpy(buf + 10, frame, len);
    if (translate(buf, &phys) != 0) {
        tx_used[id] = 0;
        return -1;
    }
    wr64(q->base + (size_t)id * 16, phys);
    wr32(q->base + (size_t)id * 16 + 8, (uint32_t)(len + 10));
    wr16(q->base + (size_t)id * 16 + 12, 0);
    wr16(q->base + (size_t)id * 16 + 14, 0);
    vq_kick(q, id);
    return 0;
}

void virtio_net_poll(void)
{
    struct virtq *q = &vq[0];
    uint16_t idx;
    uint16_t n;
    if (!dev_ready || !q->ready)
        return;
    (void)inb((uint16_t)(io_base + 19));
    idx = rd16(q->used + 2);
    n = q->qsz < VIRTIO_BUFS ? q->qsz : VIRTIO_BUFS;
    while (q->last_used != idx) {
        uint32_t id = rd32(q->used + 4 + ((size_t)(q->last_used % q->qsz) * 8));
        uint32_t rlen = rd32(q->used + 8 + ((size_t)(q->last_used % q->qsz) * 8));
        q->last_used++;
        if (id < n && rlen > 10)
            net_rx_inject(pkt_buf[0][id] + 10, rlen - 10);
        if (id < n)
            vq_kick(q, (uint16_t)id);
    }
    tx_reclaim();
}

static int dev_match(uint16_t device)
{
    return device == 0x1000 || device == 0x1041;
}

int virtio_net_init(void)
{
    uint8_t mac[6];
    int found = 0;
    uint8_t bus = 0;
    uint8_t dev = 0;
    uint8_t func = 0;
    uint16_t qsz0;
    uint16_t qsz1;
    dev_ready = 0;
    for (uint8_t b = 0; b < 8 && !found; b++) {
        for (uint8_t d = 0; d < 32 && !found; d++) {
            uint16_t vendor = pci_read16(b, d, 0, 0);
            uint8_t header;
            uint8_t nfunc;
            if (vendor == 0xFFFF)
                continue;
            header = (uint8_t)((pci_read(b, d, 0, 0x0C) >> 16) & 0xFF);
            nfunc = (header & 0x80) ? 8 : 1;
            for (uint8_t f = 0; f < nfunc; f++) {
                uint16_t ven = pci_read16(b, d, f, 0);
                uint16_t did = pci_read16(b, d, f, 2);
                if (ven == VIRTIO_VENDOR && dev_match(did)) {
                    bus = b;
                    dev = d;
                    func = f;
                    found = 1;
                    break;
                }
            }
        }
    }
    if (!found)
        return -1;
    if (find_io_bar(bus, dev, func, &io_base) != 0)
        return -1;
    {
        uint16_t cmd = pci_read16(bus, dev, func, 4);
        cmd |= 0x7;
        pci_write(bus, dev, func, 4, cmd);
    }
    outb((uint16_t)(io_base + 18), 0);
    io_wait();
    for (int i = 0; i < 1000 && inb((uint16_t)(io_base + 18)) != 0; i++)
        io_wait();
    outb((uint16_t)(io_base + 18), 1);
    outb((uint16_t)(io_base + 18), 1 | 2);
    {
        uint32_t offered = inl((uint16_t)(io_base + 0));
        outl((uint16_t)(io_base + 4), offered & (1u << 5));
        if (offered & (1u << 5)) {
            for (int i = 0; i < 6; i++)
                mac[i] = inb((uint16_t)(io_base + 20 + i));
            net_set_mac(mac);
        }
    }
    outw((uint16_t)(io_base + 14), 0);
    qsz0 = inw((uint16_t)(io_base + 12));
    outw((uint16_t)(io_base + 14), 1);
    qsz1 = inw((uint16_t)(io_base + 12));
    if (vq_setup(0, qsz0) != 0 || vq_setup(1, qsz1) != 0) {
        outb((uint16_t)(io_base + 18), 0x80);
        return -1;
    }
    if (post_rx() != 0) {
        outb((uint16_t)(io_base + 18), 0x80);
        return -1;
    }
    outb((uint16_t)(io_base + 18), 1 | 2 | 4);
    dev_ready = 1;
    pr_info("virtio", "net port %#x", io_base);
    return 0;
}
