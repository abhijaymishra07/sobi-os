.section .data
.global user_return_rsp
user_return_rsp:
    .quad 0

.section .text
.global enter_user
.global user_resume
.type enter_user, @function
enter_user:
    /* Every SysV callee-saved register: user_resume is reached with user values in them. */
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $8, %rsp
    movq %rdi, %r12
    movq %rsi, %r13
    movq %rsp, user_return_rsp(%rip)
    movq %rsp, syscall_stack_top(%rip)
    movq %rsp, %rdi
    call tss_set_rsp0
    pushq $0x1b
    pushq %r13
    pushq $0x202
    pushq $0x23
    pushq %r12
    iretq
user_resume:
    addq $8, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    xorl %eax, %eax
    ret
.size enter_user, . - enter_user

.global copy_user_load
.global copy_user_load_insn
.global copy_user_load_fault
.type copy_user_load, @function
copy_user_load:
    xorl %eax, %eax
    testq %rdx, %rdx
    jz 2f
1:
copy_user_load_insn:
    movb (%rsi), %al
    movb %al, (%rdi)
    incq %rsi
    incq %rdi
    decq %rdx
    jnz 1b
    xorl %eax, %eax
2:
    ret
copy_user_load_fault:
    movl $-14, %eax
    ret
.size copy_user_load, . - copy_user_load

.global copy_user_store
.global copy_user_store_insn
.global copy_user_store_fault
.type copy_user_store, @function
copy_user_store:
    xorl %eax, %eax
    testq %rdx, %rdx
    jz 2f
1:
    movb (%rsi), %al
copy_user_store_insn:
    movb %al, (%rdi)
    incq %rsi
    incq %rdi
    decq %rdx
    jnz 1b
    xorl %eax, %eax
2:
    ret
copy_user_store_fault:
    movl $-14, %eax
    ret
.size copy_user_store, . - copy_user_store

.section .note.GNU-stack, "", @progbits
