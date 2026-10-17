.section .text
.global write
.global exit
.global read
write:
    mov $6, %rax
    syscall
    ret
read:
    mov $5, %rax
    syscall
    ret
exit:
    xor %eax, %eax
    syscall
    ud2

.section .note.GNU-stack, "", @progbits
