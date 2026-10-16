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

void cpu_halt_forever(void) __noreturn;

#endif
