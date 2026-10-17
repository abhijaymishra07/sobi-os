.section .text

.global exc_ud2_insn
.global exc_ud2_resume
.type exc_ud2_insn, @function
exc_ud2_insn:
    ud2
exc_ud2_resume:
    ret
.size exc_ud2_insn, . - exc_ud2_insn

.global exc_de_insn
.global exc_de_div
.global exc_de_resume
.type exc_de_insn, @function
exc_de_insn:
    xorl %edx, %edx
    xorl %eax, %eax
    xorl %ecx, %ecx
exc_de_div:
    divl %ecx
exc_de_resume:
    ret
.size exc_de_insn, . - exc_de_insn

.global exc_pf_insn
.global exc_pf_load
.global exc_pf_resume
.type exc_pf_insn, @function
exc_pf_insn:
    movabs $0x1000, %rax
exc_pf_load:
    movq (%rax), %rax
exc_pf_resume:
    ret
.size exc_pf_insn, . - exc_pf_insn

.global exc_gp_insn
.global exc_gp_load
.global exc_gp_resume
.type exc_gp_insn, @function
exc_gp_insn:
    movabs $0x0000800000000000, %rax
exc_gp_load:
    movq (%rax), %rax
exc_gp_resume:
    ret
.size exc_gp_insn, . - exc_gp_insn

.global exc_int3_insn
.type exc_int3_insn, @function
exc_int3_insn:
    int3
    ret
.size exc_int3_insn, . - exc_int3_insn

.global exc_df_trigger
.type exc_df_trigger, @function
exc_df_trigger:
    movabs $0x0000800000000000, %rsp
    ud2
.size exc_df_trigger, . - exc_df_trigger

.section .note.GNU-stack, "", @progbits
