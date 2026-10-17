#include <sobi/interrupt.h>
#include <sobi/compiler.h>
#include <sobi/string.h>
#include <sobi/log.h>

struct __packed idt_gate {
    uint16_t off_low;
    uint16_t sel;
    uint8_t ist;
    uint8_t type;
    uint16_t off_mid;
    uint32_t off_high;
    uint32_t zero;
};

struct __packed idtr {
    uint16_t limit;
    uint64_t base;
};

extern void *isr_stub_table[256];

static struct idt_gate idt[256] __aligned(16);

static void set_gate(int vec, void *handler, uint8_t ist)
{
    uint64_t addr = (uint64_t)handler;
    idt[vec].off_low = (uint16_t)(addr & 0xffff);
    idt[vec].sel = GDT_KCODE;
    idt[vec].ist = ist;
    idt[vec].type = 0x8e;
    idt[vec].off_mid = (uint16_t)((addr >> 16) & 0xffff);
    idt[vec].off_high = (uint32_t)(addr >> 32);
    idt[vec].zero = 0;
}

void idt_init(void)
{
    memset(idt, 0, sizeof(idt));
    for (int i = 0; i < 256; i++) {
        uint8_t ist = 0;
        if (i == VEC_DF)
            ist = 1;
        else if (i == VEC_NMI || i == VEC_MC)
            ist = 2;
        set_gate(i, isr_stub_table[i], ist);
    }

    struct idtr idtr = {
        .limit = (uint16_t)(sizeof(idt) - 1),
        .base = (uint64_t)idt,
    };
    __asm__ volatile("lidt %0" : : "m"(idtr) : "memory");
    pr_info("cpu", "idt: 256 gates, df on ist1, nmi/mc on ist2");
}

void idt_allow_user(int vec)
{
    if (vec < 0 || vec > 255)
        return;
    idt[vec].type = 0xee;
}

void arch_init(void)
{
    gdt_init();
    idt_init();
    pic_init();
}
