#ifndef SOBI_PROC_H
#define SOBI_PROC_H

#include <sobi/types.h>

#define USER_IMAGE 0x400000ULL
#define USER_STACK_TOP 0x00007ffffffff000ULL
#define USER_LIMIT 0x0000800000000000ULL

struct process {
    uint64_t pid;
    uint64_t ppid;
    struct process *parent;
    struct process *child;
    struct process *sibling;
    uint64_t cr3;
    int exit_status;
    bool alive;
    void *fds[16];
    uint64_t brk;
};

void proc_init(void);
struct process *proc_create(struct process *parent);
void proc_enter(struct process *proc, uint64_t rip, uint64_t user_rsp);
void proc_switch(struct process *proc);
struct process *proc_current(void);
void proc_self_test(void);

#endif
