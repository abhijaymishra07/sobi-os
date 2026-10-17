#ifdef SOBI_TEST

#include <sobi/test.h>
#include <sobi/io.h>
#include <sobi/cpu.h>
#include <sobi/log.h>
#include <sobi/string.h>
#include <sobi/panic.h>
#include <sobi/interrupt.h>
#include <sobi/ksym.h>
#include <sobi/pmm.h>
#include <sobi/boot.h>
#include <sobi/vmm.h>
#include <sobi/kheap.h>
#include <sobi/cpu.h>
#include <limine.h>

extern char exc_ud2_insn[], exc_ud2_resume[];
extern char exc_de_insn[], exc_de_div[], exc_de_resume[];
extern char exc_pf_insn[], exc_pf_load[], exc_pf_resume[];
extern char exc_gp_insn[], exc_gp_load[], exc_gp_resume[];
extern char exc_int3_insn[];
extern char exc_df_trigger[];
extern char exc_store[], exc_store_insn[], exc_store_resume[];
extern char exc_load_rax[], exc_load_rax_insn[], exc_load_rax_resume[];
extern char exc_call_rax[], exc_call_rax_resume[];
extern char __stack_guard_boot[];

static void call_at(char *addr)
{
    ((void (*)(void))addr)();
}

void test_exit(uint8_t code)
{
    kprintf("test: exit %u\n", code);
    outb(TEST_EXIT_PORT, code);
    cpu_halt_forever();
}

static bool cmdline_has(const char *cmdline, const char *key)
{
    if (!cmdline)
        return false;
    size_t klen = strlen(key);
    const char *p = cmdline;
    while (*p) {
        while (*p == ' ')
            p++;
        const char *start = p;
        while (*p && *p != ' ')
            p++;
        if ((size_t)(p - start) == klen && memcmp(start, key, klen) == 0)
            return true;
    }
    return false;
}

static void fail(const char *msg)
{
    pr_err("test", "%s", msg);
    test_exit(TEST_EXIT_CODE_FAIL);
}

static void test_exc(void)
{
    uint64_t before = breakpoint_hits;
    call_at(exc_int3_insn);
    if (breakpoint_hits != before + 1)
        fail("int3 did not fire");
    pr_info("test", "exc: int3 ok");

    uint64_t off = 1;
    const char *sym = ksym_lookup((uint64_t)exc_ud2_insn, &off);
    if (!sym || strcmp(sym, "exc_ud2_insn") != 0 || off != 0)
        fail("symbol lookup mismatch");
    pr_info("test", "exc: ksym ok");

    fault_expect(VEC_UD, (uint64_t)exc_ud2_insn, (uint64_t)exc_ud2_resume);
    call_at(exc_ud2_insn);
    if (!fault_took(NULL))
        fail("ud2 was not caught");
    pr_info("test", "exc: ud2 ok");

    fault_expect(VEC_DE, (uint64_t)exc_de_div, (uint64_t)exc_de_resume);
    call_at(exc_de_insn);
    if (!fault_took(NULL))
        fail("divide error was not caught");
    pr_info("test", "exc: de ok");

    fault_expect(VEC_PF, (uint64_t)exc_pf_load, (uint64_t)exc_pf_resume);
    call_at(exc_pf_insn);
    uint64_t cr2 = 0;
    if (!fault_took(&cr2) || cr2 != 0x1000)
        fail("page fault cr2 mismatch");
    pr_info("test", "exc: pf ok cr2=%#lx", cr2);

    fault_expect(VEC_GP, (uint64_t)exc_gp_load, (uint64_t)exc_gp_resume);
    call_at(exc_gp_insn);
    if (!fault_took(NULL))
        fail("general protection was not caught");
    pr_info("test", "exc: gp ok");

    pr_info("test", "exc: all faults handled");
    test_exit(TEST_EXIT_CODE_PASS);
}

struct pmm_node {
    struct pmm_node *next;
};

static void test_pmm(void)
{
    struct pmm_stats before = pmm_stats();
    if (before.free == 0 || before.free + before.used != before.npages)
        fail("pmm stats inconsistent");
    if (before.regions == 0 || before.bitmap_pages == 0)
        fail("pmm bitmap missing");
    if (pmm_is_free(before.bitmap_phys))
        fail("bitmap page is free");
    if (pmm_is_free(0))
        fail("page 0 is free");
    pr_info("test", "pmm: stats free=%lu used=%lu npages=%lu",
            before.free, before.used, before.npages);

    uint64_t reserved = 0;
    const struct limine_memmap_response *map = boot_info->memmap;
    for (uint64_t i = 0; i < map->entry_count; i++) {
        const struct limine_memmap_entry *e = map->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE || e->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
            e->type == LIMINE_MEMMAP_EXECUTABLE_AND_MODULES)
            continue;
        uint64_t addr = ALIGN_UP(e->base, PAGE_SIZE);
        if (addr < e->base + e->length && addr < before.npages * PAGE_SIZE) {
            reserved = addr;
            break;
        }
    }
    if (reserved == 0 || pmm_is_free(reserved) || pmm_type_at(reserved) == LIMINE_MEMMAP_USABLE)
        fail("reserved page looks allocatable");
    pr_info("test", "pmm: reserved %#lx held", reserved);

    uint64_t a = pmm_alloc();
    if (a == 0 || !IS_ALIGNED(a, PAGE_SIZE) || pmm_type_at(a) != LIMINE_MEMMAP_USABLE)
        fail("alloc returned a bad page");
    if (pmm_free(a) != 0)
        fail("free failed");
    struct pmm_stats mid = pmm_stats();
    if (mid.free != before.free)
        fail("free count changed across a round trip");
    pr_info("test", "pmm: round trip %#lx", a);

    uint64_t block = pmm_alloc_contig(8, 0x8000);
    if (block == 0 || !IS_ALIGNED(block, 0x8000))
        fail("contig alloc failed");
    for (uint64_t i = 0; i < 8; i++) {
        if (pmm_is_free(block + i * PAGE_SIZE))
            fail("contig page still free");
        if (pmm_free(block + i * PAGE_SIZE) != 0)
            fail("contig free failed");
    }
    if (pmm_stats().free != before.free)
        fail("free count changed across contig");
    pr_info("test", "pmm: contig %#lx", block);

    uint64_t hhdm = boot_info->hhdm_offset;
    struct pmm_node *head = NULL;
    uint64_t got = 0;
    for (;;) {
        uint64_t page = pmm_alloc();
        if (page == 0)
            break;
        if (pmm_type_at(page) != LIMINE_MEMMAP_USABLE)
            fail("allocator handed out a non-usable page");
        struct pmm_node *node = (struct pmm_node *)(hhdm + page);
        node->next = head;
        head = node;
        got++;
    }
    if (got != before.free)
        fail("exhaustion count != free count");
    if (pmm_stats().free != 0)
        fail("free count not zero at exhaustion");
    if (pmm_alloc() != 0)
        fail("alloc succeeded when exhausted");
    while (head) {
        struct pmm_node *next = head->next;
        uint64_t page = (uint64_t)head - hhdm;
        if (pmm_free(page) != 0)
            fail("free during recovery failed");
        head = next;
    }
    if (pmm_stats().free != before.free)
        fail("free count not restored");
    uint64_t again = pmm_alloc();
    if (again == 0)
        fail("alloc failed after recovery");
    if (pmm_free(again) != 0)
        fail("free after recovery failed");
    if (pmm_stats().free != before.free)
        fail("free count drifted");
    pr_info("test", "pmm: exhausted %lu pages and recovered", got);
    pr_info("test", "pmm: all checks passed");
    test_exit(TEST_EXIT_CODE_PASS);
}

static void test_pmm_dbl(void)
{
    uint64_t page = pmm_alloc();
    if (page == 0)
        fail("no page for double free");
    pmm_free(page);
    pr_info("test", "pmm: double free of %#lx", page);
    pmm_free(page);
    fail("double free returned");
}

static void test_pmm_range(void)
{
    pr_info("test", "pmm: freeing an out of range address");
    pmm_free(0xffffffffffe00000ULL);
    fail("out of range free returned");
}

static void test_vmm(void)
{
    if (!vmm_nx_supported())
        fail("nx not enabled");
    uint64_t cr3 = cpu_read_cr3() & ~0xfffULL;
    if (cr3 != vmm_kernel_cr3() || pmm_type_at(cr3) != LIMINE_MEMMAP_USABLE)
        fail("cr3 is not a usable kernel frame");

    const struct limine_memmap_response *map = boot_info->memmap;
    uint64_t reclaimed = 0;
    for (uint64_t i = 0; i < map->entry_count; i++) {
        if (map->entries[i]->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE) {
            fail("bootloader memory was not reclaimed");
        }
    }
    for (uint64_t i = 0; i < map->entry_count; i++) {
        uint64_t addr = ALIGN_UP(map->entries[i]->base, PAGE_SIZE);
        if (addr >= map->entries[i]->base && addr < map->entries[i]->base + map->entries[i]->length &&
            addr < 0x100000) {
            reclaimed = addr;
            break;
        }
    }
    if (reclaimed != 0 && pmm_is_free(reclaimed))
        pr_info("test", "vmm: reclaimed page %#lx is free", reclaimed);
    else
        pr_info("test", "vmm: reclaimed low page not in the free set");

    uint64_t frame = pmm_alloc();
    if (frame == 0)
        fail("no frame for vmm tests");
    uint8_t *alias = (uint8_t *)(boot_info->hhdm_offset + frame);
    const uint64_t virt = 0xfffffff800000000ULL;
    alias[0] = 0xc3;
    alias[1] = 0x11;
    if (!vmm_map(virt, frame, 1, 0))
        fail("map failed");
    uint64_t got = 0;
    uint64_t flags = 0;
    if (!vmm_translate(virt, &got, &flags) || (got & ~0xfffULL) != frame)
        fail("translate mismatch");
    if (((uint8_t *)virt)[1] != 0x11)
        fail("mapped read mismatch");
    pr_info("test", "vmm: map %#lx -> %#lx", virt, frame);

    typedef void (*call_fn)(uint64_t);
    ((call_fn)exc_call_rax)(virt);
    pr_info("test", "vmm: execute ok");

    if (!vmm_protect(virt, 1, VMM_NX))
        fail("protect nx failed");
    fault_expect(VEC_PF, virt, (uint64_t)exc_call_rax_resume);
    ((call_fn)exc_call_rax)(virt);
    uint64_t cr2 = 0;
    if (!fault_took(&cr2) || cr2 != virt)
        fail("nx fault did not report the target");
    pr_info("test", "vmm: nx fault cr2=%#lx", cr2);

    if (!vmm_protect(virt, 1, VMM_NX))
        fail("protect ro failed");
    typedef void (*store_fn)(uint64_t, uint64_t);
    fault_expect(VEC_PF, (uint64_t)exc_store_insn, (uint64_t)exc_store_resume);
    ((store_fn)exc_store)(virt, 0x22);
    if (!fault_took(&cr2) || cr2 != virt)
        fail("read-only store did not fault");
    if (alias[1] != 0x11)
        fail("read-only store modified memory");
    pr_info("test", "vmm: ro fault ok");

    if (!vmm_unmap(virt, 1) || vmm_translate(virt, NULL, NULL))
        fail("unmap failed");
    typedef uint64_t (*load_fn)(uint64_t);
    fault_expect(VEC_PF, (uint64_t)exc_load_rax_insn, (uint64_t)exc_load_rax_resume);
    ((load_fn)exc_load_rax)(virt);
    if (!fault_took(&cr2) || cr2 != virt)
        fail("unmapped load did not fault");
    pr_info("test", "vmm: unmap tlb ok");

    fault_expect(VEC_PF, (uint64_t)exc_load_rax_insn, (uint64_t)exc_load_rax_resume);
    ((load_fn)exc_load_rax)((uint64_t)__stack_guard_boot);
    if (!fault_took(&cr2) || cr2 != (uint64_t)__stack_guard_boot)
        fail("stack guard is mapped");
    pr_info("test", "vmm: stack guard %#lx", (uint64_t)__stack_guard_boot);

    uint64_t top = vmm_alloc_stack(4);
    if (top == 0 || !vmm_translate(top - 8, NULL, NULL))
        fail("alloc stack failed");
    fault_expect(VEC_PF, (uint64_t)exc_load_rax_insn, (uint64_t)exc_load_rax_resume);
    ((load_fn)exc_load_rax)(top - 5 * PAGE_SIZE);
    if (!fault_took(NULL))
        fail("fresh stack has no guard");
    pr_info("test", "vmm: alloc stack %#lx", top);

    pmm_free(frame);
    pr_info("test", "vmm: all checks passed");
    test_exit(TEST_EXIT_CODE_PASS);
}

static void test_kheap(void)
{
    struct kheap_stats before = kheap_stats();
    void *a = kmalloc(1);
    void *b = kmalloc(24);
    void *c = kmalloc(2000);
    void *d = kmalloc(5000);
    if (!a || !b || !c || !d)
        fail("kmalloc returned null");
    if (((uint64_t)a | (uint64_t)b | (uint64_t)c | (uint64_t)d) & 0xf)
        fail("allocation is not 16-byte aligned");
    memset(a, 0xab, 1);
    memset(b, 0xcd, 24);
    memset(c, 0xef, 2000);
    memset(d, 0x11, 5000);
    kfree(b);
    void *b2 = kmalloc(24);
    if (b2 != b)
        fail("slab did not reuse a freed slot");
    kfree(a);
    kfree(b2);
    kfree(c);
    kfree(d);

    void *lots[256];
    for (int i = 0; i < 256; i++) {
        lots[i] = kmalloc((size_t)((i * 17) % 3000) + 1);
        if (!lots[i])
            fail("stress alloc failed");
        ((uint8_t *)lots[i])[0] = (uint8_t)i;
    }
    for (int i = 0; i < 256; i++)
        kfree(lots[i]);

    void *z = kzalloc(32);
    if (!z || ((uint8_t *)z)[31] != 0)
        fail("kzalloc not zeroed");
    void *bigger = krealloc(z, 100);
    if (!bigger)
        fail("krealloc failed");
    kfree(bigger);

    struct kheap_stats after = kheap_stats();
    if (after.live_bytes != before.live_bytes)
        fail("kheap leaked");
    if (after.allocs <= before.allocs || after.frees <= before.frees)
        fail("kheap counters did not move");
    pr_info("test", "kheap: live %lu allocs %lu frees %lu slabs %lu",
            after.live_bytes, after.allocs, after.frees, after.slabs);
    pr_info("test", "kheap: all checks passed");
    test_exit(TEST_EXIT_CODE_PASS);
}

static void test_kheap_dbl(void)
{
    void *p = kmalloc(32);
    if (!p)
        fail("no heap block");
    kfree(p);
    pr_info("test", "kheap: double free of %p", p);
    kfree(p);
    fail("double free returned");
}

static void test_df(void)
{
    pr_info("test", "exc: triggering double fault");
    call_at(exc_df_trigger);
    fail("double fault returned");
}

void test_run(const char *cmdline)
{
    pr_info("test", "cmdline: \"%s\"", cmdline ? cmdline : "");

    if (cmdline_has(cmdline, "test=panic"))
        panic("deliberate test panic (value=%d)", 42);

    if (cmdline_has(cmdline, "test=exc"))
        test_exc();

    if (cmdline_has(cmdline, "test=df"))
        test_df();

    if (cmdline_has(cmdline, "test=pmm-dbl"))
        test_pmm_dbl();

    if (cmdline_has(cmdline, "test=pmm-range"))
        test_pmm_range();

    if (cmdline_has(cmdline, "test=pmm"))
        test_pmm();

    if (cmdline_has(cmdline, "test=vmm"))
        test_vmm();

    if (cmdline_has(cmdline, "test=kheap-dbl"))
        test_kheap_dbl();

    if (cmdline_has(cmdline, "test=kheap"))
        test_kheap();

    if (cmdline_has(cmdline, "test=boot") || !cmdline || !*cmdline) {
        pr_info("test", "boot test complete");
        test_exit(TEST_EXIT_CODE_PASS);
    }

    pr_err("test", "unknown test selector in cmdline");
    test_exit(TEST_EXIT_CODE_FAIL);
}

#endif
