.section .text
.global _start
_start:
    call main
    mov %rax, %rdi
    xor %eax, %eax
    syscall
1:
    jmp 1b

.section .note.GNU-stack, "", @progbits
