#ifndef SOBI_TRACE_H
#define SOBI_TRACE_H

#include <sobi/types.h>

enum trace_kind {
    TRACE_SCHED = 1,
    TRACE_SYSCALL = 2,
    TRACE_PAGEFAULT = 3,
    TRACE_IRQ = 4,
    TRACE_FS = 5
};

#define TRACE_RING 256
#define TRACE_HIST 16

struct trace_event {
    uint64_t seq;
    uint64_t timestamp;
    uint64_t a;
    uint64_t b;
    uint32_t type;
};

typedef void (*trace_dump_fn)(const struct trace_event *ev, void *ctx);

void trace_event(uint32_t type, uint64_t a, uint64_t b);
void trace_dump(trace_dump_fn fn, void *ctx);
void trace_sched_latency(uint64_t latency_ms);
uint64_t trace_total(void);
uint64_t trace_counter(uint32_t type);
uint64_t trace_hist_bucket(unsigned bucket);
void trace_self_test(void);

#endif
