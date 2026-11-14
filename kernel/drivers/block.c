#include <sobi/block.h>
#include <sobi/pci.h>
#include <sobi/virtio.h>
#include <sobi/sync.h>
#include <sobi/kheap.h>
#include <sobi/string.h>
#include <sobi/log.h>
#include <sobi/panic.h>

#define BLK_MAX_DEVS 4

struct blk_dev {
    uint64_t capacity;
    void *ctx;
    blk_read_fn read;
    blk_write_fn write;
    blk_flush_fn flush;
};

/* Direct-mapped, write-through sector cache shared by all devices. */
struct cache_slot {
    bool valid;
    uint32_t dev;
    uint64_t lba;
    uint8_t data[BLOCK_SECTOR];
};

static struct blk_dev devs[BLK_MAX_DEVS];
static uint32_t ndevs;
static struct cache_slot cache[BLOCK_CACHE_SLOTS];
static mutex_t blk_lock;
static bool lock_ready;

uint64_t cache_hits;
uint64_t cache_misses;

static void lock(void)
{
    if (!lock_ready) {
        mutex_init(&blk_lock);
        lock_ready = true;
    }
    mutex_lock(&blk_lock);
}

static struct cache_slot *slot_for(uint32_t dev, uint64_t lba)
{
    return &cache[(lba + dev * 7) % BLOCK_CACHE_SLOTS];
}

int blk_register(uint64_t capacity, void *ctx, blk_read_fn read, blk_write_fn write, blk_flush_fn flush)
{
    if (ndevs >= BLK_MAX_DEVS || !read)
        return -ENOMEM;
    devs[ndevs] = (struct blk_dev){ capacity, ctx, read, write, flush };
    return (int)ndevs++;
}

static struct blk_dev *get(uint32_t dev, uint64_t lba, uint32_t n)
{
    if (dev >= ndevs)
        return NULL;
    struct blk_dev *d = &devs[dev];
    if (lba + n > d->capacity || lba + n < lba)
        return NULL;
    return d;
}

int blk_read(uint32_t dev, uint64_t lba, void *buf, uint32_t nsectors)
{
    struct blk_dev *d = get(dev, lba, nsectors);
    if (!d)
        return -EINVAL;
    lock();
    uint8_t *out = buf;
    for (uint32_t i = 0; i < nsectors; i++) {
        struct cache_slot *s = slot_for(dev, lba + i);
        if (s->valid && s->dev == dev && s->lba == lba + i) {
            cache_hits++;
            memcpy(out + (size_t)i * BLOCK_SECTOR, s->data, BLOCK_SECTOR);
            continue;
        }
        cache_misses++;
        /* Read the rest of the run in one request, then populate the cache. */
        uint32_t run = nsectors - i;
        int rc = d->read(d->ctx, lba + i, out + (size_t)i * BLOCK_SECTOR, run);
        if (rc) {
            mutex_unlock(&blk_lock);
            return -EIO;
        }
        for (uint32_t j = i; j < nsectors; j++) {
            struct cache_slot *c = slot_for(dev, lba + j);
            c->valid = true;
            c->dev = dev;
            c->lba = lba + j;
            memcpy(c->data, out + (size_t)j * BLOCK_SECTOR, BLOCK_SECTOR);
        }
        break;
    }
    mutex_unlock(&blk_lock);
    return 0;
}

int blk_write(uint32_t dev, uint64_t lba, const void *buf, uint32_t nsectors)
{
    struct blk_dev *d = get(dev, lba, nsectors);
    if (!d)
        return -EINVAL;
    if (!d->write)
        return -EROFS;
    lock();
    int rc = d->write(d->ctx, lba, buf, nsectors);
    const uint8_t *in = buf;
    for (uint32_t i = 0; i < nsectors; i++) {
        struct cache_slot *s = slot_for(dev, lba + i);
        if (rc) {
            if (s->valid && s->dev == dev && s->lba == lba + i)
                s->valid = false;
            continue;
        }
        s->valid = true;
        s->dev = dev;
        s->lba = lba + i;
        memcpy(s->data, in + (size_t)i * BLOCK_SECTOR, BLOCK_SECTOR);
    }
    mutex_unlock(&blk_lock);
    return rc ? -EIO : 0;
}

int blk_sync(uint32_t dev)
{
    if (dev >= ndevs)
        return -EINVAL;
    struct blk_dev *d = &devs[dev];
    if (!d->flush)
        return 0;
    lock();
    int rc = d->flush(d->ctx);
    mutex_unlock(&blk_lock);
    return rc ? -EIO : 0;
}

uint64_t blk_capacity(uint32_t dev)
{
    return dev < ndevs ? devs[dev].capacity : 0;
}

uint32_t blk_count(void)
{
    return ndevs;
}

int block_init(void)
{
    if (pci_count() == 0)
        pci_scan();
    for (uint32_t i = 0; i < pci_count(); i++) {
        struct pci_dev *p = pci_get(i);
        if (virtio_blk_match(p))
            virtio_blk_attach(p);
    }
    return (int)ndevs;
}

static uint8_t *ram_disk;

static int ram_rd(void *ctx, uint64_t lba, void *buf, uint32_t n)
{
    (void)ctx;
    memcpy(buf, ram_disk + lba * BLOCK_SECTOR, (size_t)n * BLOCK_SECTOR);
    return 0;
}

static int ram_wr(void *ctx, uint64_t lba, const void *buf, uint32_t n)
{
    (void)ctx;
    memcpy(ram_disk + lba * BLOCK_SECTOR, buf, (size_t)n * BLOCK_SECTOR);
    return 0;
}

void block_self_test(void)
{
    ram_disk = kzalloc(16 * BLOCK_SECTOR);
    if (!ram_disk)
        panic("block: ram");
    int dev = blk_register(16, NULL, ram_rd, ram_wr, NULL);
    if (dev < 0)
        panic("block: register");
    uint8_t buf[2 * BLOCK_SECTOR];
    memset(buf, 0xA5, sizeof(buf));
    if (blk_write((uint32_t)dev, 3, buf, 2) != 0)
        panic("block: write");
    if (ram_disk[3 * BLOCK_SECTOR] != 0xA5 || ram_disk[5 * BLOCK_SECTOR - 1] != 0xA5)
        panic("block: write-through");
    uint64_t hits = cache_hits;
    memset(buf, 0, sizeof(buf));
    if (blk_read((uint32_t)dev, 3, buf, 2) != 0 || buf[0] != 0xA5 || buf[sizeof(buf) - 1] != 0xA5)
        panic("block: read");
    if (cache_hits != hits + 2)
        panic("block: cache");
    if (blk_read((uint32_t)dev, 15, buf, 2) != -EINVAL)
        panic("block: bounds");
    pr_info("block", "ramdisk cache ok");
}
