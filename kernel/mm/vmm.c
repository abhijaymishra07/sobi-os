#include <sobi/vmm.h>
#include <sobi/sched.h>
#include <sobi/pmm.h>
#include <sobi/boot.h>
#include <sobi/string.h>
#include <sobi/log.h>
#include <sobi/panic.h>
#include <sobi/cpu.h>
#include <limine.h>

#define PTE_ADDR(e) ((e) & 0x000ffffffffff000ULL)
#define IDX(v, shift) (((v) >> (shift)) & 0x1ffULL)

extern char __text_start[], __text_end[];
extern char __rodata_start[], __rodata_end[];
extern char __data_start[], __kernel_end[];
extern char __stack_guard_boot[], __stack_guard_ist1[], __stack_guard_ist2[];

static uint64_t hhdm;
static uint64_t pml4_phys;
static uint64_t kernel_pml4;
static uint64_t mmio_next = VMM_MMIO_BASE;
static uint64_t stack_next = VMM_STACK_BASE;
static bool nx_ok;

static uint64_t *as_virt(uint64_t phys)
{
    return (uint64_t *)(hhdm + phys);
}

static uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static void wrmsr(uint32_t msr, uint64_t val)
{
    __asm__ volatile("wrmsr" : : "c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}

static bool cpu_has_nx(void)
{
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000));
    if (a < 0x80000001)
        return false;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000001));
    return (d & (1u << 20)) != 0;
}

static void enable_nx_and_global(void)
{
    if (cpu_has_nx()) {
        uint64_t efer = rdmsr(0xC0000080);
        efer |= 1ULL << 11;
        wrmsr(0xC0000080, efer);
        nx_ok = true;
    }
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= 1ULL << 7;
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
}

bool vmm_nx_supported(void)
{
    return nx_ok;
}

uint64_t vmm_kernel_cr3(void)
{
    return kernel_pml4 ? kernel_pml4 : pml4_phys;
}

uint64_t vmm_clone_address_space(void)
{
    uint64_t frame = pmm_alloc();
    if (frame == 0)
        return 0;
    uint64_t *dst = as_virt(frame);
    uint64_t *src = as_virt(vmm_kernel_cr3());
    memset(dst, 0, PAGE_SIZE);
    for (int i = 256; i < 512; i++)
        dst[i] = src[i];
    return frame;
}

static void free_tree(uint64_t table_phys, int level)
{
    uint64_t *table = as_virt(table_phys);
    for (int i = 0; i < 512; i++) {
        if (!(table[i] & VMM_PRESENT))
            continue;
        uint64_t child = PTE_ADDR(table[i]);
        if (level > 0)
            free_tree(child, level - 1);
        else
            pmm_free(child);
        table[i] = 0;
    }
    pmm_free(table_phys);
}

void vmm_destroy_address_space(uint64_t cr3)
{
    cr3 &= ~0xfffULL;
    if (cr3 == 0 || cr3 == vmm_kernel_cr3())
        return;
    if ((cpu_read_cr3() & ~0xfffULL) == cr3)
        vmm_switch(vmm_kernel_cr3());
    uint64_t *pml4 = as_virt(cr3);
    for (int i = 0; i < 256; i++) {
        if (!(pml4[i] & VMM_PRESENT))
            continue;
        free_tree(PTE_ADDR(pml4[i]), 2);
        pml4[i] = 0;
    }
    pmm_free(cr3);
}

void vmm_switch(uint64_t cr3)
{
    cr3 &= ~0xfffULL;
    if (cr3 == 0)
        return;
    pml4_phys = cr3;
    if ((cpu_read_cr3() & ~0xfffULL) != cr3)
        __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
    struct task *task = sched_current();
    if (task)
        task->cr3 = cr3;
}

static bool is_guard(uint64_t virt)
{
    return virt == (uint64_t)__stack_guard_boot || virt == (uint64_t)__stack_guard_ist1 ||
           virt == (uint64_t)__stack_guard_ist2;
}

static uint64_t *ensure_table(uint64_t *table, uint64_t index)
{
    if (table[index] & VMM_PRESENT)
        return as_virt(PTE_ADDR(table[index]));
    uint64_t frame = pmm_alloc();
    if (frame == 0)
        panic("vmm: out of frames for a page table");
    memset(as_virt(frame), 0, PAGE_SIZE);
    table[index] = frame | VMM_PRESENT | VMM_WRITE | VMM_USER;
    return as_virt(frame);
}

static uint64_t *leaf_table(uint64_t virt, bool create)
{
    uint64_t *pml4 = as_virt(pml4_phys);
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    if (create) {
        pdpt = ensure_table(pml4, IDX(virt, 39));
        pd = ensure_table(pdpt, IDX(virt, 30));
        pt = ensure_table(pd, IDX(virt, 21));
        return pt;
    }
    if (!(pml4[IDX(virt, 39)] & VMM_PRESENT))
        return NULL;
    pdpt = as_virt(PTE_ADDR(pml4[IDX(virt, 39)]));
    if (!(pdpt[IDX(virt, 30)] & VMM_PRESENT))
        return NULL;
    pd = as_virt(PTE_ADDR(pdpt[IDX(virt, 30)]));
    if (!(pd[IDX(virt, 21)] & VMM_PRESENT))
        return NULL;
    pt = as_virt(PTE_ADDR(pd[IDX(virt, 21)]));
    return pt;
}

static void invlpg(uint64_t virt)
{
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

bool vmm_map(uint64_t virt, uint64_t phys, uint64_t pages, uint64_t flags)
{
    if (!IS_ALIGNED(virt, PAGE_SIZE) || !IS_ALIGNED(phys, PAGE_SIZE) || pages == 0)
        return false;
    if (!nx_ok)
        flags &= ~VMM_NX;
    flags |= VMM_PRESENT;
    for (uint64_t i = 0; i < pages; i++) {
        uint64_t v = virt + i * PAGE_SIZE;
        uint64_t p = phys + i * PAGE_SIZE;
        uint64_t *pt = leaf_table(v, true);
        uint64_t slot = IDX(v, 12);
        if ((pt[slot] & VMM_PRESENT) && PTE_ADDR(pt[slot]) != p)
            panic("vmm: remap conflict at %#lx", v);
        pt[slot] = p | flags;
        invlpg(v);
    }
    return true;
}

static bool table_empty(const uint64_t *table)
{
    for (int i = 0; i < 512; i++) {
        if (table[i])
            return false;
    }
    return true;
}

static void free_empty(uint64_t virt)
{
    uint64_t *pml4 = as_virt(pml4_phys);
    uint64_t i4 = IDX(virt, 39);
    uint64_t i3 = IDX(virt, 30);
    uint64_t i2 = IDX(virt, 21);
    if (!(pml4[i4] & VMM_PRESENT))
        return;
    uint64_t *pdpt = as_virt(PTE_ADDR(pml4[i4]));
    if (!(pdpt[i3] & VMM_PRESENT))
        return;
    uint64_t *pd = as_virt(PTE_ADDR(pdpt[i3]));
    if (!(pd[i2] & VMM_PRESENT))
        return;
    uint64_t pt_phys = PTE_ADDR(pd[i2]);
    uint64_t *pt = as_virt(pt_phys);
    if (!table_empty(pt))
        return;
    pd[i2] = 0;
    pmm_free(pt_phys);
    if (!table_empty(pd))
        return;
    uint64_t pd_phys = PTE_ADDR(pdpt[i3]);
    pdpt[i3] = 0;
    pmm_free(pd_phys);
    if (!table_empty(pdpt))
        return;
    uint64_t pdpt_phys = PTE_ADDR(pml4[i4]);
    pml4[i4] = 0;
    pmm_free(pdpt_phys);
}

bool vmm_unmap(uint64_t virt, uint64_t pages)
{
    if (!IS_ALIGNED(virt, PAGE_SIZE) || pages == 0)
        return false;
    for (uint64_t i = 0; i < pages; i++) {
        uint64_t v = virt + i * PAGE_SIZE;
        uint64_t *pt = leaf_table(v, false);
        if (!pt)
            return false;
        pt[IDX(v, 12)] = 0;
        invlpg(v);
        free_empty(v);
    }
    return true;
}

bool vmm_protect(uint64_t virt, uint64_t pages, uint64_t flags)
{
    if (!IS_ALIGNED(virt, PAGE_SIZE) || pages == 0)
        return false;
    if (!nx_ok)
        flags &= ~VMM_NX;
    flags |= VMM_PRESENT;
    for (uint64_t i = 0; i < pages; i++) {
        uint64_t v = virt + i * PAGE_SIZE;
        uint64_t *pt = leaf_table(v, false);
        if (!pt || !(pt[IDX(v, 12)] & VMM_PRESENT))
            return false;
        uint64_t phys = PTE_ADDR(pt[IDX(v, 12)]);
        pt[IDX(v, 12)] = phys | flags;
        invlpg(v);
    }
    return true;
}

bool vmm_translate(uint64_t virt, uint64_t *phys, uint64_t *flags)
{
    uint64_t *pt = leaf_table(virt, false);
    if (!pt)
        return false;
    uint64_t entry = pt[IDX(virt, 12)];
    if (!(entry & VMM_PRESENT))
        return false;
    if (phys)
        *phys = PTE_ADDR(entry) + (virt & (PAGE_SIZE - 1));
    if (flags)
        *flags = entry & ~0x000ffffffffff000ULL;
    return true;
}

void *vmm_map_mmio(uint64_t phys, uint64_t size)
{
    if (size == 0)
        return NULL;
    uint64_t start = ALIGN_DOWN(phys, PAGE_SIZE);
    uint64_t end = ALIGN_UP(phys + size, PAGE_SIZE);
    uint64_t pages = (end - start) / PAGE_SIZE;
    uint64_t virt = mmio_next;
    mmio_next += pages * PAGE_SIZE;
    if (!vmm_map(virt, start, pages, VMM_WRITE | VMM_NX | VMM_PCD))
        return NULL;
    return (void *)(virt + (phys - start));
}

uint64_t vmm_alloc_stack(uint64_t pages)
{
    if (pages == 0)
        return 0;
    uint64_t base = stack_next;
    stack_next += (pages + 1) * PAGE_SIZE;
    for (uint64_t i = 0; i < pages; i++) {
        uint64_t frame = pmm_alloc();
        if (frame == 0)
            return 0;
        if (!vmm_map(base + (i + 1) * PAGE_SIZE, frame, 1, VMM_WRITE | VMM_NX))
            return 0;
    }
    return base + (pages + 1) * PAGE_SIZE;
}

static void map_kernel_span(uint64_t vstart, uint64_t vend, uint64_t flags)
{
    uint64_t virt_base = boot_info->kernel_virt_base;
    uint64_t phys_base = boot_info->kernel_phys_base;
    vstart = ALIGN_DOWN(vstart, PAGE_SIZE);
    vend = ALIGN_UP(vend, PAGE_SIZE);
    for (uint64_t v = vstart; v < vend; v += PAGE_SIZE) {
        if (is_guard(v))
            continue;
        uint64_t phys = phys_base + (v - virt_base);
        if (!vmm_map(v, phys, 1, flags))
            panic("vmm: failed to map kernel page %#lx", v);
    }
}

static bool hhdm_type(uint64_t type)
{
    return type == LIMINE_MEMMAP_USABLE || type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
           type == LIMINE_MEMMAP_EXECUTABLE_AND_MODULES || type == LIMINE_MEMMAP_FRAMEBUFFER ||
           type == LIMINE_MEMMAP_ACPI_RECLAIMABLE || type == LIMINE_MEMMAP_ACPI_NVS ||
           type == LIMINE_MEMMAP_RESERVED_MAPPED;
}

static void map_hhdm(void)
{
    const struct limine_memmap_response *map = boot_info->memmap;
    for (uint64_t i = 0; i < map->entry_count; i++) {
        const struct limine_memmap_entry *e = map->entries[i];
        if (!hhdm_type(e->type) || e->length == 0)
            continue;
        if (e->length > GiB(16))
            panic("vmm: refusing to map %lu byte region at %#lx", e->length, e->base);
        uint64_t start = ALIGN_DOWN(e->base, PAGE_SIZE);
        uint64_t end = ALIGN_UP(e->base + e->length, PAGE_SIZE);
        uint64_t flags = VMM_WRITE | VMM_NX;
        if (e->type == LIMINE_MEMMAP_FRAMEBUFFER)
            flags |= VMM_PCD;
        for (uint64_t phys = start; phys < end; phys += PAGE_SIZE) {
            uint64_t virt = hhdm + phys;
            uint64_t existing_phys = 0;
            if (vmm_translate(virt, &existing_phys, NULL)) {
                if (ALIGN_DOWN(existing_phys, PAGE_SIZE) != phys)
                    panic("vmm: hhdm conflict at %#lx", virt);
                continue;
            }
            if (!vmm_map(virt, phys, 1, flags))
                panic("vmm: hhdm map failed at %#lx", phys);
        }
    }
}

void vmm_init(void)
{
    hhdm = boot_info->hhdm_offset;
    enable_nx_and_global();

    pml4_phys = pmm_alloc();
    if (pml4_phys == 0)
        panic("vmm: no frame for pml4");
    memset(as_virt(pml4_phys), 0, PAGE_SIZE);

    map_kernel_span((uint64_t)__text_start, (uint64_t)__text_end, VMM_GLOBAL);
    map_kernel_span((uint64_t)__rodata_start, (uint64_t)__rodata_end, VMM_GLOBAL | VMM_NX);
    map_kernel_span((uint64_t)__data_start, (uint64_t)__kernel_end, VMM_WRITE | VMM_GLOBAL | VMM_NX);
    map_hhdm();

    kernel_pml4 = pml4_phys;
    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");

    uint64_t reclaimed = pmm_reclaim_bootloader();
    pr_info("vmm", "cr3 %#lx nx %s, reclaimed %lu KiB, guards unmapped",
            pml4_phys, nx_ok ? "on" : "off", reclaimed * 4);
}
