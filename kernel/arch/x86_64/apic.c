#include <sobi/apic.h>
#include <sobi/cpu.h>
#include <sobi/vmm.h>
#include <sobi/timer.h>
#include <sobi/interrupt.h>
#include <sobi/log.h>
#include <sobi/panic.h>

#define IA32_APIC_BASE 0x1B
#define APIC_ENABLE    (1u << 8)
#define APIC_BSP       (1u << 11)

#define LAPIC_ID      0x020
#define LAPIC_VER     0x030
#define LAPIC_TPR     0x080
#define LAPIC_EOI     0x0B0
#define LAPIC_SVR     0x0F0
#define LAPIC_ESR     0x280
#define LAPIC_LVT_TMR 0x320
#define LAPIC_LVT_LINT0 0x350
#define LAPIC_LVT_LINT1 0x360
#define LAPIC_LVT_ERR 0x370
#define LAPIC_TICR    0x380
#define LAPIC_TCCR    0x390
#define LAPIC_TDCR    0x3E0

#define LVT_MASKED (1u << 16)
#define LVT_PERIODIC (1u << 17)
#define TDCR_DIV16 0x3

static volatile uint32_t *lapic;

static uint32_t lapic_read(uint32_t reg)
{
    return lapic[reg / 4];
}

static void lapic_write(uint32_t reg, uint32_t value)
{
    lapic[reg / 4] = value;
    (void)lapic[reg / 4];
}

void apic_eoi(void)
{
    lapic_write(LAPIC_EOI, 0);
}

static uint32_t calibrate_once(void)
{
    lapic_write(LAPIC_TDCR, TDCR_DIV16);
    lapic_write(LAPIC_TICR, 0xffffffffu);
    pit_delay_ms(10);
    uint32_t current = lapic_read(LAPIC_TCCR);
    return 0xffffffffu - current;
}

static uint32_t median5(uint32_t *v)
{
    for (int i = 0; i < 5; i++) {
        for (int j = i + 1; j < 5; j++) {
            if (v[j] < v[i]) {
                uint32_t t = v[i];
                v[i] = v[j];
                v[j] = t;
            }
        }
    }
    return v[2];
}

void apic_init(void)
{
    uint32_t a, d;
    cpu_cpuid(1, &a, NULL, NULL, &d);
    if ((d & (1u << 9)) == 0)
        panic("apic: no local apic");

    uint64_t base_msr = cpu_rdmsr(IA32_APIC_BASE);
    if ((base_msr & APIC_BSP) == 0)
        panic("apic: not the bootstrap processor");
    if ((base_msr & APIC_ENABLE) == 0)
        cpu_wrmsr(IA32_APIC_BASE, base_msr | APIC_ENABLE);

    uint64_t phys = base_msr & ~0xfffULL;
    lapic = vmm_map_mmio(phys, PAGE_SIZE);
    if (!lapic)
        panic("apic: failed to map %#lx", phys);

    lapic_write(LAPIC_SVR, VEC_SPURIOUS | (1u << 8));
    lapic_write(LAPIC_TPR, 0);
    lapic_write(LAPIC_LVT_LINT0, LVT_MASKED);
    lapic_write(LAPIC_LVT_LINT1, LVT_MASKED);
    lapic_write(LAPIC_LVT_ERR, LVT_MASKED);
    lapic_write(LAPIC_LVT_TMR, LVT_MASKED);
    lapic_write(LAPIC_ESR, 0);
    lapic_write(LAPIC_ESR, 0);

    pr_info("apic", "xapic at %#lx id %u version %#x", phys, lapic_read(LAPIC_ID) >> 24,
            lapic_read(LAPIC_VER));
}

uint32_t apic_timer_calibrate(void)
{
    uint32_t samples[5];
    for (int i = 0; i < 5; i++)
        samples[i] = calibrate_once();
    uint32_t counts = median5(samples);
    if (counts < 1000)
        panic("apic: calibration too small (%u)", counts);
    return counts;
}

void apic_timer_start(uint8_t vector, uint32_t counts)
{
    lapic_write(LAPIC_TDCR, TDCR_DIV16);
    lapic_write(LAPIC_LVT_TMR, vector | LVT_PERIODIC);
    lapic_write(LAPIC_TICR, counts);
}
