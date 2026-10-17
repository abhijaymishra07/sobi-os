#include <sobi/irq.h>
#include <sobi/log.h>
#include <sobi/panic.h>

static irq_handler_t handlers[256];
static volatile uint64_t spurious_count;

void irq_init(void)
{
    for (int i = 0; i < 256; i++)
        handlers[i] = NULL;
}

bool irq_register(uint64_t vector, irq_handler_t handler)
{
    if (vector < 32 || vector > 255 || !handler)
        return false;
    handlers[vector] = handler;
    return true;
}

uint64_t irq_spurious_count(void)
{
    return spurious_count;
}

void irq_dispatch(struct int_frame *frame)
{
    if (frame->vector == VEC_SPURIOUS) {
        spurious_count++;
        return;
    }
    irq_handler_t handler = handlers[frame->vector];
    if (!handler)
        panic("unhandled irq vector %#lx", frame->vector);
    handler(frame);
}
