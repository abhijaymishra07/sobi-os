#include <sobi/timer.h>
#include <sobi/apic.h>
#include <sobi/irq.h>
#include <sobi/cpu.h>
#include <sobi/log.h>
#include <sobi/panic.h>
#include <sobi/interrupt.h>
#include <sobi/sched.h>

static volatile uint64_t ticks;

static void timer_irq(struct int_frame *frame)
{
    (void)frame;
    ticks++;
    apic_eoi();
    sched_on_tick();
}

void timer_init(void)
{
    irq_init();
    apic_init();
    if (!irq_register(VEC_TIMER, timer_irq))
        panic("timer: failed to register");
    uint32_t counts = apic_timer_calibrate();
    apic_timer_start(VEC_TIMER, counts);
    pr_info("timer", "lapic periodic 100 Hz, %u counts/tick", counts);
    cpu_sti();
}

uint64_t timer_ticks(void)
{
    return ticks;
}

uint64_t uptime_ms(void)
{
    return ticks * 10;
}

void sleep_poll(uint64_t ms)
{
    if (ms == 0)
        return;
    uint64_t need = (ms + 9) / 10;
    uint64_t end = ticks + need;
    while (ticks < end)
        cpu_hlt();
}
