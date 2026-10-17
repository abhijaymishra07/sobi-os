#ifndef SOBI_TIMER_H
#define SOBI_TIMER_H

#include <sobi/types.h>

void timer_init(void);
uint64_t timer_ticks(void);
uint64_t uptime_ms(void);
void sleep_poll(uint64_t ms);
void pit_delay_ms(uint32_t ms);

#endif
