#ifndef SOBI_APIC_H
#define SOBI_APIC_H

#include <sobi/types.h>

void apic_init(void);
void apic_eoi(void);
uint32_t apic_timer_calibrate(void);
void apic_timer_start(uint8_t vector, uint32_t counts);

#endif
