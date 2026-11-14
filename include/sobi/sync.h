#ifndef SOBI_SYNC_H
#define SOBI_SYNC_H

#include <sobi/types.h>

struct task;

typedef struct spinlock {
    volatile uint32_t locked;
    uint32_t rank;
} spinlock_t;

typedef struct mutex {
    volatile uint32_t locked;
    uint32_t rank;
    struct task *owner;
    struct task *head;
    struct task *tail;
} mutex_t;

typedef struct semaphore {
    int count;
    uint32_t rank;
    struct task *head;
    struct task *tail;
} semaphore_t;

typedef struct waitqueue {
    uint32_t rank;
    struct task *head;
    struct task *tail;
} waitqueue_t;

typedef struct rwlock {
    int readers;
    int writer;
    uint32_t rank;
    struct task *rhead;
    struct task *rtail;
    struct task *whead;
    struct task *wtail;
} rwlock_t;

void spin_init(spinlock_t *lock);
bool spin_lock(spinlock_t *lock);
void spin_unlock(spinlock_t *lock);

void mutex_init(mutex_t *mtx);
bool mutex_lock(mutex_t *mtx);
void mutex_unlock(mutex_t *mtx);

void sem_init(semaphore_t *sem, int count);
void sem_wait(semaphore_t *sem);
void sem_post(semaphore_t *sem);

void wq_init(waitqueue_t *wq);
void wq_wait(waitqueue_t *wq);
void wq_wake_one(waitqueue_t *wq);
void wq_wake_all(waitqueue_t *wq);

void rw_init(rwlock_t *rw);
bool rw_read_lock(rwlock_t *rw);
void rw_read_unlock(rwlock_t *rw);
bool rw_write_lock(rwlock_t *rw);
void rw_write_unlock(rwlock_t *rw);

void sync_set_check_only(bool enable);
bool sync_order_violation(void);
void sync_self_test(void);

#endif
