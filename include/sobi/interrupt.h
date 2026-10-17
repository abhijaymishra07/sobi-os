#ifndef SOBI_INTERRUPT_H
#define SOBI_INTERRUPT_H

#include <sobi/types.h>

#define GDT_NULL  0x00
#define GDT_KCODE 0x08
#define GDT_KDATA 0x10
#define GDT_UDATA 0x18
#define GDT_UCODE 0x20
#define GDT_TSS   0x28

#define VEC_DE  0
#define VEC_DB  1
#define VEC_NMI 2
#define VEC_BP  3
#define VEC_OF  4
#define VEC_BR  5
#define VEC_UD  6
#define VEC_NM  7
#define VEC_DF  8
#define VEC_TS  10
#define VEC_NP  11
#define VEC_SS  12
#define VEC_GP  13
#define VEC_PF  14
#define VEC_MF  16
#define VEC_AC  17
#define VEC_MC  18
#define VEC_XF  19

#define VEC_TIMER    0x30
#define VEC_SPURIOUS 0xFF

#define PF_PRESENT  (1u << 0)
#define PF_WRITE    (1u << 1)
#define PF_USER     (1u << 2)
#define PF_RESERVED (1u << 3)
#define PF_INSTR    (1u << 4)

struct int_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t vector;
    uint64_t error;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

void gdt_init(void);
void tss_set_rsp0(uint64_t rsp);
void idt_init(void);
void idt_allow_user(int vec);
void pic_init(void);
void pic_eoi(uint8_t irq);
void arch_init(void);

void fault_expect(uint64_t vector, uint64_t fault_rip, uint64_t resume_rip);
bool fault_took(uint64_t *cr2);

extern volatile uint64_t breakpoint_hits;

void exception_dispatch(struct int_frame *frame);
bool proc_user_trap(struct int_frame *frame);

#endif
