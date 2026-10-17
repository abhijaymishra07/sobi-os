#ifndef SOBI_PMM_H
#define SOBI_PMM_H

#include <sobi/types.h>
#include <sobi/boot.h>

struct pmm_stats {
    uint64_t npages;
    uint64_t free;
    uint64_t used;
    uint64_t usable;
    uint64_t reclaimable;
    uint64_t kernel;
    uint64_t reserved;
    uint64_t bitmap_phys;
    uint64_t bitmap_pages;
    uint64_t regions;
};

void pmm_init(const struct boot_info *info);
uint64_t pmm_reclaim_bootloader(void);
uint64_t pmm_alloc(void);
uint64_t pmm_alloc_contig(uint64_t pages, uint64_t align);
int pmm_free(uint64_t phys);
bool pmm_is_free(uint64_t phys);
uint64_t pmm_type_at(uint64_t phys);
struct pmm_stats pmm_stats(void);

#endif
