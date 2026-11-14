#ifndef SOBI_BLOCK_H
#define SOBI_BLOCK_H

#include <sobi/types.h>

#ifndef EIO
#define EIO 5
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
#ifndef EROFS
#define EROFS 30
#endif

#define BLOCK_SECTOR 512u
#define BLOCK_CACHE_SLOTS 64

typedef int (*blk_read_fn)(void *ctx, uint64_t lba, void *buf, uint32_t nsectors);
typedef int (*blk_write_fn)(void *ctx, uint64_t lba, const void *buf, uint32_t nsectors);
typedef int (*blk_flush_fn)(void *ctx);

extern uint64_t cache_hits;
extern uint64_t cache_misses;

int blk_register(uint64_t capacity, void *ctx, blk_read_fn read, blk_write_fn write, blk_flush_fn flush);
int blk_read(uint32_t dev, uint64_t lba, void *buf, uint32_t nsectors);
int blk_write(uint32_t dev, uint64_t lba, const void *buf, uint32_t nsectors);
int blk_sync(uint32_t dev);
int block_init(void);
void block_self_test(void);

#endif
