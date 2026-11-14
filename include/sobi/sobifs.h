#ifndef SOBI_SOBIFS_H
#define SOBI_SOBIFS_H

#include <sobi/vfs.h>
#include <sobi/string.h>

#define SOBIFS_MAGIC       0x49424F53u
#define SOBIFS_VERSION     1u
#define SOBIFS_BLOCK       4096u
#define SOBIFS_SECTOR      512u
#define SOBIFS_SECTORS_PER_BLOCK 8u
#define SOBIFS_INODE_SIZE  64u
#define SOBIFS_NAME_MAX    27u
#define SOBIFS_DIRECT      8u
#define SOBIFS_INDIRECT    1024u
#define SOBIFS_JNL_MAGIC   0x4C4E524Au
#define SOBIFS_J_DATA      1u
#define SOBIFS_J_COMMIT    2u
#define SOBIFS_JREC_DATA   4120u
#define SOBIFS_JREC_COMMIT 24u
#define SOBIFS_IFREG       0x8000u
#define SOBIFS_IFDIR       0x4000u

struct sobifs_super {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t inode_count;
    uint32_t journal_start;
    uint32_t journal_blocks;
    uint32_t journal_len;
    uint32_t bmap_block;
    uint32_t imap_block;
    uint32_t inode_start;
    uint32_t inode_blocks;
    uint32_t data_start;
    uint32_t data_blocks;
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint32_t root_ino;
    uint32_t txid;
    uint32_t checksum;
    uint8_t reserved[4096 - 76];
};

struct sobifs_inode {
    uint32_t mode;
    uint32_t size;
    uint32_t direct[SOBIFS_DIRECT];
    uint32_t indirect;
    uint32_t nlink;
    uint32_t reserved[4];
};

struct sobifs_dirent {
    uint32_t ino;
    char name[28];
};

struct sobifs_layout {
    uint32_t blocks;
    uint32_t inode_count;
    uint32_t journal_start;
    uint32_t journal_blocks;
    uint32_t bmap_block;
    uint32_t imap_block;
    uint32_t inode_start;
    uint32_t inode_blocks;
    uint32_t data_start;
    uint32_t data_blocks;
};

struct sobifs_dev {
    int (*read)(void *ctx, uint64_t lba, void *buf, uint32_t n);
    int (*write)(void *ctx, uint64_t lba, const void *buf, uint32_t n);
    void *ctx;
    uint64_t nsectors;
};

_Static_assert(sizeof(struct sobifs_super) == 4096, "sobifs superblock");
_Static_assert(offsetof(struct sobifs_super, checksum) == 72, "sobifs checksum offset");
_Static_assert(sizeof(struct sobifs_inode) == 64, "sobifs inode");
_Static_assert(sizeof(struct sobifs_dirent) == 32, "sobifs dirent");
_Static_assert(SOBIFS_JREC_DATA == 24 + 4096, "sobifs journal data record");

static inline __attribute__((unused)) uint32_t sobifs_rd32(const void *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static inline __attribute__((unused)) void sobifs_wr32(void *p, uint32_t v)
{
    memcpy(p, &v, 4);
}

static inline __attribute__((unused)) uint32_t sobifs_sum_words(const void *p, uint32_t len)
{
    const uint8_t *b = p;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; i += 4)
        sum += sobifs_rd32(b + i);
    return sum;
}

static inline __attribute__((unused)) uint32_t sobifs_sum_skip(const void *p, uint32_t len, uint32_t skip)
{
    const uint8_t *b = p;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; i += 4) {
        if (i == skip)
            continue;
        sum += sobifs_rd32(b + i);
    }
    return sum;
}

static inline __attribute__((unused)) uint32_t sobifs_super_checksum(const struct sobifs_super *sb)
{
    return sobifs_sum_skip(sb, SOBIFS_BLOCK, (uint32_t)offsetof(struct sobifs_super, checksum));
}

static inline __attribute__((unused)) int sobifs_compute_layout(uint64_t nsectors, struct sobifs_layout *lay)
{
    if (!lay || (nsectors % SOBIFS_SECTORS_PER_BLOCK) != 0)
        return -1;
    uint64_t blocks64 = nsectors / SOBIFS_SECTORS_PER_BLOCK;
    if (blocks64 < 16 || blocks64 > 32768)
        return -1;
    memset(lay, 0, sizeof(*lay));
    lay->blocks = (uint32_t)blocks64;
    lay->journal_blocks = 8;
    lay->inode_count = 64;
    lay->inode_blocks = 1;
    lay->bmap_block = 1;
    lay->imap_block = 2;
    lay->inode_start = 3;
    lay->data_start = 4;
    lay->journal_start = lay->blocks - lay->journal_blocks;
    if (lay->journal_start <= lay->data_start)
        return -1;
    lay->data_blocks = lay->journal_start - lay->data_start;
    if (lay->data_blocks < 2)
        return -1;
    return 0;
}

static inline __attribute__((unused)) void sobifs_mark_bit(uint8_t *bm, uint32_t bit)
{
    bm[bit >> 3] = (uint8_t)(bm[bit >> 3] | (uint8_t)(1u << (bit & 7)));
}

static inline __attribute__((unused)) void sobifs_fill_super(uint8_t *block, const struct sobifs_layout *lay)
{
    memset(block, 0, SOBIFS_BLOCK);
    sobifs_wr32(block + 0, SOBIFS_MAGIC);
    sobifs_wr32(block + 4, SOBIFS_VERSION);
    sobifs_wr32(block + 8, SOBIFS_BLOCK);
    sobifs_wr32(block + 12, lay->blocks);
    sobifs_wr32(block + 16, lay->inode_count);
    sobifs_wr32(block + 20, lay->journal_start);
    sobifs_wr32(block + 24, lay->journal_blocks);
    sobifs_wr32(block + 28, 0);
    sobifs_wr32(block + 32, lay->bmap_block);
    sobifs_wr32(block + 36, lay->imap_block);
    sobifs_wr32(block + 40, lay->inode_start);
    sobifs_wr32(block + 44, lay->inode_blocks);
    sobifs_wr32(block + 48, lay->data_start);
    sobifs_wr32(block + 52, lay->data_blocks);
    sobifs_wr32(block + 56, lay->data_blocks - 1);
    sobifs_wr32(block + 60, lay->inode_count - 2);
    sobifs_wr32(block + 64, 1);
    sobifs_wr32(block + 68, 0);
    sobifs_wr32(block + 72, sobifs_sum_skip(block, SOBIFS_BLOCK, 72));
}

static inline __attribute__((unused)) void sobifs_fill_bmap(uint8_t *block, const struct sobifs_layout *lay)
{
    memset(block, 0, SOBIFS_BLOCK);
    for (uint32_t b = 0; b < lay->data_start; b++)
        sobifs_mark_bit(block, b);
    sobifs_mark_bit(block, lay->data_start);
    for (uint32_t b = lay->journal_start; b < lay->blocks; b++)
        sobifs_mark_bit(block, b);
}

static inline __attribute__((unused)) void sobifs_fill_imap(uint8_t *block, const struct sobifs_layout *lay)
{
    (void)lay;
    memset(block, 0, SOBIFS_BLOCK);
    sobifs_mark_bit(block, 0);
    sobifs_mark_bit(block, 1);
}

static inline __attribute__((unused)) void sobifs_fill_inodes(uint8_t *block, const struct sobifs_layout *lay)
{
    memset(block, 0, SOBIFS_BLOCK);
    struct sobifs_inode ino;
    memset(&ino, 0, sizeof(ino));
    ino.mode = SOBIFS_IFDIR | 0755u;
    ino.size = 64;
    ino.direct[0] = lay->data_start;
    ino.nlink = 2;
    memcpy(block + SOBIFS_INODE_SIZE, &ino, sizeof(ino));
}

static inline __attribute__((unused)) void sobifs_fill_rootdir(uint8_t *block)
{
    struct sobifs_dirent de;
    memset(block, 0, SOBIFS_BLOCK);
    memset(&de, 0, sizeof(de));
    de.ino = 1;
    de.name[0] = '.';
    memcpy(block, &de, sizeof(de));
    de.name[1] = '.';
    memcpy(block + sizeof(de), &de, sizeof(de));
}

static inline __attribute__((unused)) int sobifs_journal_inspect(const uint8_t *buf, uint32_t len,
                                                                 uint32_t block_count, int apply,
                                                                 int (*cb)(uint32_t block, const uint8_t *image, void *arg),
                                                                 void *arg)
{
    if (!buf && len)
        return -1;
    if (len == 0)
        return 0;
    if ((len % SOBIFS_SECTOR) != 0 || (len % 4) != 0)
        return 1;
    uint32_t off = 0;
    uint32_t nrecs = 0;
    uint32_t txid = 0;
    while (off + SOBIFS_JREC_DATA + SOBIFS_JREC_COMMIT <= len) {
        if (sobifs_rd32(buf + off) != SOBIFS_JNL_MAGIC)
            break;
        if (sobifs_rd32(buf + off + 4) != SOBIFS_J_DATA)
            break;
        uint32_t rec_tx = sobifs_rd32(buf + off + 8);
        uint32_t seq = sobifs_rd32(buf + off + 12);
        uint32_t block = sobifs_rd32(buf + off + 16);
        uint32_t csum = sobifs_rd32(buf + off + 20);
        if (seq != nrecs || (nrecs && rec_tx != txid))
            return 1;
        if (block_count && block >= block_count)
            return 1;
        if (csum != sobifs_sum_words(buf + off + 24, SOBIFS_BLOCK))
            return 1;
        if (block == 0) {
            const uint8_t *image = buf + off + 24;
            if (sobifs_rd32(image) != SOBIFS_MAGIC || sobifs_rd32(image + 68) != rec_tx)
                return 1;
            if (sobifs_rd32(image + 72) != sobifs_sum_skip(image, SOBIFS_BLOCK, 72))
                return 1;
        }
        txid = rec_tx;
        nrecs++;
        off += SOBIFS_JREC_DATA;
    }
    if (off + SOBIFS_JREC_COMMIT > len)
        return 1;
    if (sobifs_rd32(buf + off) != SOBIFS_JNL_MAGIC)
        return 1;
    if (sobifs_rd32(buf + off + 4) != SOBIFS_J_COMMIT)
        return 1;
    if (sobifs_rd32(buf + off + 8) != txid && nrecs)
        return 1;
    if (sobifs_rd32(buf + off + 12) != nrecs)
        return 1;
    if (sobifs_rd32(buf + off + 20) != len)
        return 1;
    uint32_t stored = sobifs_rd32(buf + off + 16);
    if (stored != sobifs_sum_skip(buf, len, off + 16))
        return 1;
    if (apply && cb) {
        for (uint32_t i = 0; i < nrecs; i++) {
            uint32_t rec = i * SOBIFS_JREC_DATA;
            uint32_t block = sobifs_rd32(buf + rec + 16);
            int rc = cb(block, buf + rec + 24, arg);
            if (rc)
                return -1;
        }
    }
    return 0;
}

void sobifs_attach(struct sobifs_dev *dev);
int sobifs_format(struct sobifs_dev *dev);
int sobifs_mount(struct sobifs_dev *dev);
int sobifs_create(const char *path);
int sobifs_read(const char *path, uint64_t off, void *buf, uint32_t len);
int sobifs_write(const char *path, uint64_t off, const void *buf, uint32_t len);
int sobifs_unlink(const char *path);
int sobifs_mkdir(const char *path);
int sobifs_sync(void);
int sobifs_replay(struct sobifs_dev *dev);
void sobifs_self_test(void);

#endif
