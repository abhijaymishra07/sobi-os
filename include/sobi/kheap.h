#ifndef SOBI_KHEAP_H
#define SOBI_KHEAP_H

#include <sobi/types.h>

struct kheap_stats {
    uint64_t live_bytes;
    uint64_t allocs;
    uint64_t frees;
    uint64_t slabs;
    uint64_t big_live;
};

void kheap_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void *krealloc(void *ptr, size_t size);
void kfree(void *ptr);
struct kheap_stats kheap_stats(void);

#endif
