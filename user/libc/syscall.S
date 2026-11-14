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
.global spawn
spawn:
    mov $3, %rax
    syscall
    ret
.global waitpid
waitpid:
    mov $4, %rax
    syscall
    ret

.section .note.GNU-stack, "", @progbits
