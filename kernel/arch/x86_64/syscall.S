.section .data
.global syscall_stack_top
.global syscall_stopped
syscall_stack_top:
    .quad 0
syscall_stopped:
    .byte 0

.section .bss
.lcomm syscall_user_rsp, 8

.section .text
.global syscall_entry
.type syscall_entry, @function
syscall_entry:
    movq %rsp, syscall_user_rsp(%rip)
    movq syscall_stack_top(%rip), %rsp
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    pushq syscall_user_rsp(%rip)
    pushq %rcx
    pushq %r11
    subq $56, %rsp
    movq %rax, 0(%rsp)
    movq %rdi, 8(%rsp)
    movq %rsi, 16(%rsp)
    movq %rdx, 24(%rsp)
    movq %r10, 32(%rsp)
    movq %r8, 40(%rsp)
    movq %r9, 48(%rsp)
    movq %rsp, %rdi
    call syscall_dispatch
    addq $56, %rsp
    cmpb $0, syscall_stopped(%rip)
    jne syscall_to_kernel
    popq %r11
    popq %rcx
    popq %rdx
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    movq %rdx, %rsp
    sysretq
syscall_to_kernel:
    movq user_return_rsp(%rip), %rsp
    jmp user_resume
.size syscall_entry, . - syscall_entry

.section .note.GNU-stack, "", @progbits
