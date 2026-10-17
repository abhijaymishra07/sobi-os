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
