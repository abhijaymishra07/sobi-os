#include <sobi/virtio.h>
#include <sobi/block.h>
#include <sobi/pmm.h>
#include <sobi/boot.h>
#include <sobi/io.h>
#include <sobi/cpu.h>
#include <sobi/string.h>
#include <sobi/log.h>
#include <sobi/compiler.h>

/* Legacy (virtio 0.9.5) PCI transport: registers live in I/O BAR0. */
#define VIO_DEV_FEATURES 0x00
#define VIO_DRV_FEATURES 0x04
#define VIO_QUEUE_PFN    0x08
#define VIO_QUEUE_SIZE   0x0C
#define VIO_QUEUE_SEL    0x0E
#define VIO_QUEUE_NOTIFY 0x10
#define VIO_STATUS       0x12
#define VIO_ISR          0x13
#define VIO_BLK_CAPACITY 0x14

#define VIRTIO_BLK_T_IN    0
#define VIRTIO_BLK_T_OUT   1
#define VIRTIO_BLK_T_FLUSH 4

#define BOUNCE_PAGES   16
#define BOUNCE_SECTORS (BOUNCE_PAGES * 4096 / BLOCK_SECTOR)

struct blk_req_hdr {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __packed;

struct vblk {
    uint16_t io;
    uint16_t qsz;
    uint16_t last_used;
    uint32_t features;
    struct virtq_desc *desc;
    volatile uint16_t *avail;
    volatile uint8_t *used;
    uint64_t hdr_phys;
    struct blk_req_hdr *hdr;
    volatile uint8_t *status;
    uint64_t bounce_phys;
    uint8_t *bounce;
};

static struct vblk disks[2];
static uint32_t ndisks;

static void *phys_virt(uint64_t phys)
{
    return (void *)(boot_info->hhdm_offset + phys);
}

bool virtio_blk_match(const struct pci_dev *dev)
{
    return dev && dev->vendor == VIRTIO_VENDOR && dev->device == VIRTIO_ID_BLOCK &&
           (dev->bars[0].flags & PCI_BAR_IO);
}

static int submit(struct vblk *d, uint32_t type, uint64_t sector, uint32_t bytes, bool device_writes)
{
    d->hdr->type = type;
    d->hdr->reserved = 0;
    d->hdr->sector = sector;
    *d->status = 0xFF;

    uint16_t n = 0;
    d->desc[0] = (struct virtq_desc){ d->hdr_phys, sizeof(struct blk_req_hdr), VIRTQ_DESC_F_NEXT, 1 };
    n = 1;
    if (bytes) {
        d->desc[1] = (struct virtq_desc){ d->bounce_phys, bytes,
                                          (uint16_t)(VIRTQ_DESC_F_NEXT | (device_writes ? VIRTQ_DESC_F_WRITE : 0)), 2 };
        d->desc[0].next = 1;
        n = 2;
    }
    d->desc[n - 1].next = n;
    d->desc[n] = (struct virtq_desc){ d->hdr_phys + sizeof(struct blk_req_hdr), 1, VIRTQ_DESC_F_WRITE, 0 };

    uint16_t aidx = d->avail[1];
    d->avail[2 + (aidx % d->qsz)] = 0;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    d->avail[1] = (uint16_t)(aidx + 1);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    outw((uint16_t)(d->io + VIO_QUEUE_NOTIFY), 0);

    volatile uint16_t *used_idx = (volatile uint16_t *)(d->used + 2);
    for (uint64_t spins = 0; *used_idx == d->last_used; spins++) {
        if (spins > 200000000ULL)
            return -EIO;
        cpu_pause();
    }
    d->last_used++;
    (void)inb((uint16_t)(d->io + VIO_ISR));
    return *d->status == 0 ? 0 : -EIO;
}

static int vblk_xfer(struct vblk *d, uint64_t lba, uint8_t *buf, uint32_t n, bool write)
{
    while (n) {
        uint32_t chunk = n > BOUNCE_SECTORS ? BOUNCE_SECTORS : n;
        uint32_t bytes = chunk * BLOCK_SECTOR;
        if (write)
            memcpy(d->bounce, buf, bytes);
        uint64_t flags = cpu_read_flags();
        cpu_cli();
        int rc = submit(d, write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN, lba, bytes, !write);
        if (flags & 0x200)
            cpu_sti();
        if (rc)
            return rc;
        if (!write)
            memcpy(buf, d->bounce, bytes);
        buf += bytes;
        lba += chunk;
        n -= chunk;
    }
    return 0;
}

static int vblk_read(void *ctx, uint64_t lba, void *buf, uint32_t n)
{
    return vblk_xfer(ctx, lba, buf, n, false);
}

static int vblk_write(void *ctx, uint64_t lba, const void *buf, uint32_t n)
{
    return vblk_xfer(ctx, lba, (uint8_t *)buf, n, true);
}

static int vblk_flush(void *ctx)
{
    struct vblk *d = ctx;
    if (!(d->features & (1u << VIRTIO_BLK_F_FLUSH)))
        return 0;
    uint64_t flags = cpu_read_flags();
    cpu_cli();
    int rc = submit(d, VIRTIO_BLK_T_FLUSH, 0, 0, false);
    if (flags & 0x200)
        cpu_sti();
    return rc;
}

int virtio_blk_attach(struct pci_dev *dev)
{
    if (ndisks >= 2)
        return -ENOMEM;
    struct vblk *d = &disks[ndisks];
    memset(d, 0, sizeof(*d));
    d->io = (uint16_t)dev->bars[0].addr;
    pci_set_command(dev, PCI_CMD_IO | PCI_CMD_MASTER);

    outb((uint16_t)(d->io + VIO_STATUS), 0);
    outb((uint16_t)(d->io + VIO_STATUS), VIRTIO_STATUS_ACK);
    outb((uint16_t)(d->io + VIO_STATUS), VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);
    uint32_t feat = inl((uint16_t)(d->io + VIO_DEV_FEATURES));
    d->features = feat & ((1u << VIRTIO_BLK_F_FLUSH) | (1u << VIRTIO_BLK_F_RO));
    outl((uint16_t)(d->io + VIO_DRV_FEATURES), d->features);

    outw((uint16_t)(d->io + VIO_QUEUE_SEL), 0);
    d->qsz = inw((uint16_t)(d->io + VIO_QUEUE_SIZE));
    if (d->qsz == 0 || d->qsz > 1024)
        goto fail;
    /* Legacy vring: descriptors + avail ring, then the used ring on the next 4 KiB boundary. */
    uint64_t avail_end = 16ULL * d->qsz + 6 + 2ULL * d->qsz;
    uint64_t used_off = (avail_end + 4095) & ~4095ULL;
    uint64_t total = used_off + 6 + 8ULL * d->qsz;
    uint64_t pages = (total + 4095) / 4096;
    uint64_t ring = pmm_alloc_contig(pages, 4096);
    uint64_t misc = pmm_alloc();
    d->bounce_phys = pmm_alloc_contig(BOUNCE_PAGES, 4096);
    if (!ring || !misc || !d->bounce_phys)
        goto fail;
    uint8_t *rv = phys_virt(ring);
    memset(rv, 0, pages * 4096);
    d->desc = (struct virtq_desc *)rv;
    d->avail = (volatile uint16_t *)(rv + 16ULL * d->qsz);
    d->used = rv + used_off;
    d->hdr_phys = misc;
    d->hdr = phys_virt(misc);
    d->status = (volatile uint8_t *)d->hdr + sizeof(struct blk_req_hdr);
    d->bounce = phys_virt(d->bounce_phys);
    d->avail[0] = VIRTQ_AVAIL_F_NO_INTERRUPT;
    outl((uint16_t)(d->io + VIO_QUEUE_PFN), (uint32_t)(ring >> 12));
    outb((uint16_t)(d->io + VIO_STATUS),
         VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);

    uint64_t capacity = inl((uint16_t)(d->io + VIO_BLK_CAPACITY)) |
                        ((uint64_t)inl((uint16_t)(d->io + VIO_BLK_CAPACITY + 4)) << 32);
    bool ro = d->features & (1u << VIRTIO_BLK_F_RO);
    int id = blk_register(capacity, d, vblk_read, ro ? NULL : vblk_write, vblk_flush);
    if (id < 0)
        goto fail;
    ndisks++;
    pr_info("virtio-blk", "disk %d: %lu sectors, queue %u%s", id, capacity, d->qsz, ro ? ", read-only" : "");
    return id;
fail:
    outb((uint16_t)(d->io + VIO_STATUS), VIRTIO_STATUS_FAILED);
    pr_info("virtio-blk", "attach failed");
    return -EIO;
}
