#include <sobi/console.h>
#include <sobi/vmm.h>
#include <sobi/panic.h>
#include <sobi/compiler.h>

#define IOAPIC_PHYS_Q35 0xFEC00000ULL
#define IOAPIC_REG_VER  0x01
#define IOAPIC_REDTBL   0x10

static volatile uint32_t *ioapic;
static uint32_t ioapic_max;

static uint32_t ioapic_read(uint32_t reg)
{
    ioapic[0] = reg;
    barrier();
    return ioapic[4];
}

static void ioapic_write(uint32_t reg, uint32_t val)
{
    ioapic[0] = reg;
    barrier();
    ioapic[4] = val;
    barrier();
}

void ioapic_mask(uint32_t irq)
{
    uint32_t reg;
    uint32_t low;
    if (!ioapic || irq > ioapic_max)
        return;
    reg = IOAPIC_REDTBL + irq * 2;
    low = ioapic_read(reg);
    low |= 1u << 16;
    ioapic_write(reg, low);
}

void ioapic_route(uint32_t irq, uint32_t vector)
{
    uint32_t reg;
    uint32_t low;
    if (!ioapic || irq > ioapic_max)
        return;
    reg = IOAPIC_REDTBL + irq * 2;
    low = (uint32_t)(vector & 0xFFu);
    ioapic_write(reg + 1, 0);
    ioapic_write(reg, low);
}

void ioapic_init(uint64_t mmio_phys)
{
    uint32_t ver;
    void *mapped;
    if (mmio_phys == 0)
        mmio_phys = IOAPIC_PHYS_Q35;
    mapped = vmm_map_mmio(mmio_phys, 4096);
    if (!mapped)
        panic("ioapic: map");
    ioapic = mapped;
    ver = ioapic_read(IOAPIC_REG_VER);
    ioapic_max = (ver >> 16) & 0xFFu;
    if (ioapic_max > 23)
        ioapic_max = 23;
    for (uint32_t irq = 0; irq <= ioapic_max; irq++) {
        uint32_t reg = IOAPIC_REDTBL + irq * 2;
        ioapic_write(reg + 1, 0);
        ioapic_write(reg, (1u << 16) | 0x20u);
    }
    ioapic_route(1, 0x31);
}
