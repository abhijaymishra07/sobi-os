#include <sobi/interrupt.h>
#include <sobi/cpu.h>
#include <sobi/log.h>
#include <sobi/panic.h>
#include <sobi/ksym.h>

volatile uint64_t breakpoint_hits;

static struct {
    bool armed;
    bool hit;
    uint64_t vector;
    uint64_t fault_rip;
    uint64_t resume_rip;
    uint64_t cr2;
} expect;

void fault_expect(uint64_t vector, uint64_t fault_rip, uint64_t resume_rip)
{
    expect.armed = true;
    expect.hit = false;
    expect.vector = vector;
    expect.fault_rip = fault_rip;
    expect.resume_rip = resume_rip;
    expect.cr2 = 0;
}

bool fault_took(uint64_t *cr2)
{
    bool hit = expect.hit;
    if (cr2)
        *cr2 = expect.cr2;
    expect.hit = false;
    expect.armed = false;
    return hit;
}

static const char *const names[32] = {
    [VEC_DE] = "divide error",
    [VEC_DB] = "debug",
    [VEC_NMI] = "nmi",
    [VEC_BP] = "breakpoint",
    [VEC_OF] = "overflow",
    [VEC_BR] = "bound range",
    [VEC_UD] = "invalid opcode",
    [VEC_NM] = "device not available",
    [VEC_DF] = "double fault",
    [9] = "coprocessor segment overrun",
    [VEC_TS] = "invalid tss",
    [VEC_NP] = "segment not present",
    [VEC_SS] = "stack fault",
    [VEC_GP] = "general protection",
    [VEC_PF] = "page fault",
    [15] = "reserved",
    [VEC_MF] = "x87 fault",
    [VEC_AC] = "alignment check",
    [VEC_MC] = "machine check",
    [VEC_XF] = "simd fault",
};

static const char *vector_name(uint64_t vec)
{
    if (vec < 32 && names[vec])
        return names[vec];
    if (vec >= 0x20 && vec < 0x30)
        return "legacy irq";
    return "interrupt";
}

static void dump_frame(const struct int_frame *f)
{
    uint64_t off = 0;
    const char *sym = ksym_lookup(f->rip, &off);
    kprintf("vector %lu (%s) error %#lx\n", f->vector, vector_name(f->vector), f->error);
    if (sym)
        kprintf("rip=%#018lx <%s+%#lx> cs=%#lx rflags=%#lx\n",
                f->rip, sym, off, f->cs, f->rflags);
    else
        kprintf("rip=%#018lx cs=%#lx rflags=%#lx\n", f->rip, f->cs, f->rflags);
    kprintf("rsp=%#018lx ss=%#lx cr2=%#018lx cr3=%#018lx\n",
            f->rsp, f->ss, cpu_read_cr2(), cpu_read_cr3());
    kprintf("rax=%#018lx rbx=%#018lx rcx=%#018lx rdx=%#018lx\n",
            f->rax, f->rbx, f->rcx, f->rdx);
    kprintf("rsi=%#018lx rdi=%#018lx rbp=%#018lx\n", f->rsi, f->rdi, f->rbp);
    kprintf("r8 =%#018lx r9 =%#018lx r10=%#018lx r11=%#018lx\n",
            f->r8, f->r9, f->r10, f->r11);
    kprintf("r12=%#018lx r13=%#018lx r14=%#018lx r15=%#018lx\n",
            f->r12, f->r13, f->r14, f->r15);
    if (f->vector == VEC_PF) {
        kprintf("pf:");
        if (f->error & PF_PRESENT)
            kprintf(" present");
        else
            kprintf(" not-present");
        kprintf(f->error & PF_WRITE ? " write" : " read");
        kprintf(f->error & PF_USER ? " user" : " kernel");
        if (f->error & PF_INSTR)
            kprintf(" instr");
        if (f->error & PF_RESERVED)
            kprintf(" reserved");
        kprintf("\n");
    }
}

void exception_dispatch(struct int_frame *frame)
{
    if (frame->vector == VEC_BP) {
        breakpoint_hits++;
        return;
    }

    if (expect.armed && frame->vector == expect.vector && frame->rip == expect.fault_rip) {
        expect.armed = false;
        expect.hit = true;
        expect.cr2 = cpu_read_cr2();
        frame->rip = expect.resume_rip;
        return;
    }

    dump_frame(frame);

    if (frame->vector == VEC_DF)
        panic("double fault");

    if (expect.armed)
        panic("unexpected %s (wanted vec %lu rip %#lx, got vec %lu rip %#lx)",
              vector_name(frame->vector), expect.vector, expect.fault_rip,
              frame->vector, frame->rip);

    panic("unhandled %s", vector_name(frame->vector));
}
