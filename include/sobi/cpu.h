#ifndef SOBI_CPU_H
#define SOBI_CPU_H

#include <sobi/types.h>
#include <sobi/compiler.h>

static inline void cpu_cli(void)
{
    __asm__ volatile("cli" ::: "memory");
}

static inline void cpu_sti(void)
{
    __asm__ volatile("sti" ::: "memory");
}

static inline void cpu_hlt(void)
{
    __asm__ volatile("hlt" ::: "memory");
}

static inline uint64_t cpu_read_flags(void)
{
    uint64_t flags;
    __asm__ volatile("pushfq\n\tpopq %0" : "=r"(flags) : : "memory");
    return flags;
}

static inline uint64_t cpu_read_cr2(void)
{
    uint64_t v;
    __asm__ volatile("movq %%cr2, %0" : "=r"(v));
    return v;
}

static inline uint64_t cpu_read_cr3(void)
{
    uint64_t v;
    __asm__ volatile("movq %%cr3, %0" : "=r"(v));
    return v;
}

static inline uint64_t cpu_read_rsp(void)
{
    uint64_t v;
    __asm__ volatile("movq %%rsp, %0" : "=r"(v));
    return v;
}

static inline void cpu_pause(void)
{
    __asm__ volatile("pause");
}

static inline void cpu_cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    uint32_t av, bv, cv, dv;
    __asm__ volatile("cpuid" : "=a"(av), "=b"(bv), "=c"(cv), "=d"(dv) : "a"(leaf), "c"(0));
    if (a)
        *a = av;
    if (b)
        *b = bv;
    if (c)
        *c = cv;
    if (d)
        *d = dv;
}

static inline uint64_t cpu_rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void cpu_wrmsr(uint32_t msr, uint64_t val)
{
    uint32_t lo = (uint32_t)val;
    uint32_t hi = (uint32_t)(val >> 32);
    __asm__ volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

void cpu_halt_forever(void) __noreturn;

#endif
