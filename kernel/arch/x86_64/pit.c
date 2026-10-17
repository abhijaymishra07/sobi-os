#include <sobi/timer.h>
#include <sobi/io.h>
#include <sobi/cpu.h>

#define PIT_CH2  0x42
#define PIT_CMD  0x43
#define PIT_GATE 0x61
#define PIT_BASE 1193182u

static void pit_wait_counts(uint16_t count)
{
    if (count < 2)
        count = 2;
    outb(PIT_CMD, 0xB0);
    outb(PIT_CH2, (uint8_t)(count & 0xff));
    outb(PIT_CH2, (uint8_t)(count >> 8));
    uint8_t gate = inb(PIT_GATE);
    outb(PIT_GATE, (uint8_t)((gate & ~0x02) | 0x01));
    while ((inb(PIT_GATE) & 0x20) != 0)
        cpu_pause();
    while ((inb(PIT_GATE) & 0x20) == 0)
        cpu_pause();
    outb(PIT_GATE, (uint8_t)(inb(PIT_GATE) & ~0x01));
}

void pit_delay_ms(uint32_t ms)
{
    while (ms > 0) {
        uint32_t step = ms > 50 ? 50 : ms;
        uint32_t counts = (PIT_BASE * step) / 1000u;
        if (counts > 65535)
            counts = 65535;
        pit_wait_counts((uint16_t)counts);
        ms -= step;
    }
}
