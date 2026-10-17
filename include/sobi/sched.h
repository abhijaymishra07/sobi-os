#ifndef SOBI_SCHED_H
#define SOBI_SCHED_H

#include <sobi/types.h>

enum task_state {
    TASK_READY = 1,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_SLEEPING,
    TASK_DEAD
};

struct task {
    uint64_t id;
    int state;
    uint64_t kstack_top;
    uint64_t saved_rsp;
    uint64_t cr3;
    int priority;
    int timeslice;
    int exit_code;
    struct task *wait_next;
    struct task *rq_next;
    uint64_t sleep_until;
    uint64_t stack_pages;
    bool own_stack;
    void (*entry)(void *);
    void *arg;
    char name[16];
};

void sched_init(void);
struct task *task_create(void (*entry)(void *), void *arg, const char *name);
void sched_yield(void);
void sched_block(void);
void sched_wakeup(struct task *task);
void sched_sleep_ms(uint64_t ms);
void task_exit(int code) __attribute__((noreturn));
void sched_on_tick(void);
struct task *sched_current(void);
int sched_task_state(struct task *task);
uint64_t sched_switch_count(void);
void preempt_disable(void);
void preempt_enable(void);

#endif
