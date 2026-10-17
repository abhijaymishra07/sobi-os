#ifndef SOBI_SYSCALL_H
#define SOBI_SYSCALL_H

#include <sobi/types.h>

#define SYS_EXIT   0
#define SYS_GETPID 1
#define SYS_GETPPID 2
#define SYS_SPAWN  3
#define SYS_WAIT   4
#define SYS_READ   5
#define SYS_WRITE  6
#define SYS_SLEEP  7
#define SYS_UPTIME 8
#define SYS_YIELD  9
#define SYS_SBRK   10
#define SYS_OPEN   11
#define SYS_CLOSE  12
#define SYS_LSEEK  13
#define SYS_MKDIR  14
#define SYS_UNLINK 15
#define SYS_STAT   16
#define SYS_MAX    17

void syscall_init(void);
int64_t syscall_invoke(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
void syscall_push_input(char c);
void syscall_self_test(void);

#endif
