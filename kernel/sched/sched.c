#include <sobi/sched.h>
#include <sobi/kheap.h>
#include <sobi/vmm.h>
#include <sobi/pmm.h>
#include <sobi/timer.h>
#include <sobi/cpu.h>
#include <sobi/interrupt.h>
#include <sobi/log.h>
#include <sobi/panic.h>
#include <sobi/string.h>

#define QUANTUM 5
#define STACK_PAGES 4
#define WHEEL 16

extern void context_switch(uint64_t *old_rsp, uint64_t new_rsp);
extern char boot_stack_top[];

static struct task *current;
static struct task *idle_task;
static struct task *ready_head;
static struct task *ready_tail;
static struct task *zombies;
static struct task *wheel[WHEEL];
static uint64_t next_id = 1;
static uint64_t switches;
static int preempt_count;
static bool online;

static void enqueue(struct task *task)
{
    task->rq_next = NULL;
    if (!ready_tail) {
        ready_head = ready_tail = task;
        return;
    }
    ready_tail->rq_next = task;
    ready_tail = task;
}

static struct task *dequeue(void)
{
    struct task *task = ready_head;
    if (!task)
        return NULL;
    ready_head = task->rq_next;
    if (!ready_head)
        ready_tail = NULL;
    task->rq_next = NULL;
    return task;
}

static void wheel_remove(struct task *task)
{
    uint64_t slot = task->sleep_until & (WHEEL - 1);
    struct task **pp = &wheel[slot];
    while (*pp) {
        if (*pp == task) {
            *pp = task->wait_next;
            task->wait_next = NULL;
            return;
        }
        pp = &(*pp)->wait_next;
    }
}

static void free_stack(struct task *task)
{
    if (!task->own_stack)
        return;
    uint64_t mapped = task->kstack_top - task->stack_pages * PAGE_SIZE;
    for (uint64_t i = 0; i < task->stack_pages; i++) {
        uint64_t virt = mapped + i * PAGE_SIZE;
        uint64_t phys = 0;
        if (!vmm_translate(virt, &phys, NULL))
            continue;
        vmm_unmap(virt, 1);
        pmm_free(phys & ~0xfffULL);
    }
    task->own_stack = false;
}

static void reap(void)
{
    while (zombies) {
        struct task *task = zombies;
        zombies = task->rq_next;
        free_stack(task);
        kfree(task);
    }
}

static void schedule_locked(void)
{
    struct task *prev = current;
    if (prev->state == TASK_RUNNING && prev != idle_task) {
        prev->state = TASK_READY;
        enqueue(prev);
    }
    struct task *next = dequeue();
    if (!next)
        next = idle_task;
    if (next == prev) {
        prev->state = TASK_RUNNING;
        prev->timeslice = QUANTUM;
        return;
    }
    next->state = TASK_RUNNING;
    next->timeslice = QUANTUM;
    current = next;
    tss_set_rsp0(next->kstack_top);
    if (next->cr3 != 0 && (cpu_read_cr3() & ~0xfffULL) != (next->cr3 & ~0xfffULL)) {
        __asm__ volatile("mov %0, %%cr3" : : "r"(next->cr3) : "memory");
    }
    switches++;
    context_switch(&prev->saved_rsp, next->saved_rsp);
    reap();
}

static void idle_loop(void *arg)
{
    (void)arg;
    for (;;) {
        cpu_hlt();
        sched_yield();
    }
}

static void task_start(void)
{
    struct task *self = current;
    cpu_sti();
    self->entry(self->arg);
    task_exit(0);
}

static struct task *alloc_task(void (*entry)(void *), void *arg, const char *name, bool stack)
{
    struct task *task = kzalloc(sizeof(*task));
    if (!task)
        panic("sched: no memory for task");
    task->id = next_id++;
    task->entry = entry;
    task->arg = arg;
    task->priority = 0;
    task->timeslice = QUANTUM;
    task->cr3 = cpu_read_cr3() & ~0xfffULL;
    task->state = TASK_READY;
    size_t n = 0;
    if (name) {
        while (name[n] && n < sizeof(task->name) - 1) {
            task->name[n] = name[n];
            n++;
        }
    }
    task->name[n] = 0;
    if (stack) {
        uint64_t top = vmm_alloc_stack(STACK_PAGES);
        if (top == 0)
            panic("sched: no stack");
        task->kstack_top = top;
        task->stack_pages = STACK_PAGES;
        task->own_stack = true;
        uint64_t *sp = (uint64_t *)(top - 64);
        sp[0] = sp[1] = sp[2] = sp[3] = sp[4] = sp[5] = 0;
        sp[6] = (uint64_t)task_start;
        task->saved_rsp = (uint64_t)sp;
    }
    return task;
}

void sched_init(void)
{
    cpu_cli();
    memset(wheel, 0, sizeof(wheel));
    idle_task = alloc_task(idle_loop, NULL, "idle", true);
    current = alloc_task(NULL, NULL, "boot", false);
    current->kstack_top = (uint64_t)boot_stack_top;
    current->state = TASK_RUNNING;
    current->own_stack = false;
    online = true;
    pr_info("sched", "online, boot task %lu, idle %lu", current->id, idle_task->id);
    cpu_sti();
}

struct task *task_create(void (*entry)(void *), void *arg, const char *name)
{
    cpu_cli();
    struct task *task = alloc_task(entry, arg, name, true);
    enqueue(task);
    cpu_sti();
    return task;
}

void sched_yield(void)
{
    cpu_cli();
    if (current && current != idle_task)
        current->timeslice = 0;
    if (online)
        schedule_locked();
    cpu_sti();
}

void sched_block(void)
{
    cpu_cli();
    current->state = TASK_BLOCKED;
    schedule_locked();
    cpu_sti();
}

void sched_wakeup(struct task *task)
{
    if (!task)
        return;
    cpu_cli();
    if (task->state == TASK_SLEEPING)
        wheel_remove(task);
    if (task->state == TASK_BLOCKED || task->state == TASK_SLEEPING) {
        task->state = TASK_READY;
        enqueue(task);
    }
    cpu_sti();
}

void sched_sleep_ms(uint64_t ms)
{
    if (ms == 0) {
        sched_yield();
        return;
    }
    cpu_cli();
    uint64_t ticks = (ms + 9) / 10;
    if (ticks == 0)
        ticks = 1;
    current->sleep_until = timer_ticks() + ticks;
    current->state = TASK_SLEEPING;
    uint64_t slot = current->sleep_until & (WHEEL - 1);
    current->wait_next = wheel[slot];
    wheel[slot] = current;
    schedule_locked();
    cpu_sti();
}

void task_exit(int code)
{
    cpu_cli();
    current->exit_code = code;
    current->state = TASK_DEAD;
    current->rq_next = zombies;
    zombies = current;
    schedule_locked();
    panic("sched: returned to dead task");
}

void sched_on_tick(void)
{
    if (!online || !current)
        return;
    bool woke = false;
    for (int i = 0; i < WHEEL; i++) {
        struct task **pp = &wheel[i];
        while (*pp) {
            struct task *task = *pp;
            if (task->sleep_until <= timer_ticks()) {
                *pp = task->wait_next;
                task->wait_next = NULL;
                task->state = TASK_READY;
                enqueue(task);
                woke = true;
            } else {
                pp = &task->wait_next;
            }
        }
    }
    if (preempt_count > 0)
        return;
    if (current == idle_task) {
        if (ready_head)
            schedule_locked();
        return;
    }
    if (current->timeslice > 0)
        current->timeslice--;
    if (current->timeslice == 0 || woke)
        schedule_locked();
}

struct task *sched_current(void)
{
    return current;
}

int sched_task_state(struct task *task)
{
    if (!task)
        return 0;
    return task->state;
}

uint64_t sched_switch_count(void)
{
    return switches;
}

void preempt_disable(void)
{
    preempt_count++;
}

void preempt_enable(void)
{
    if (preempt_count > 0)
        preempt_count--;
}
