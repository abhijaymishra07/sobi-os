#include <sobi/interrupt.h>
#include <sobi/compiler.h>
#include <sobi/string.h>
#include <sobi/log.h>
#include <sobi/cpu.h>

struct __packed gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t gran;
    uint8_t base_high;
};

struct __packed tss_entry {
    struct gdt_entry low;
    uint32_t base_high;
    uint32_t zero;
};

struct __packed gdt_table {
    struct gdt_entry null;
    struct gdt_entry kcode;
    struct gdt_entry kdata;
    struct gdt_entry udata;
    struct gdt_entry ucode;
    struct tss_entry tss;
};

struct __packed gdtr {
    uint16_t limit;
    uint64_t base;
};

struct __packed tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb;
};

extern char boot_stack_top[];
extern char ist1_stack_top[];
extern char ist2_stack_top[];

static struct gdt_table gdt __aligned(16);
static struct tss tss __aligned(16);

static struct gdt_entry make_seg(uint8_t access, uint8_t flags)
{
    struct gdt_entry e;
    memset(&e, 0, sizeof(e));
    e.access = access;
    e.gran = (uint8_t)(flags << 4);
    return e;
}

static void fill_tss_entry(struct tss_entry *d, uint64_t base, uint32_t limit)
{
    memset(d, 0, sizeof(*d));
    d->low.limit_low = (uint16_t)(limit & 0xffff);
    d->low.base_low = (uint16_t)(base & 0xffff);
    d->low.base_mid = (uint8_t)((base >> 16) & 0xff);
    d->low.access = 0x89;
    d->low.gran = (uint8_t)((limit >> 16) & 0x0f);
    d->low.base_high = (uint8_t)((base >> 24) & 0xff);
    d->base_high = (uint32_t)(base >> 32);
}

void tss_set_rsp0(uint64_t rsp)
{
    tss.rsp0 = rsp;
}

void gdt_init(void)
{
    memset(&gdt, 0, sizeof(gdt));
    memset(&tss, 0, sizeof(tss));

    gdt.kcode = make_seg(0x9a, 0x2);
    gdt.kdata = make_seg(0x92, 0x0);
    gdt.udata = make_seg(0xf2, 0x0);
    gdt.ucode = make_seg(0xfa, 0x2);

    tss.iopb = sizeof(tss);
    tss.rsp0 = (uint64_t)boot_stack_top;
    tss.ist1 = (uint64_t)ist1_stack_top;
    tss.ist2 = (uint64_t)ist2_stack_top;
    fill_tss_entry(&gdt.tss, (uint64_t)&tss, sizeof(tss) - 1);

    struct gdtr gdtr = {
        .limit = (uint16_t)(sizeof(gdt) - 1),
        .base = (uint64_t)&gdt,
    };

    __asm__ volatile("lgdt %0" : : "m"(gdtr) : "memory");
    __asm__ volatile(
        "pushq %[csel]\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "movw %[dsel], %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        :
        : [csel] "i"((uint64_t)GDT_KCODE), [dsel] "i"((uint16_t)GDT_KDATA)
        : "rax", "memory");
    __asm__ volatile("ltr %0" : : "r"((uint16_t)GDT_TSS) : "memory");

    pr_info("cpu", "gdt loaded, tss %#x, ist1 %#lx, ist2 %#lx",
            GDT_TSS, tss.ist1, tss.ist2);
}
