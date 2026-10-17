#ifndef SOBI_IRQ_H
#define SOBI_IRQ_H

#include <sobi/types.h>
#include <sobi/interrupt.h>

typedef void (*irq_handler_t)(struct int_frame *frame);

void irq_init(void);
bool irq_register(uint64_t vector, irq_handler_t handler);
void irq_dispatch(struct int_frame *frame);
uint64_t irq_spurious_count(void);

#endif
