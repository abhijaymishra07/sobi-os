#include <sobi/proc.h>
#include <sobi/uaccess.h>
#include <sobi/vmm.h>
#include <sobi/pmm.h>
#include <sobi/boot.h>
#include <sobi/kheap.h>
#include <sobi/cpu.h>
#include <sobi/interrupt.h>
#include <sobi/sched.h>
#include <sobi/log.h>
#include <sobi/panic.h>
#include <sobi/string.h>

extern void enter_user(uint64_t rip, uint64_t user_rsp);
extern char user_resume[];
extern uint64_t user_return_rsp;

static struct process *current_proc;
static uint64_t next_pid = 1;
static uint64_t int80_hits;

struct process *proc_current(void)
{
    return current_proc;
}

void proc_init(void)
{
    idt_allow_user(0x80);
}

struct process *proc_create(struct process *parent)
{
    struct process *proc = kzalloc(sizeof(*proc));
    if (!proc)
        return NULL;
    proc->cr3 = vmm_clone_address_space();
    if (proc->cr3 == 0) {
        kfree(proc);
        return NULL;
    }
    proc->pid = next_pid++;
    proc->alive = true;
    proc->brk = USER_IMAGE + PAGE_SIZE;
    proc->parent = parent;
    if (parent) {
        proc->ppid = parent->pid;
        proc->sibling = parent->child;
        parent->child = proc;
    }
    return proc;
}

static bool map_user_page(struct process *proc, uint64_t virt, uint64_t phys, uint64_t flags)
{
    uint64_t back = cpu_read_cr3() & ~0xfffULL;
    vmm_switch(proc->cr3);
    bool ok = vmm_map(virt, phys, 1, flags | VMM_USER);
    vmm_switch(back);
    return ok;
}

void proc_switch(struct process *proc)
{
    current_proc = proc;
    if (proc && proc->cr3)
        vmm_switch(proc->cr3);
}

void proc_enter(struct process *proc, uint64_t rip, uint64_t user_rsp)
{
    current_proc = proc;
    vmm_switch(proc->cr3);
    enter_user(rip, user_rsp);
    vmm_switch(vmm_kernel_cr3());
}

bool proc_user_trap(struct int_frame *frame)
{
    if ((frame->cs & 3) != 3)
        return false;
    if (frame->vector == 0x80) {
        int80_hits++;
    } else if (frame->vector >= 32) {
        return false;
    } else {
        pr_info("proc", "killed pid %lu vec %lu", current_proc ? current_proc->pid : 0, frame->vector);
        if (current_proc)
            current_proc->alive = false;
    }
    frame->rip = (uint64_t)user_resume;
    frame->cs = GDT_KCODE;
    frame->ss = GDT_KDATA;
    frame->rsp = user_return_rsp;
    frame->rflags = 0x202;
    return true;
}

static uint64_t frame_with(const uint8_t *bytes, size_t n)
{
    uint64_t phys = pmm_alloc();
    if (phys == 0)
        return 0;
    uint8_t *alias = (uint8_t *)(boot_info->hhdm_offset + phys);
    memset(alias, 0, PAGE_SIZE);
    memcpy(alias, bytes, n);
    return phys;
}

void proc_self_test(void)
{
    preempt_disable();
    static const uint8_t trap_blob[] = { 0xcd, 0x80 };
    static const uint8_t kill_blob[] = {
        0x48, 0xb8, 0x00, 0x00, 0x00, 0x80, 0xff, 0xff, 0xff, 0xff, 0x8a, 0x00
    };
    struct process *proc = proc_create(NULL);
    if (!proc)
        panic("proc: create failed");
    uint64_t code = frame_with(trap_blob, sizeof(trap_blob));
    uint64_t stack = pmm_alloc();
    if (!code || !stack || !map_user_page(proc, USER_IMAGE, code, 0))
        panic("proc: map code failed");
    if (!map_user_page(proc, USER_STACK_TOP - PAGE_SIZE, stack, VMM_WRITE | VMM_NX))
        panic("proc: map stack failed");
    uint64_t before = int80_hits;
    proc_enter(proc, USER_IMAGE, USER_STACK_TOP - 16);
    if (int80_hits <= before)
        panic("proc: int 0x80 did not return");
    pr_info("test", "user: trap ok");

    struct process *bad = proc_create(NULL);
    uint64_t bad_code = frame_with(kill_blob, sizeof(kill_blob));
    uint64_t bad_stack = pmm_alloc();
    map_user_page(bad, USER_IMAGE, bad_code, 0);
    map_user_page(bad, USER_STACK_TOP - PAGE_SIZE, bad_stack, VMM_WRITE | VMM_NX);
    proc_enter(bad, USER_IMAGE, USER_STACK_TOP - 16);
    if (bad->alive)
        panic("proc: kernel access was not killed");
    pr_info("test", "user: killed pid %lu", bad->pid);

    struct process *a = proc_create(NULL);
    struct process *b = proc_create(NULL);
    uint64_t secret = pmm_alloc();
    uint8_t *alias = (uint8_t *)(boot_info->hhdm_offset + secret);
    alias[0] = 0x5a;
    if (!map_user_page(a, USER_IMAGE + PAGE_SIZE, secret, VMM_WRITE | VMM_NX))
        panic("proc: isolation map failed");
    vmm_switch(b->cr3);
    if (vmm_translate(USER_IMAGE + PAGE_SIZE, NULL, NULL))
        panic("proc: other address space can see the page");
    vmm_switch(a->cr3);
    if (((uint8_t *)(USER_IMAGE + PAGE_SIZE))[0] != 0x5a)
        panic("proc: secret mismatch");
    uint8_t got = 0;
    if (copy_from_user(&got, (void *)(USER_IMAGE + PAGE_SIZE), 1) != 0 || got != 0x5a)
        panic("proc: copy_from_user failed");
    vmm_switch(vmm_kernel_cr3());
    if (copy_from_user(&got, (void *)0xffffffff80000000ULL, 1) != -EFAULT)
        panic("proc: kernel pointer was accepted");
    if (copy_from_user(&got, (void *)0x500000ULL, 1) != -EFAULT)
        panic("proc: unmapped user pointer was accepted");
    pr_info("test", "user: isolation ok");
    pr_info("test", "user: all checks passed");
    preempt_enable();
}
