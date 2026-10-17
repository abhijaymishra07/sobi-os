#include <sobi/syscall.h>
#include <sobi/proc.h>
#include <sobi/uaccess.h>
#include <sobi/cpu.h>
#include <sobi/serial.h>
#include <sobi/sched.h>
#include <sobi/timer.h>
#include <sobi/vmm.h>
#include <sobi/pmm.h>
#include <sobi/boot.h>
#include <sobi/log.h>
#include <sobi/panic.h>
#include <sobi/string.h>

#define MSR_EFER  0xC0000080
#define MSR_STAR  0xC0000081
#define MSR_LSTAR 0xC0000082
#define MSR_FMASK 0xC0000084
#define EFER_SCE  1

struct sc_args {
    uint64_t num;
    uint64_t a0;
    uint64_t a1;
    uint64_t a2;
    uint64_t a3;
    uint64_t a4;
    uint64_t a5;
};

extern char syscall_entry[];
extern uint8_t syscall_stopped;

int64_t syscall_dispatch(struct sc_args *args);

static char input_buf[64];
static unsigned input_r;
static unsigned input_w;

void syscall_push_input(char c)
{
    unsigned next = (input_w + 1) % sizeof(input_buf);
    if (next == input_r)
        return;
    input_buf[input_w] = c;
    input_w = next;
}

static int64_t sys_write(uint64_t fd, uint64_t ptr, uint64_t len)
{
    if (fd != 1 && fd != 2)
        return -EBADF;
    if (len > 4096)
        return -EINVAL;
    if (!user_range_ok((void *)ptr, (size_t)len))
        return -EFAULT;
    char buf[4096];
    if (len && copy_from_user(buf, (void *)ptr, (size_t)len) != 0)
        return -EFAULT;
    serial_write(buf, (size_t)len);
    return (int64_t)len;
}

static int64_t sys_read(uint64_t fd, uint64_t ptr, uint64_t len)
{
    if (fd != 0)
        return -EBADF;
    if (len == 0)
        return 0;
    if (len > 4096)
        return -EINVAL;
    if (!user_range_ok((void *)ptr, 1))
        return -EFAULT;
    if (input_r == input_w) {
        if (serial_rx_ready())
            syscall_push_input(serial_getc());
    }
    if (input_r == input_w)
        return -EAGAIN;
    char ch = input_buf[input_r];
    input_r = (input_r + 1) % sizeof(input_buf);
    if (copy_to_user((void *)ptr, &ch, 1) != 0)
        return -EFAULT;
    return 1;
}

static int64_t sys_wait(void)
{
    struct process *self = proc_current();
    if (!self)
        return -ECHILD;
    for (struct process *c = self->child; c; c = c->sibling) {
        if (!c->alive)
            return c->exit_status;
    }
    return -ECHILD;
}

int64_t syscall_dispatch(struct sc_args *args)
{
    switch (args->num) {
    case SYS_EXIT:
        if (proc_current()) {
            proc_current()->exit_status = (int)args->a0;
            proc_current()->alive = false;
        }
        syscall_stopped = 1;
        return (int64_t)args->a0;
    case SYS_GETPID:
        return proc_current() ? (int64_t)proc_current()->pid : -EINVAL;
    case SYS_GETPPID:
        return proc_current() ? (int64_t)proc_current()->ppid : -EINVAL;
    case SYS_SPAWN:
        return -ENOENT;
    case SYS_WAIT:
        return sys_wait();
    case SYS_READ:
        return sys_read(args->a0, args->a1, args->a2);
    case SYS_WRITE:
        return sys_write(args->a0, args->a1, args->a2);
    case SYS_SLEEP:
        sched_sleep_ms(args->a0);
        return 0;
    case SYS_UPTIME:
        return (int64_t)uptime_ms();
    case SYS_YIELD:
        sched_yield();
        return 0;
    case SYS_SBRK: {
        struct process *self = proc_current();
        if (!self)
            return -EINVAL;
        uint64_t old = self->brk;
        uint64_t next = old + args->a0;
        if (args->a0 && (next < old || next >= USER_LIMIT))
            return -EINVAL;
        self->brk = next;
        return (int64_t)old;
    }
    default:
        return -ENOSYS;
    }
}

int64_t syscall_invoke(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    struct sc_args args = { num, a0, a1, a2, a3, a4, a5 };
    syscall_stopped = 0;
    return syscall_dispatch(&args);
}

void syscall_init(void)
{
    uint64_t efer = cpu_rdmsr(MSR_EFER);
    cpu_wrmsr(MSR_EFER, efer | EFER_SCE);
    uint64_t star = (0x10ULL << 48) | (0x08ULL << 32);
    cpu_wrmsr(MSR_STAR, star);
    cpu_wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    cpu_wrmsr(MSR_FMASK, 0x47700);
    pr_info("syscall", "star %#lx lstar %#lx", star, (uint64_t)syscall_entry);
}

static uint64_t map_blob(struct process *proc, const void *bytes, size_t n)
{
    uint64_t phys = pmm_alloc();
    if (!phys)
        return 0;
    uint8_t *alias = (uint8_t *)(boot_info->hhdm_offset + phys);
    memset(alias, 0, PAGE_SIZE);
    memcpy(alias, bytes, n);
    uint64_t back = cpu_read_cr3() & ~0xfffULL;
    vmm_switch(proc->cr3);
    bool ok = vmm_map(USER_IMAGE, phys, 1, VMM_USER);
    vmm_switch(back);
    return ok ? phys : 0;
}

void syscall_self_test(void)
{
    preempt_disable();
    static const uint8_t blob[] = {
        0xb8, 0x01, 0x00, 0x00, 0x00, 0x0f, 0x05, 0xb8, 0x00, 0x00, 0x00, 0x00,
        0xbf, 0x2a, 0x00, 0x00, 0x00, 0x0f, 0x05
    };
    struct process *parent = proc_create(NULL);
    struct process *child = proc_create(parent);
    uint64_t stack = pmm_alloc();
    if (!parent || !child || !stack || !map_blob(child, blob, sizeof(blob)))
        panic("syscall: setup failed");
    uint64_t back = cpu_read_cr3() & ~0xfffULL;
    vmm_switch(child->cr3);
    if (!vmm_map(USER_STACK_TOP - PAGE_SIZE, stack, 1, VMM_USER | VMM_WRITE | VMM_NX))
        panic("syscall: stack map failed");
    vmm_switch(back);
    proc_enter(child, USER_IMAGE, USER_STACK_TOP - 16);
    if (child->alive || child->exit_status != 42)
        panic("syscall: user exit status %d", child->exit_status);
    proc_switch(parent);
    int64_t waited = syscall_invoke(SYS_WAIT, 0, 0, 0, 0, 0, 0);
    if (waited != 42)
        panic("syscall: wait returned %ld", (long)waited);
    pr_info("test", "syscall: wait 42");

    proc_switch(child);
    int64_t pid = syscall_invoke(SYS_GETPID, 0, 0, 0, 0, 0, 0);
    if (pid != (int64_t)child->pid)
        panic("syscall: getpid mismatch");
    pr_info("test", "syscall: getpid ok");
    if (syscall_invoke(999, 0, 0, 0, 0, 0, 0) != -ENOSYS)
        panic("syscall: bad number accepted");
    pr_info("test", "syscall: bad number");
    if (syscall_invoke(SYS_WRITE, 1, 0xffffffff80000000ULL, 1, 0, 0, 0) != -EFAULT)
        panic("syscall: kernel pointer accepted");
    pr_info("test", "syscall: bad pointer");
    if (syscall_invoke(SYS_WRITE, 1, USER_IMAGE, 1ULL << 40, 0, 0, 0) != -EINVAL)
        panic("syscall: huge size accepted");
    pr_info("test", "syscall: huge size");
    int64_t up = syscall_invoke(SYS_UPTIME, 0, 0, 0, 0, 0, 0);
    if (up < 0)
        panic("syscall: uptime");
    int64_t brk = syscall_invoke(SYS_SBRK, 0, 0, 0, 0, 0, 0);
    if (brk < 0)
        panic("syscall: sbrk");
    if (syscall_invoke(SYS_SPAWN, 0, 0, 0, 0, 0, 0) != -ENOENT)
        panic("syscall: spawn");
    pr_info("test", "syscall: all checks passed");
    preempt_enable();
}
