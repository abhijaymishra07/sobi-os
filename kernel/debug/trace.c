#include <sobi/trace.h>
#include <sobi/timer.h>
#include <sobi/panic.h>
#include <sobi/log.h>

static struct trace_event ring[TRACE_RING];
static uint32_t head;
static uint64_t total;
static uint64_t counters[8];
static uint64_t hist[TRACE_HIST];

void trace_event(uint32_t type, uint64_t a, uint64_t b)
{
    struct trace_event *ev = &ring[head];
    ev->seq = total;
    ev->timestamp = uptime_ms();
    ev->a = a;
    ev->b = b;
    ev->type = type;
    head = (head + 1) % TRACE_RING;
    total++;
    if (type < 8)
        counters[type]++;
}

void trace_dump(trace_dump_fn fn, void *ctx)
{
    uint32_t count;
    uint32_t start;
    if (!fn)
        return;
    count = total > TRACE_RING ? TRACE_RING : (uint32_t)total;
    start = total > TRACE_RING ? head : 0;
    for (uint32_t i = 0; i < count; i++)
        fn(&ring[(start + i) % TRACE_RING], ctx);
}

void trace_sched_latency(uint64_t latency_ms)
{
    unsigned bucket = 0;
    uint64_t v = latency_ms;
    while (v > 1 && bucket < TRACE_HIST - 1) {
        v >>= 1;
        bucket++;
    }
    hist[bucket]++;
}

uint64_t trace_total(void)
{
    return total;
}

uint64_t trace_counter(uint32_t type)
{
    if (type >= 8)
        return 0;
    return counters[type];
}

uint64_t trace_hist_bucket(unsigned bucket)
{
    if (bucket >= TRACE_HIST)
        return 0;
    return hist[bucket];
}

struct trace_grab {
    struct trace_event ev[TRACE_RING];
    int n;
};

static void grab_one(const struct trace_event *ev, void *ctx)
{
    struct trace_grab *g = ctx;
    if (g->n < TRACE_RING)
        g->ev[g->n] = *ev;
    g->n++;
}

void trace_self_test(void)
{
    struct trace_grab grab;
    uint64_t start = trace_total();
    uint64_t sched_before = trace_counter(TRACE_SCHED);
    uint64_t hist_before = trace_hist_bucket(0);
    const int n = 400;
    uint64_t expect_total;
    uint64_t first_seq;
    grab.n = 0;
    for (int i = 0; i < n; i++)
        trace_event(TRACE_SCHED, (uint64_t)i, 1);
    expect_total = start + (uint64_t)n;
    if (trace_total() != expect_total)
        panic("trace: count");
    if (trace_counter(TRACE_SCHED) != sched_before + (uint64_t)n)
        panic("trace: counter");
    trace_sched_latency(1);
    if (trace_hist_bucket(0) != hist_before + 1)
        panic("trace: hist");
    trace_dump(grab_one, &grab);
    if (grab.n != TRACE_RING)
        panic("trace: window");
    first_seq = expect_total - TRACE_RING;
    for (int i = 0; i < grab.n; i++) {
        if (grab.ev[i].seq != first_seq + (uint64_t)i)
            panic("trace: seq");
        if (i > 0 && grab.ev[i].seq <= grab.ev[i - 1].seq)
            panic("trace: monotonic");
        if (grab.ev[i].a != (uint64_t)(n - TRACE_RING) + (uint64_t)i)
            panic("trace: payload");
    }
    pr_info("trace", "wrap ok");
    pr_info("trace", "all checks passed");
}
