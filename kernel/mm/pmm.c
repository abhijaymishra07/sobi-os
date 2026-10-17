#include <sobi/pmm.h>
#include <sobi/boot.h>
#include <sobi/string.h>
#include <sobi/log.h>
#include <sobi/panic.h>
#include <limine.h>

#define PMM_MAX_REGIONS 256

struct pmm_region {
    uint64_t base;
    uint64_t length;
    uint64_t type;
};

static struct pmm_region regions[PMM_MAX_REGIONS];
static uint64_t nregions;
static uint64_t hhdm;
static uint64_t npages;
static uint64_t free_count;
static uint64_t scan;
static uint64_t bitmap_phys;
static uint64_t bitmap_pages;
static uint8_t *bitmap;

static int pmm_error(const char *what)
{
#ifdef SOBI_BUILD_RELEASE
    pr_err("pmm", "%s", what);
    return -1;
#else
    panic("pmm: %s", what);
#endif
}

static bool bit_used(uint64_t page)
{
    return (bitmap[page >> 3] & (uint8_t)(1u << (page & 7))) != 0;
}

static void bit_mark_used(uint64_t page)
{
    bitmap[page >> 3] |= (uint8_t)(1u << (page & 7));
}

static void bit_mark_free(uint64_t page)
{
    bitmap[page >> 3] &= (uint8_t)~(1u << (page & 7));
}

static bool allocatable(uint64_t phys)
{
    if (phys == 0)
        return false;
    if (phys >= bitmap_phys && phys < bitmap_phys + bitmap_pages * PAGE_SIZE)
        return false;
    return pmm_type_at(phys) == LIMINE_MEMMAP_USABLE;
}

uint64_t pmm_type_at(uint64_t phys)
{
    for (uint64_t i = 0; i < nregions; i++) {
        if (phys >= regions[i].base && phys < regions[i].base + regions[i].length)
            return regions[i].type;
    }
    return UINT64_MAX;
}

static void take(uint64_t page)
{
    bit_mark_used(page);
    free_count--;
}

static uint64_t claim_run(uint64_t start, uint64_t pages)
{
    for (uint64_t i = 0; i < pages; i++)
        take(start + i);
    if (scan == start)
        scan = start + pages;
    return start << PAGE_SHIFT;
}

void pmm_init(const struct boot_info *info)
{
    boot_own_firmware_data();
    info = boot_info;
    const struct limine_memmap_response *map = info->memmap;
    if (map->entry_count > PMM_MAX_REGIONS)
        panic("pmm: %lu memmap entries exceeds %d", map->entry_count, PMM_MAX_REGIONS);

    hhdm = info->hhdm_offset;
    nregions = map->entry_count;
    uint64_t max_end = 0;
    for (uint64_t i = 0; i < nregions; i++) {
        regions[i].base = map->entries[i]->base;
        regions[i].length = map->entries[i]->length;
        regions[i].type = map->entries[i]->type;
        uint64_t type = regions[i].type;
        if (type == LIMINE_MEMMAP_USABLE ||
            type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
            type == LIMINE_MEMMAP_EXECUTABLE_AND_MODULES) {
            uint64_t end = regions[i].base + regions[i].length;
            if (end > max_end)
                max_end = end;
        }
    }

    npages = max_end >> PAGE_SHIFT;
    uint64_t bitmap_bytes = ALIGN_UP((npages + 7) / 8, PAGE_SIZE);
    bitmap_pages = bitmap_bytes / PAGE_SIZE;

    bitmap_phys = 0;
    for (uint64_t i = 0; i < nregions; i++) {
        if (regions[i].type != LIMINE_MEMMAP_USABLE)
            continue;
        if (regions[i].length >= bitmap_bytes && IS_ALIGNED(regions[i].base, PAGE_SIZE)) {
            bitmap_phys = regions[i].base;
            break;
        }
    }
    if (bitmap_phys == 0)
        panic("pmm: no usable region for %lu byte bitmap", bitmap_bytes);

    bitmap = (uint8_t *)(hhdm + bitmap_phys);
    memset(bitmap, 0xff, bitmap_bytes);
    free_count = 0;
    scan = 0;

    for (uint64_t i = 0; i < nregions; i++) {
        if (regions[i].type != LIMINE_MEMMAP_USABLE)
            continue;
        uint64_t start = ALIGN_UP(regions[i].base, PAGE_SIZE);
        uint64_t end = ALIGN_DOWN(regions[i].base + regions[i].length, PAGE_SIZE);
        for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
            uint64_t page = addr >> PAGE_SHIFT;
            if (page == 0 || page >= npages)
                continue;
            bit_mark_free(page);
            free_count++;
        }
    }

    for (uint64_t i = 0; i < bitmap_pages; i++) {
        uint64_t page = (bitmap_phys >> PAGE_SHIFT) + i;
        if (!bit_used(page)) {
            bit_mark_used(page);
            free_count--;
        }
    }

    pr_info("pmm", "%lu KiB free across %lu pages, bitmap %lu KiB at %#lx (%lu regions copied)",
            free_count * 4, npages, bitmap_bytes / 1024, bitmap_phys, nregions);
}

uint64_t pmm_alloc(void)
{
    uint64_t page = scan;
    while (page < npages) {
        if (bitmap[page >> 3] == 0xff) {
            page = (page & ~7ULL) + 8;
            continue;
        }
        if (!bit_used(page)) {
            take(page);
            scan = page + 1;
            return page << PAGE_SHIFT;
        }
        page++;
    }
    scan = 0;
    return 0;
}

uint64_t pmm_alloc_contig(uint64_t pages, uint64_t align)
{
    if (pages == 0)
        return 0;
    if (align < PAGE_SIZE)
        align = PAGE_SIZE;
    if (!IS_ALIGNED(align, PAGE_SIZE) || (align & (align - 1)) != 0)
        return 0;
    uint64_t align_pages = align >> PAGE_SHIFT;
    uint64_t page = 0;
    while (page + pages <= npages) {
        if (page % align_pages) {
            page = ALIGN_UP(page, align_pages);
            continue;
        }
        if (bitmap[page >> 3] == 0xff) {
            page = (page & ~7ULL) + 8;
            continue;
        }
        uint64_t run = 0;
        while (run < pages && page + run < npages && !bit_used(page + run))
            run++;
        if (run == pages)
            return claim_run(page, pages);
        page += run + 1;
    }
    return 0;
}

int pmm_free(uint64_t phys)
{
    if (!IS_ALIGNED(phys, PAGE_SIZE) || (phys >> PAGE_SHIFT) >= npages || phys == 0)
        return pmm_error("out of range");
    if (!allocatable(phys))
        return pmm_error("not allocatable");
    uint64_t page = phys >> PAGE_SHIFT;
    if (!bit_used(page))
        return pmm_error("double free");
    bit_mark_free(page);
    free_count++;
    if (page < scan)
        scan = page;
    return 0;
}

bool pmm_is_free(uint64_t phys)
{
    if (!IS_ALIGNED(phys, PAGE_SIZE) || (phys >> PAGE_SHIFT) >= npages)
        return false;
    return !bit_used(phys >> PAGE_SHIFT);
}

struct pmm_stats pmm_stats(void)
{
    struct pmm_stats s;
    memset(&s, 0, sizeof(s));
    s.npages = npages;
    s.free = free_count;
    s.used = npages - free_count;
    s.bitmap_phys = bitmap_phys;
    s.bitmap_pages = bitmap_pages;
    s.regions = nregions;
    for (uint64_t i = 0; i < nregions; i++) {
        uint64_t pages = regions[i].length >> PAGE_SHIFT;
        switch (regions[i].type) {
        case LIMINE_MEMMAP_USABLE:
            s.usable += pages;
            break;
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
            s.reclaimable += pages;
            break;
        case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
            s.kernel += pages;
            break;
        default:
            s.reserved += pages;
            break;
        }
    }
    return s;
}
