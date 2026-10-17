.altmacro

.macro isr_noerr num
    .section .text
    .global isr\num
    .type isr\num, @function
isr\num:
    pushq $0
    pushq $\num
    jmp isr_common
.endm

.macro isr_err num
    .section .text
    .global isr\num
    .type isr\num, @function
isr\num:
    pushq $\num
    jmp isr_common
.endm

.macro make_isr num
    .if \num == 8 || \num == 10 || \num == 11 || \num == 12 || \num == 13 || \num == 14 || \num == 17 || \num == 21 || \num == 29 || \num == 30
        isr_err \num
    .else
        isr_noerr \num
    .endif
.endm

.set i, 0
.rept 256
    make_isr %i
    .set i, i+1
.endr

.section .text
.global isr_common
.type isr_common, @function
isr_common:
    cld
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %rdi
    call exception_dispatch
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax
    addq $16, %rsp
    iretq
.size isr_common, . - isr_common

.macro stub_quad num
    .quad isr\num
.endm

.section .rodata
.align 8
.global isr_stub_table
isr_stub_table:
.set i, 0
.rept 256
    stub_quad %i
    .set i, i+1
.endr

.section .note.GNU-stack, "", @progbits
