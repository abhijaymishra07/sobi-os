#include <sobi/sobifs.h>
#include <sobi/kheap.h>
#include <sobi/log.h>
#include <sobi/panic.h>

#define META_SLOTS 8
#define PATH_COMPS 16
#define SNAP_MAX 8

struct meta_slot {
    uint32_t block;
    uint8_t *data;
    bool used;
    bool dirty;
};

struct ramdisk {
    uint8_t *part[2];
    uint32_t part_bytes;
    uint32_t nparts;
    uint64_t nbytes;
    struct sobifs_dev dev;
};

static struct sobifs_dev *attached;
static struct sobifs_dev *curdev;
static struct sobifs_super sb;
static uint8_t *bmap;
static uint8_t *imap;
static struct meta_slot meta[META_SLOTS];
static bool dirty_sb;
static bool dirty_bmap;
static bool dirty_imap;
static bool mounted;

static int dev_read_block(struct sobifs_dev *d, uint32_t block, void *buf);
static int dev_write_block(struct sobifs_dev *d, uint32_t block, const void *buf);
static void drop_mem(void);
static int meta_get(uint32_t block, struct meta_slot **out, int fresh);
static void meta_forget(uint32_t block);
static int alloc_block(uint32_t *out);
static int free_block(uint32_t block);
static int alloc_inode(uint32_t *out);
static int inode_read(uint32_t ino, struct sobifs_inode *out);
static int inode_write(uint32_t ino, const struct sobifs_inode *in);
static int map_block(struct sobifs_inode *ino, uint32_t index, int alloc, int as_meta, uint32_t *out);
static int read_bytes(const struct sobifs_inode *ino, uint64_t off, void *buf, uint32_t len, int as_meta);
static int write_bytes(struct sobifs_inode *ino, uint64_t off, const void *buf, uint32_t len, int as_meta,
                       uint32_t *done);
static int dir_lookup(uint32_t dirino, const char *name, uint32_t *out);
static int dir_add(uint32_t dirino, const char *name, uint32_t child);
static int dir_clear(uint32_t dirino, const char *name);
static int split_abs(const char *path, char comps[][28], int *out_n);
static int lookup_ino(const char *path, uint32_t *ino, int *is_dir);
static int lookup_parent(const char *path, uint32_t *parent, char *name);
static int replay_apply(uint32_t block, const uint8_t *image, void *arg);
static int ram_read(void *ctx, uint64_t lba, void *buf, uint32_t n);
static int ram_write(void *ctx, uint64_t lba, const void *buf, uint32_t n);
static void scheck(int cond, const char *what);

static int dev_read_block(struct sobifs_dev *d, uint32_t block, void *buf)
{
    if (!d || !d->read || !buf)
        return -EINVAL;
    int rc = d->read(d->ctx, (uint64_t)block * SOBIFS_SECTORS_PER_BLOCK, buf, SOBIFS_SECTORS_PER_BLOCK);
    return rc ? -EIO : 0;
}

static int dev_write_block(struct sobifs_dev *d, uint32_t block, const void *buf)
{
    if (!d || !d->write || !buf)
        return -EINVAL;
    int rc = d->write(d->ctx, (uint64_t)block * SOBIFS_SECTORS_PER_BLOCK, buf, SOBIFS_SECTORS_PER_BLOCK);
    return rc ? -EIO : 0;
}

static void drop_mem(void)
{
    for (int i = 0; i < META_SLOTS; i++) {
        kfree(meta[i].data);
        meta[i].data = NULL;
        meta[i].used = false;
        meta[i].dirty = false;
        meta[i].block = 0;
    }
    kfree(bmap);
    kfree(imap);
    bmap = NULL;
    imap = NULL;
    dirty_sb = false;
    dirty_bmap = false;
    dirty_imap = false;
    mounted = false;
    curdev = NULL;
    memset(&sb, 0, sizeof(sb));
}

static int meta_get(uint32_t block, struct meta_slot **out, int fresh)
{
    for (int i = 0; i < META_SLOTS; i++) {
        if (meta[i].used && meta[i].block == block) {
            *out = &meta[i];
            return 0;
        }
    }
    int slot = -1;
    for (int i = 0; i < META_SLOTS; i++) {
        if (!meta[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return -ENOMEM;
    uint8_t *buf = kmalloc(SOBIFS_BLOCK);
    if (!buf)
        return -ENOMEM;
    if (fresh) {
        memset(buf, 0, SOBIFS_BLOCK);
    } else {
        int rc = dev_read_block(curdev, block, buf);
        if (rc) {
            kfree(buf);
            return rc;
        }
    }
    meta[slot].used = true;
    meta[slot].block = block;
    meta[slot].data = buf;
    meta[slot].dirty = fresh ? true : false;
    *out = &meta[slot];
    return 0;
}

static void meta_forget(uint32_t block)
{
    for (int i = 0; i < META_SLOTS; i++) {
        if (!meta[i].used || meta[i].block != block)
            continue;
        kfree(meta[i].data);
        meta[i].data = NULL;
        meta[i].used = false;
        meta[i].dirty = false;
        meta[i].block = 0;
    }
}

static int btest(const uint8_t *bm, uint32_t bit)
{
    return (bm[bit >> 3] >> (bit & 7)) & 1;
}

static void bset(uint8_t *bm, uint32_t bit, int val)
{
    uint8_t mask = (uint8_t)(1u << (bit & 7));
    if (val)
        bm[bit >> 3] = (uint8_t)(bm[bit >> 3] | mask);
    else
        bm[bit >> 3] = (uint8_t)(bm[bit >> 3] & (uint8_t)~mask);
}

static int alloc_block(uint32_t *out)
{
    if (sb.free_blocks == 0)
        return -ENOSPC;
    uint32_t end = sb.data_start + sb.data_blocks;
    for (uint32_t b = sb.data_start; b < end; b++) {
        if (btest(bmap, b))
            continue;
        bset(bmap, b, 1);
        dirty_bmap = true;
        sb.free_blocks--;
        dirty_sb = true;
        *out = b;
        return 0;
    }
    return -ENOSPC;
}

static int free_block(uint32_t block)
{
    if (block < sb.data_start || block >= sb.data_start + sb.data_blocks)
        return -EINVAL;
    if (!btest(bmap, block))
        return 0;
    bset(bmap, block, 0);
    dirty_bmap = true;
    sb.free_blocks++;
    dirty_sb = true;
    meta_forget(block);
    return 0;
}

static int inode_read(uint32_t ino, struct sobifs_inode *out)
{
    if (ino >= sb.inode_count)
        return -EINVAL;
    uint32_t byte = ino * SOBIFS_INODE_SIZE;
    uint32_t block = sb.inode_start + byte / SOBIFS_BLOCK;
    uint32_t bo = byte % SOBIFS_BLOCK;
    struct meta_slot *slot = NULL;
    int rc = meta_get(block, &slot, 0);
    if (rc)
        return rc;
    memcpy(out, slot->data + bo, sizeof(*out));
    return 0;
}

static int inode_write(uint32_t ino, const struct sobifs_inode *in)
{
    if (ino >= sb.inode_count)
        return -EINVAL;
    uint32_t byte = ino * SOBIFS_INODE_SIZE;
    uint32_t block = sb.inode_start + byte / SOBIFS_BLOCK;
    uint32_t bo = byte % SOBIFS_BLOCK;
    struct meta_slot *slot = NULL;
    int rc = meta_get(block, &slot, 0);
    if (rc)
        return rc;
    memcpy(slot->data + bo, in, sizeof(*in));
    slot->dirty = true;
    return 0;
}

static int alloc_inode(uint32_t *out)
{
    if (sb.free_inodes == 0)
        return -ENOSPC;
    for (uint32_t ino = 2; ino < sb.inode_count; ino++) {
        if (btest(imap, ino))
            continue;
        bset(imap, ino, 1);
        dirty_imap = true;
        sb.free_inodes--;
        dirty_sb = true;
        struct sobifs_inode zero;
        memset(&zero, 0, sizeof(zero));
        int rc = inode_write(ino, &zero);
        if (rc)
            return rc;
        *out = ino;
        return 0;
    }
    return -ENOSPC;
}

static int zero_device(uint32_t block)
{
    uint8_t *buf = kzalloc(SOBIFS_BLOCK);
    if (!buf)
        return -ENOMEM;
    int rc = dev_write_block(curdev, block, buf);
    kfree(buf);
    return rc;
}

static int map_block(struct sobifs_inode *ino, uint32_t index, int alloc, int as_meta, uint32_t *out)
{
    if (index < SOBIFS_DIRECT) {
        if (ino->direct[index] == 0 && alloc) {
            uint32_t block = 0;
            int rc = alloc_block(&block);
            if (rc)
                return rc;
            if (as_meta) {
                struct meta_slot *slot = NULL;
                rc = meta_get(block, &slot, 1);
                if (rc)
                    return rc;
            } else {
                rc = zero_device(block);
                if (rc)
                    return rc;
            }
            ino->direct[index] = block;
        }
        *out = ino->direct[index];
        return 0;
    }
    index -= SOBIFS_DIRECT;
    if (index >= SOBIFS_INDIRECT)
        return -EFBIG;
    if (ino->indirect == 0) {
        if (!alloc) {
            *out = 0;
            return 0;
        }
        uint32_t block = 0;
        int rc = alloc_block(&block);
        if (rc)
            return rc;
        struct meta_slot *slot = NULL;
        rc = meta_get(block, &slot, 1);
        if (rc)
            return rc;
        ino->indirect = block;
    }
    struct meta_slot *tab = NULL;
    int rc = meta_get(ino->indirect, &tab, 0);
    if (rc)
        return rc;
    uint32_t ptr = sobifs_rd32(tab->data + index * 4);
    if (ptr == 0 && alloc) {
        uint32_t block = 0;
        rc = alloc_block(&block);
        if (rc)
            return rc;
        if (as_meta) {
            struct meta_slot *slot = NULL;
            rc = meta_get(block, &slot, 1);
            if (rc)
                return rc;
        } else {
            rc = zero_device(block);
            if (rc)
                return rc;
        }
        sobifs_wr32(tab->data + index * 4, block);
        tab->dirty = true;
        ptr = block;
    }
    *out = ptr;
    return 0;
}

static int read_block_off(uint32_t block, uint32_t bo, void *buf, uint32_t n)
{
    uint8_t *blk = kmalloc(SOBIFS_BLOCK);
    if (!blk)
        return -ENOMEM;
    int rc = dev_read_block(curdev, block, blk);
    if (!rc)
        memcpy(buf, blk + bo, n);
    kfree(blk);
    return rc;
}

static int write_block_off(uint32_t block, uint32_t bo, const void *buf, uint32_t n)
{
    if (bo == 0 && n == SOBIFS_BLOCK)
        return dev_write_block(curdev, block, buf);
    uint8_t *blk = kmalloc(SOBIFS_BLOCK);
    if (!blk)
        return -ENOMEM;
    int rc = dev_read_block(curdev, block, blk);
    if (!rc) {
        memcpy(blk + bo, buf, n);
        rc = dev_write_block(curdev, block, blk);
    }
    kfree(blk);
    return rc;
}

static int read_bytes(const struct sobifs_inode *ino, uint64_t off, void *buf, uint32_t len, int as_meta)
{
    struct sobifs_inode tmp = *ino;
    uint8_t *dst = buf;
    while (len) {
        uint32_t index = (uint32_t)(off / SOBIFS_BLOCK);
        uint32_t bo = (uint32_t)(off % SOBIFS_BLOCK);
        uint32_t n = SOBIFS_BLOCK - bo;
        if (n > len)
            n = len;
        uint32_t block = 0;
        int rc = map_block(&tmp, index, 0, as_meta, &block);
        if (rc)
            return rc;
        if (block == 0) {
            memset(dst, 0, n);
        } else if (as_meta) {
            struct meta_slot *slot = NULL;
            rc = meta_get(block, &slot, 0);
            if (rc)
                return rc;
            memcpy(dst, slot->data + bo, n);
        } else {
            rc = read_block_off(block, bo, dst, n);
            if (rc)
                return rc;
        }
        dst += n;
        off += n;
        len -= n;
    }
    return 0;
}

static int write_bytes(struct sobifs_inode *ino, uint64_t off, const void *buf, uint32_t len, int as_meta,
                       uint32_t *done)
{
    const uint8_t *src = buf;
    *done = 0;
    while (len) {
        uint32_t index = (uint32_t)(off / SOBIFS_BLOCK);
        uint32_t bo = (uint32_t)(off % SOBIFS_BLOCK);
        uint32_t n = SOBIFS_BLOCK - bo;
        if (n > len)
            n = len;
        uint32_t block = 0;
        int rc = map_block(ino, index, 1, as_meta, &block);
        if (rc)
            return rc;
        if (block == 0)
            return -EIO;
        if (as_meta) {
            struct meta_slot *slot = NULL;
            rc = meta_get(block, &slot, 0);
            if (rc)
                return rc;
            memcpy(slot->data + bo, src, n);
            slot->dirty = true;
        } else {
            rc = write_block_off(block, bo, src, n);
            if (rc)
                return rc;
        }
        src += n;
        off += n;
        len -= n;
        *done += n;
    }
    return 0;
}

static int name_eq(const char *fixed, const char *want)
{
    char tmp[28];
    memcpy(tmp, fixed, 27);
    tmp[27] = 0;
    return strcmp(tmp, want) == 0;
}

static int dir_lookup(uint32_t dirino, const char *name, uint32_t *out)
{
    struct sobifs_inode ino;
    int rc = inode_read(dirino, &ino);
    if (rc)
        return rc;
    if ((ino.mode & SOBIFS_IFDIR) == 0)
        return -ENOTDIR;
    uint32_t slots = ino.size / (uint32_t)sizeof(struct sobifs_dirent);
    for (uint32_t i = 0; i < slots; i++) {
        struct sobifs_dirent de;
        rc = read_bytes(&ino, (uint64_t)i * sizeof(de), &de, sizeof(de), 1);
        if (rc)
            return rc;
        if (de.ino && name_eq(de.name, name)) {
            *out = de.ino;
            return 0;
        }
    }
    return -ENOENT;
}

static int dir_add(uint32_t dirino, const char *name, uint32_t child)
{
    size_t nl = strlen(name);
    if (nl == 0 || nl > SOBIFS_NAME_MAX)
        return nl ? -ENAMETOOLONG : -EINVAL;
    struct sobifs_inode ino;
    int rc = inode_read(dirino, &ino);
    if (rc)
        return rc;
    if ((ino.mode & SOBIFS_IFDIR) == 0)
        return -ENOTDIR;
    struct sobifs_dirent de;
    memset(&de, 0, sizeof(de));
    de.ino = child;
    memcpy(de.name, name, nl);
    uint32_t slots = ino.size / (uint32_t)sizeof(de);
    for (uint32_t i = 0; i < slots; i++) {
        struct sobifs_dirent cur;
        rc = read_bytes(&ino, (uint64_t)i * sizeof(de), &cur, sizeof(cur), 1);
        if (rc)
            return rc;
        if (cur.ino)
            continue;
        uint32_t done = 0;
        rc = write_bytes(&ino, (uint64_t)i * sizeof(de), &de, sizeof(de), 1, &done);
        if (rc)
            return rc;
        return inode_write(dirino, &ino);
    }
    uint32_t off = ino.size;
    ino.size += (uint32_t)sizeof(de);
    uint32_t done = 0;
    rc = write_bytes(&ino, off, &de, sizeof(de), 1, &done);
    if (rc)
        return rc;
    return inode_write(dirino, &ino);
}

static int dir_clear(uint32_t dirino, const char *name)
{
    struct sobifs_inode ino;
    int rc = inode_read(dirino, &ino);
    if (rc)
        return rc;
    uint32_t slots = ino.size / (uint32_t)sizeof(struct sobifs_dirent);
    for (uint32_t i = 0; i < slots; i++) {
        struct sobifs_dirent de;
        rc = read_bytes(&ino, (uint64_t)i * sizeof(de), &de, sizeof(de), 1);
        if (rc)
            return rc;
        if (!de.ino || !name_eq(de.name, name))
            continue;
        memset(&de, 0, sizeof(de));
        uint32_t done = 0;
        rc = write_bytes(&ino, (uint64_t)i * sizeof(de), &de, sizeof(de), 1, &done);
        if (rc)
            return rc;
        return inode_write(dirino, &ino);
    }
    return -ENOENT;
}

static int split_abs(const char *path, char comps[][28], int *out_n)
{
    if (!path || path[0] != '/')
        return -EINVAL;
    int n = 0;
    const char *p = path;
    while (*p) {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        const char *start = p;
        while (*p && *p != '/')
            p++;
        size_t len = (size_t)(p - start);
        if (len == 1 && start[0] == '.')
            continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (n > 0)
                n--;
            continue;
        }
        if (len > SOBIFS_NAME_MAX || n >= PATH_COMPS)
            return -ENAMETOOLONG;
        memcpy(comps[n], start, len);
        comps[n][len] = 0;
        n++;
    }
    *out_n = n;
    return 0;
}

static int lookup_ino(const char *path, uint32_t *ino, int *is_dir)
{
    char comps[PATH_COMPS][28];
    int n = 0;
    int rc = split_abs(path, comps, &n);
    if (rc)
        return rc;
    uint32_t cur = sb.root_ino ? sb.root_ino : 1;
    for (int i = 0; i < n; i++) {
        uint32_t next = 0;
        rc = dir_lookup(cur, comps[i], &next);
        if (rc)
            return rc;
        cur = next;
    }
    struct sobifs_inode node;
    rc = inode_read(cur, &node);
    if (rc)
        return rc;
    *ino = cur;
    if (is_dir)
        *is_dir = (node.mode & SOBIFS_IFDIR) ? 1 : 0;
    return 0;
}

static int lookup_parent(const char *path, uint32_t *parent, char *name)
{
    char comps[PATH_COMPS][28];
    int n = 0;
    int rc = split_abs(path, comps, &n);
    if (rc)
        return rc;
    if (n == 0)
        return -EINVAL;
    memcpy(name, comps[n - 1], 28);
    uint32_t cur = sb.root_ino ? sb.root_ino : 1;
    for (int i = 0; i < n - 1; i++) {
        uint32_t next = 0;
        rc = dir_lookup(cur, comps[i], &next);
        if (rc)
            return rc;
        cur = next;
    }
    struct sobifs_inode node;
    rc = inode_read(cur, &node);
    if (rc)
        return rc;
    if ((node.mode & SOBIFS_IFDIR) == 0)
        return -ENOTDIR;
    *parent = cur;
    return 0;
}

static int free_inode_blocks(struct sobifs_inode *ino)
{
    for (uint32_t i = 0; i < SOBIFS_DIRECT; i++) {
        if (!ino->direct[i])
            continue;
        int rc = free_block(ino->direct[i]);
        if (rc)
            return rc;
        ino->direct[i] = 0;
    }
    if (!ino->indirect)
        return 0;
    struct meta_slot *tab = NULL;
    int rc = meta_get(ino->indirect, &tab, 0);
    if (rc)
        return rc;
    for (uint32_t i = 0; i < SOBIFS_INDIRECT; i++) {
        uint32_t block = sobifs_rd32(tab->data + i * 4);
        if (!block)
            continue;
        rc = free_block(block);
        if (rc)
            return rc;
    }
    uint32_t indirect = ino->indirect;
    ino->indirect = 0;
    return free_block(indirect);
}

static int replay_apply(uint32_t block, const uint8_t *image, void *arg)
{
    return dev_write_block(arg, block, image);
}

void sobifs_attach(struct sobifs_dev *dev)
{
    attached = dev;
}

int sobifs_format(struct sobifs_dev *dev)
{
    if (!dev || !dev->read || !dev->write)
        return -EINVAL;
    if (mounted)
        return -EBUSY;
    struct sobifs_layout lay;
    if (sobifs_compute_layout(dev->nsectors, &lay))
        return -EINVAL;
    uint8_t *blk = kzalloc(SOBIFS_BLOCK);
    if (!blk)
        return -ENOMEM;
    for (uint32_t b = 0; b < lay.blocks; b++) {
        if (dev_write_block(dev, b, blk)) {
            kfree(blk);
            return -EIO;
        }
    }
    sobifs_fill_super(blk, &lay);
    if (dev_write_block(dev, 0, blk))
        goto io;
    sobifs_fill_bmap(blk, &lay);
    if (dev_write_block(dev, lay.bmap_block, blk))
        goto io;
    sobifs_fill_imap(blk, &lay);
    if (dev_write_block(dev, lay.imap_block, blk))
        goto io;
    sobifs_fill_inodes(blk, &lay);
    if (dev_write_block(dev, lay.inode_start, blk))
        goto io;
    sobifs_fill_rootdir(blk);
    if (dev_write_block(dev, lay.data_start, blk))
        goto io;
    kfree(blk);
    return 0;
io:
    kfree(blk);
    return -EIO;
}

int sobifs_replay(struct sobifs_dev *dev)
{
    if (!dev || !dev->read || !dev->write)
        return -EINVAL;
    struct sobifs_super *image = kmalloc(sizeof(*image));
    if (!image)
        return -ENOMEM;
    int rc = dev_read_block(dev, 0, image);
    if (rc) {
        kfree(image);
        return rc;
    }
    if (image->magic != SOBIFS_MAGIC || image->version != SOBIFS_VERSION || image->block_size != SOBIFS_BLOCK) {
        kfree(image);
        return -EIO;
    }
    if (image->checksum != sobifs_super_checksum(image)) {
        kfree(image);
        return -EIO;
    }
    if (image->journal_len == 0) {
        kfree(image);
        pr_info("sobifs", "replay empty");
        return 0;
    }
    if (image->journal_len > image->journal_blocks * SOBIFS_BLOCK || (image->journal_len % SOBIFS_SECTOR) != 0) {
        kfree(image);
        return -EIO;
    }
    uint8_t *jbuf = kmalloc(image->journal_blocks * SOBIFS_BLOCK);
    if (!jbuf) {
        kfree(image);
        return -ENOMEM;
    }
    for (uint32_t b = 0; b < image->journal_blocks; b++) {
        rc = dev_read_block(dev, image->journal_start + b, jbuf + b * SOBIFS_BLOCK);
        if (rc) {
            kfree(jbuf);
            kfree(image);
            return rc;
        }
    }
    int st = sobifs_journal_inspect(jbuf, image->journal_len, image->block_count, 0, NULL, NULL);
    if (st < 0) {
        kfree(jbuf);
        kfree(image);
        return -EIO;
    }
    if (st == 1) {
        pr_info("sobifs", "replay dropped torn journal");
        kfree(jbuf);
        kfree(image);
        return 1;
    }
    st = sobifs_journal_inspect(jbuf, image->journal_len, image->block_count, 1, replay_apply, dev);
    kfree(jbuf);
    kfree(image);
    if (st != 0)
        return -EIO;
    pr_info("sobifs", "replay applied");
    return 0;
}

int sobifs_mount(struct sobifs_dev *dev)
{
    if (!dev)
        dev = attached;
    if (!dev || !dev->read || !dev->write)
        return -EINVAL;
    drop_mem();
    int rc = sobifs_replay(dev);
    if (rc < 0)
        return rc;
    bmap = kmalloc(SOBIFS_BLOCK);
    imap = kmalloc(SOBIFS_BLOCK);
    if (!bmap || !imap) {
        drop_mem();
        return -ENOMEM;
    }
    curdev = dev;
    rc = dev_read_block(dev, 0, &sb);
    if (rc || sb.magic != SOBIFS_MAGIC || sb.checksum != sobifs_super_checksum(&sb)) {
        drop_mem();
        return rc ? rc : -EIO;
    }
    rc = dev_read_block(dev, sb.bmap_block, bmap);
    if (rc) {
        drop_mem();
        return rc;
    }
    rc = dev_read_block(dev, sb.imap_block, imap);
    if (rc) {
        drop_mem();
        return rc;
    }
    dirty_sb = false;
    dirty_bmap = false;
    dirty_imap = false;
    mounted = true;
    return 0;
}

int sobifs_create(const char *path)
{
    if (!mounted)
        return -EINVAL;
    char name[28];
    uint32_t parent = 0;
    int rc = lookup_parent(path, &parent, name);
    if (rc)
        return rc;
    uint32_t exist = 0;
    if (dir_lookup(parent, name, &exist) == 0)
        return -EEXIST;
    uint32_t ino = 0;
    rc = alloc_inode(&ino);
    if (rc)
        return rc;
    struct sobifs_inode node;
    memset(&node, 0, sizeof(node));
    node.mode = SOBIFS_IFREG | 0644u;
    node.nlink = 1;
    rc = inode_write(ino, &node);
    if (rc)
        return rc;
    return dir_add(parent, name, ino);
}

int sobifs_mkdir(const char *path)
{
    if (!mounted)
        return -EINVAL;
    char name[28];
    uint32_t parent = 0;
    int rc = lookup_parent(path, &parent, name);
    if (rc)
        return rc;
    uint32_t exist = 0;
    if (dir_lookup(parent, name, &exist) == 0)
        return -EEXIST;
    uint32_t ino = 0;
    rc = alloc_inode(&ino);
    if (rc)
        return rc;
    uint32_t block = 0;
    rc = alloc_block(&block);
    if (rc)
        return rc;
    struct meta_slot *slot = NULL;
    rc = meta_get(block, &slot, 1);
    if (rc)
        return rc;
    struct sobifs_dirent de;
    memset(&de, 0, sizeof(de));
    de.ino = ino;
    de.name[0] = '.';
    memcpy(slot->data, &de, sizeof(de));
    de.ino = parent;
    de.name[1] = '.';
    memcpy(slot->data + sizeof(de), &de, sizeof(de));
    slot->dirty = true;
    struct sobifs_inode node;
    memset(&node, 0, sizeof(node));
    node.mode = SOBIFS_IFDIR | 0755u;
    node.size = 64;
    node.direct[0] = block;
    node.nlink = 2;
    rc = inode_write(ino, &node);
    if (rc)
        return rc;
    return dir_add(parent, name, ino);
}

int sobifs_unlink(const char *path)
{
    if (!mounted)
        return -EINVAL;
    char name[28];
    uint32_t parent = 0;
    int rc = lookup_parent(path, &parent, name);
    if (rc)
        return rc;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return -EINVAL;
    uint32_t ino = 0;
    rc = dir_lookup(parent, name, &ino);
    if (rc)
        return rc;
    if (ino < 2)
        return -EBUSY;
    struct sobifs_inode node;
    rc = inode_read(ino, &node);
    if (rc)
        return rc;
    if (node.mode & SOBIFS_IFDIR) {
        uint32_t slots = node.size / (uint32_t)sizeof(struct sobifs_dirent);
        for (uint32_t i = 0; i < slots; i++) {
            struct sobifs_dirent de;
            rc = read_bytes(&node, (uint64_t)i * sizeof(de), &de, sizeof(de), 1);
            if (rc)
                return rc;
            if (!de.ino)
                continue;
            if (name_eq(de.name, ".") || name_eq(de.name, ".."))
                continue;
            return -ENOTEMPTY;
        }
    }
    rc = free_inode_blocks(&node);
    if (rc)
        return rc;
    memset(&node, 0, sizeof(node));
    rc = inode_write(ino, &node);
    if (rc)
        return rc;
    bset(imap, ino, 0);
    dirty_imap = true;
    sb.free_inodes++;
    dirty_sb = true;
    return dir_clear(parent, name);
}

int sobifs_read(const char *path, uint64_t off, void *buf, uint32_t len)
{
    if (!mounted)
        return -EINVAL;
    if (len && !buf)
        return -EINVAL;
    uint32_t ino = 0;
    int is_dir = 0;
    int rc = lookup_ino(path, &ino, &is_dir);
    if (rc)
        return rc;
    if (is_dir)
        return -EISDIR;
    struct sobifs_inode node;
    rc = inode_read(ino, &node);
    if (rc)
        return rc;
    if (off >= node.size)
        return 0;
    uint64_t remain = node.size - off;
    if ((uint64_t)len > remain)
        len = (uint32_t)remain;
    rc = read_bytes(&node, off, buf, len, 0);
    if (rc)
        return rc;
    return (int)len;
}

int sobifs_write(const char *path, uint64_t off, const void *buf, uint32_t len)
{
    if (!mounted)
        return -EINVAL;
    if (len && !buf)
        return -EINVAL;
    if (len > (16u << 20))
        return -EFBIG;
    uint32_t ino = 0;
    int is_dir = 0;
    int rc = lookup_ino(path, &ino, &is_dir);
    if (rc)
        return rc;
    if (is_dir)
        return -EISDIR;
    struct sobifs_inode node;
    rc = inode_read(ino, &node);
    if (rc)
        return rc;
    if (off > node.size)
        return -EINVAL;
    if (off + len < off || off + len > UINT32_MAX)
        return -EFBIG;
    uint32_t done = 0;
    rc = write_bytes(&node, off, buf, len, 0, &done);
    if (off + done > node.size)
        node.size = (uint32_t)(off + done);
    int saved = inode_write(ino, &node);
    if (rc)
        return done ? (int)done : rc;
    if (saved)
        return saved;
    return (int)done;
}

int sobifs_sync(void)
{
    if (!mounted)
        return -EINVAL;
    bool any = dirty_sb || dirty_bmap || dirty_imap;
    for (int i = 0; i < META_SLOTS; i++) {
        if (meta[i].used && meta[i].dirty)
            any = true;
    }
    if (!any)
        return 0;
    int n = 1;
    if (dirty_bmap)
        n++;
    if (dirty_imap)
        n++;
    for (int i = 0; i < META_SLOTS; i++) {
        if (meta[i].used && meta[i].dirty)
            n++;
    }
    if (n > 7)
        return -ENOSPC;
    uint32_t raw = (uint32_t)n * SOBIFS_JREC_DATA + SOBIFS_JREC_COMMIT;
    uint32_t padded = (raw + (SOBIFS_SECTOR - 1)) & ~(SOBIFS_SECTOR - 1);
    if (padded > sb.journal_blocks * SOBIFS_BLOCK)
        return -ENOSPC;
    sb.journal_len = padded;
    sb.txid++;
    sb.checksum = sobifs_super_checksum(&sb);
    dirty_sb = true;
    uint32_t blocks[SNAP_MAX];
    const uint8_t *datas[SNAP_MAX];
    uint8_t *super_image = kmalloc(SOBIFS_BLOCK);
    uint8_t *jbuf = kzalloc(sb.journal_blocks * SOBIFS_BLOCK);
    if (!super_image || !jbuf) {
        kfree(super_image);
        kfree(jbuf);
        return -ENOMEM;
    }
    memcpy(super_image, &sb, sizeof(sb));
    int count = 0;
    blocks[count] = 0;
    datas[count] = super_image;
    count++;
    if (dirty_bmap) {
        blocks[count] = sb.bmap_block;
        datas[count] = bmap;
        count++;
    }
    if (dirty_imap) {
        blocks[count] = sb.imap_block;
        datas[count] = imap;
        count++;
    }
    for (int i = 0; i < META_SLOTS; i++) {
        if (!meta[i].used || !meta[i].dirty)
            continue;
        blocks[count] = meta[i].block;
        datas[count] = meta[i].data;
        count++;
    }
    uint32_t off = 0;
    for (int i = 0; i < count; i++) {
        sobifs_wr32(jbuf + off + 0, SOBIFS_JNL_MAGIC);
        sobifs_wr32(jbuf + off + 4, SOBIFS_J_DATA);
        sobifs_wr32(jbuf + off + 8, sb.txid);
        sobifs_wr32(jbuf + off + 12, (uint32_t)i);
        sobifs_wr32(jbuf + off + 16, blocks[i]);
        sobifs_wr32(jbuf + off + 20, sobifs_sum_words(datas[i], SOBIFS_BLOCK));
        memcpy(jbuf + off + 24, datas[i], SOBIFS_BLOCK);
        off += SOBIFS_JREC_DATA;
    }
    sobifs_wr32(jbuf + off + 0, SOBIFS_JNL_MAGIC);
    sobifs_wr32(jbuf + off + 4, SOBIFS_J_COMMIT);
    sobifs_wr32(jbuf + off + 8, sb.txid);
    sobifs_wr32(jbuf + off + 12, (uint32_t)count);
    sobifs_wr32(jbuf + off + 16, 0);
    sobifs_wr32(jbuf + off + 20, padded);
    sobifs_wr32(jbuf + off + 16, sobifs_sum_skip(jbuf, padded, off + 16));
    int rc = 0;
    for (uint32_t b = 0; b < sb.journal_blocks; b++) {
        rc = dev_write_block(curdev, sb.journal_start + b, jbuf + b * SOBIFS_BLOCK);
        if (rc)
            break;
    }
    if (!rc) {
        for (int i = 0; i < count; i++) {
            rc = dev_write_block(curdev, blocks[i], datas[i]);
            if (rc)
                break;
        }
    }
    kfree(super_image);
    kfree(jbuf);
    if (rc)
        return rc;
    dirty_sb = false;
    dirty_bmap = false;
    dirty_imap = false;
    for (int i = 0; i < META_SLOTS; i++)
        meta[i].dirty = false;
    return 0;
}

static int ram_copy_out(struct ramdisk *disk, uint64_t off, void *buf, uint64_t len, int wr, const void *src)
{
    if (off + len > disk->nbytes)
        return -EIO;
    uint8_t *dst = buf;
    const uint8_t *from = src;
    while (len) {
        uint32_t pi = (uint32_t)(off / disk->part_bytes);
        uint32_t po = (uint32_t)(off % disk->part_bytes);
        uint32_t chunk = disk->part_bytes - po;
        if ((uint64_t)chunk > len)
            chunk = (uint32_t)len;
        if (pi >= disk->nparts)
            return -EIO;
        if (wr)
            memcpy(disk->part[pi] + po, from, chunk);
        else {
            memcpy(dst, disk->part[pi] + po, chunk);
            dst += chunk;
        }
        if (from)
            from += chunk;
        off += chunk;
        len -= chunk;
    }
    return 0;
}

static int ram_read(void *ctx, uint64_t lba, void *buf, uint32_t n)
{
    struct ramdisk *disk = ctx;
    return ram_copy_out(disk, lba * SOBIFS_SECTOR, buf, (uint64_t)n * SOBIFS_SECTOR, 0, NULL);
}

static int ram_write(void *ctx, uint64_t lba, const void *buf, uint32_t n)
{
    struct ramdisk *disk = ctx;
    return ram_copy_out(disk, lba * SOBIFS_SECTOR, NULL, (uint64_t)n * SOBIFS_SECTOR, 1, buf);
}

static int ram_init(struct ramdisk *disk, uint32_t blocks)
{
    memset(disk, 0, sizeof(*disk));
    disk->part_bytes = 65536;
    disk->nbytes = (uint64_t)blocks * SOBIFS_BLOCK;
    disk->nparts = (uint32_t)((disk->nbytes + disk->part_bytes - 1) / disk->part_bytes);
    if (disk->nparts > 2)
        return -ENOMEM;
    for (uint32_t i = 0; i < disk->nparts; i++) {
        disk->part[i] = kzalloc(disk->part_bytes);
        if (!disk->part[i])
            return -ENOMEM;
    }
    disk->dev.read = ram_read;
    disk->dev.write = ram_write;
    disk->dev.ctx = disk;
    disk->dev.nsectors = disk->nbytes / SOBIFS_SECTOR;
    return 0;
}

static int ram_clone(struct ramdisk *dst, const struct ramdisk *src)
{
    int rc = ram_init(dst, (uint32_t)(src->nbytes / SOBIFS_BLOCK));
    if (rc)
        return rc;
    for (uint32_t i = 0; i < src->nparts; i++)
        memcpy(dst->part[i], src->part[i], src->part_bytes);
    return 0;
}

static void ram_xor(struct ramdisk *disk, uint64_t off)
{
    uint32_t pi = (uint32_t)(off / disk->part_bytes);
    uint32_t po = (uint32_t)(off % disk->part_bytes);
    disk->part[pi][po] ^= 0xff;
}

static void scheck(int cond, const char *what)
{
    if (!cond)
        panic("sobifs: %s", what);
}

void sobifs_self_test(void)
{
    struct ramdisk disk_a;
    struct ramdisk disk_b;
    scheck(ram_init(&disk_a, 32) == 0, "ram");
    sobifs_attach(&disk_a.dev);
    scheck(sobifs_format(&disk_a.dev) == 0, "format");
    scheck(sobifs_mount(&disk_a.dev) == 0, "mount");
    scheck(sobifs_mkdir("/dir") == 0, "mkdir");
    scheck(sobifs_create("/hello") == 0, "create");
    scheck(sobifs_create("/dir/a") == 0, "create nested");
    scheck(sobifs_write("/hello", 0, "persist", 7) == 7, "write");
    scheck(sobifs_write("/dir/a", 0, "xyz", 3) == 3, "write nested");
    scheck(sobifs_read("/missing", 0, NULL, 0) == -ENOENT, "missing");
    scheck(sobifs_sync() == 0, "sync");
    scheck(ram_clone(&disk_b, &disk_a) == 0, "clone");
    scheck(sobifs_replay(&disk_b.dev) == 0, "clean replay");
    scheck(sobifs_mount(&disk_b.dev) == 0, "remount");
    char buf[16];
    memset(buf, 0, sizeof(buf));
    scheck(sobifs_read("/hello", 0, buf, sizeof(buf)) == 7, "read len");
    scheck(memcmp(buf, "persist", 7) == 0, "read bytes");
    memset(buf, 0, sizeof(buf));
    scheck(sobifs_read("/dir/a", 0, buf, sizeof(buf)) == 3, "nested len");
    scheck(memcmp(buf, "xyz", 3) == 0, "nested bytes");
    scheck(sobifs_read("/hello", 100, buf, 4) == 0, "eof");
    pr_info("test", "sobifs: persist ok");

    struct sobifs_super *ondisk = kmalloc(sizeof(*ondisk));
    scheck(ondisk != NULL, "super buf");
    scheck(ram_read(&disk_b, 0, ondisk, SOBIFS_SECTORS_PER_BLOCK) == 0, "read super");
    scheck(ondisk->journal_len >= SOBIFS_SECTOR, "journal len");
    uint64_t poke = (uint64_t)ondisk->journal_start * SOBIFS_BLOCK + ondisk->journal_len - 1;
    kfree(ondisk);
    ram_xor(&disk_b, poke);
    scheck(sobifs_replay(&disk_b.dev) == 1, "tear detected");
    scheck(sobifs_mount(&disk_b.dev) == 0, "mount torn");
    memset(buf, 0, sizeof(buf));
    scheck(sobifs_read("/hello", 0, buf, sizeof(buf)) == 7, "tear read");
    scheck(memcmp(buf, "persist", 7) == 0, "tear bytes");
    scheck(sobifs_unlink("/dir/a") == 0, "unlink");
    scheck(sobifs_read("/dir/a", 0, buf, 4) == -ENOENT, "unlinked");
    pr_info("test", "sobifs: replay ok");

    scheck(sobifs_create("/big") == 0, "big create");
    uint8_t *chunk = kmalloc(SOBIFS_BLOCK);
    scheck(chunk != NULL, "chunk");
    memset(chunk, 0x5a, SOBIFS_BLOCK);
    uint64_t woff = 0;
    int saw_full = 0;
    for (;;) {
        int wr = sobifs_write("/big", woff, chunk, SOBIFS_BLOCK);
        if (wr < 0) {
            if (wr == -ENOSPC)
                saw_full = 1;
            break;
        }
        if (wr == 0)
            break;
        woff += (uint32_t)wr;
        if (woff > (1024u * 1024u))
            break;
    }
    kfree(chunk);
    scheck(saw_full, "disk full");
    pr_info("test", "sobifs: full ok");
    pr_info("test", "sobifs: all checks passed");
}
