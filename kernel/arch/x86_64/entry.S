#define BOOT_STACK_SIZE 16384

    .section .bss
    .balign 16
    .globl boot_stack_bottom
    .globl boot_stack_top
boot_stack_bottom:
    .skip BOOT_STACK_SIZE
boot_stack_top:

    .section .text
    .globl _start
    .type _start, @function
_start:
    cli
    cld
    leaq boot_stack_top(%rip), %rsp
    xorl %ebp, %ebp
    call kmain
.Lhalt:
    cli
    hlt
    jmp .Lhalt
    .size _start, . - _start

    .section .note.GNU-stack, "", @progbits
