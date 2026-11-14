#include <sobi/sync.h>
#include <sobi/sched.h>
#include <sobi/cpu.h>
#include <sobi/panic.h>
#include <sobi/log.h>
#include <sobi/compiler.h>

#define HELD_MAX 8
#define HOLD_SLOTS 64

static uint32_t next_rank = 1;
static int spl_depth;
static uint64_t spl_flags;

#ifndef SOBI_BUILD_RELEASE
static bool check_only;

struct hold {
    struct task *task;
    uint32_t rank[HELD_MAX];
    int n;
};

static struct hold holds[HOLD_SLOTS];

static struct hold *hold_slot(struct task *task, bool create)
{
    struct hold *free_slot = NULL;
    for (int i = 0; i < HOLD_SLOTS; i++) {
        if (holds[i].task == task)
            return &holds[i];
        if (!holds[i].task && !free_slot)
            free_slot = &holds[i];
    }
    if (!create)
        return NULL;
    if (!free_slot)
        return NULL;
    free_slot->task = task;
    free_slot->n = 0;
    return free_slot;
}

static bool order_push(uint32_t rank)
{
    struct task *task;
    struct hold *slot;
    if (rank == 0)
        return true;
    task = sched_current();
    if (!task)
        return true;
    slot = hold_slot(task, false);
    if (slot) {
        for (int i = 0; i < slot->n; i++) {
            if (rank <= slot->rank[i]) {
                if (check_only)
                    return false;
                panic("lock order");
            }
        }
        if (slot->n >= HELD_MAX) {
            if (check_only)
                return false;
            panic("lock order");
        }
    } else {
        slot = hold_slot(task, true);
        if (!slot) {
            if (check_only)
                return false;
            panic("lock order");
        }
    }
    slot->rank[slot->n++] = rank;
    return true;
}

static bool order_allows(uint32_t rank)
{
    struct task *task;
    struct hold *slot;
    if (rank == 0)
        return true;
    task = sched_current();
    if (!task)
        return true;
    slot = hold_slot(task, false);
    if (!slot)
        return true;
    for (int i = 0; i < slot->n; i++) {
        if (rank <= slot->rank[i]) {
            if (check_only)
                return false;
            panic("lock order");
        }
    }
    if (slot->n >= HELD_MAX) {
        if (check_only)
            return false;
        panic("lock order");
    }
    return true;
}

static void order_pop(uint32_t rank)
{
    struct task *task = sched_current();
    struct hold *slot;
    if (!task || rank == 0)
        return;
    slot = hold_slot(task, false);
    if (!slot)
        return;
    for (int i = slot->n - 1; i >= 0; i--) {
        if (slot->rank[i] != rank)
            continue;
        for (int j = i; j < slot->n - 1; j++)
            slot->rank[j] = slot->rank[j + 1];
        slot->n--;
        break;
    }
    if (slot->n == 0)
        slot->task = NULL;
}
#else
static bool order_push(uint32_t rank)
{
    (void)rank;
    return true;
}

static bool order_allows(uint32_t rank)
{
    (void)rank;
    return true;
}

static void order_pop(uint32_t rank)
{
    (void)rank;
}
#endif

static uint32_t claim_rank(void)
{
    uint32_t rank = next_rank++;
    if (rank == 0)
        rank = next_rank++;
    return rank;
}

static uint32_t xchg32(volatile uint32_t *p, uint32_t v)
{
    __asm__ volatile("xchgl %0, %1" : "+r"(v), "+m"(*p) : : "memory");
    return v;
}

static void spl_enter(void)
{
    uint64_t flags = cpu_read_flags();
    cpu_cli();
    if (spl_depth == 0)
        spl_flags = flags;
    spl_depth++;
}

static void spl_leave(void)
{
    if (spl_depth > 0)
        spl_depth--;
    if (spl_depth == 0 && (spl_flags & 0x200))
        cpu_sti();
}

static void q_add(struct task **head, struct task **tail, struct task *task)
{
    task->wait_next = NULL;
    if (*tail)
        (*tail)->wait_next = task;
    else
        *head = task;
    *tail = task;
}

static struct task *q_pop(struct task **head, struct task **tail)
{
    struct task *task = *head;
    if (!task)
        return NULL;
    *head = task->wait_next;
    if (!*head)
        *tail = NULL;
    task->wait_next = NULL;
    return task;
}

static struct task *q_take_all(struct task **head, struct task **tail)
{
    struct task *task = *head;
    *head = NULL;
    *tail = NULL;
    return task;
}

static void q_wake_list(struct task *task)
{
    while (task) {
        struct task *next = task->wait_next;
        task->wait_next = NULL;
        sched_wakeup(task);
        task = next;
    }
}

void spin_init(spinlock_t *lock)
{
    lock->locked = 0;
    lock->rank = claim_rank();
}

bool spin_lock(spinlock_t *lock)
{
    spl_enter();
    if (!order_push(lock->rank)) {
        spl_leave();
        return false;
    }
    while (xchg32(&lock->locked, 1) != 0)
        cpu_pause();
    return true;
}

void spin_unlock(spinlock_t *lock)
{
    order_pop(lock->rank);
    lock->locked = 0;
    barrier();
    spl_leave();
}

void mutex_init(mutex_t *mtx)
{
    mtx->locked = 0;
    mtx->rank = claim_rank();
    mtx->owner = NULL;
    mtx->head = NULL;
    mtx->tail = NULL;
}

bool mutex_lock(mutex_t *mtx)
{
    for (;;) {
        cpu_cli();
        if (!mtx->locked) {
            if (!order_push(mtx->rank)) {
                cpu_sti();
                return false;
            }
            mtx->locked = 1;
            mtx->owner = sched_current();
            cpu_sti();
            return true;
        }
        if (!order_allows(mtx->rank)) {
            cpu_sti();
            return false;
        }
        q_add(&mtx->head, &mtx->tail, sched_current());
        sched_block();
    }
}

void mutex_unlock(mutex_t *mtx)
{
    struct task *task;
    cpu_cli();
    order_pop(mtx->rank);
    mtx->locked = 0;
    mtx->owner = NULL;
    task = q_pop(&mtx->head, &mtx->tail);
    cpu_sti();
    if (task)
        sched_wakeup(task);
}

void sem_init(semaphore_t *sem, int count)
{
    sem->count = count;
    sem->rank = claim_rank();
    sem->head = NULL;
    sem->tail = NULL;
}

void sem_wait(semaphore_t *sem)
{
    cpu_cli();
    if (sem->count > 0) {
        sem->count--;
        cpu_sti();
        return;
    }
    q_add(&sem->head, &sem->tail, sched_current());
    sched_block();
}

void sem_post(semaphore_t *sem)
{
    struct task *task;
    cpu_cli();
    task = q_pop(&sem->head, &sem->tail);
    if (!task)
        sem->count++;
    cpu_sti();
    if (task)
        sched_wakeup(task);
}

void wq_init(waitqueue_t *wq)
{
    wq->rank = claim_rank();
    wq->head = NULL;
    wq->tail = NULL;
}

void wq_wait(waitqueue_t *wq)
{
    cpu_cli();
    q_add(&wq->head, &wq->tail, sched_current());
    sched_block();
}

void wq_wake_one(waitqueue_t *wq)
{
    struct task *task;
    cpu_cli();
    task = q_pop(&wq->head, &wq->tail);
    cpu_sti();
    if (task)
        sched_wakeup(task);
}

void wq_wake_all(waitqueue_t *wq)
{
    struct task *task;
    cpu_cli();
    task = q_take_all(&wq->head, &wq->tail);
    cpu_sti();
    q_wake_list(task);
}

void rw_init(rwlock_t *rw)
{
    rw->readers = 0;
    rw->writer = 0;
    rw->rank = claim_rank();
    rw->rhead = NULL;
    rw->rtail = NULL;
    rw->whead = NULL;
    rw->wtail = NULL;
}

bool rw_read_lock(rwlock_t *rw)
{
    for (;;) {
        cpu_cli();
        if (!rw->writer && !rw->whead) {
            if (!order_push(rw->rank)) {
                cpu_sti();
                return false;
            }
            rw->readers++;
            cpu_sti();
            return true;
        }
        if (!order_allows(rw->rank)) {
            cpu_sti();
            return false;
        }
        q_add(&rw->rhead, &rw->rtail, sched_current());
        sched_block();
    }
}

void rw_read_unlock(rwlock_t *rw)
{
    struct task *task = NULL;
    cpu_cli();
    order_pop(rw->rank);
    if (rw->readers > 0)
        rw->readers--;
    if (rw->readers == 0)
        task = q_pop(&rw->whead, &rw->wtail);
    cpu_sti();
    if (task)
        sched_wakeup(task);
}

bool rw_write_lock(rwlock_t *rw)
{
    for (;;) {
        cpu_cli();
        if (!rw->writer && rw->readers == 0) {
            if (!order_push(rw->rank)) {
                cpu_sti();
                return false;
            }
            rw->writer = 1;
            cpu_sti();
            return true;
        }
        if (!order_allows(rw->rank)) {
            cpu_sti();
            return false;
        }
        q_add(&rw->whead, &rw->wtail, sched_current());
        sched_block();
    }
}

void rw_write_unlock(rwlock_t *rw)
{
    struct task *writer;
    struct task *readers;
    cpu_cli();
    order_pop(rw->rank);
    rw->writer = 0;
    writer = q_pop(&rw->whead, &rw->wtail);
    readers = NULL;
    if (!writer)
        readers = q_take_all(&rw->rhead, &rw->rtail);
    cpu_sti();
    if (writer)
        sched_wakeup(writer);
    else
        q_wake_list(readers);
}

void sync_set_check_only(bool enable)
{
#ifndef SOBI_BUILD_RELEASE
    check_only = enable;
#else
    (void)enable;
#endif
}

bool sync_order_violation(void)
{
    spinlock_t low;
    spinlock_t high;
    bool bad;
    spin_init(&low);
    spin_init(&high);
    sync_set_check_only(true);
    if (!spin_lock(&high)) {
        sync_set_check_only(false);
        return false;
    }
    bad = !spin_lock(&low);
    if (!bad)
        spin_unlock(&low);
    spin_unlock(&high);
    sync_set_check_only(false);
    return bad;
}

static semaphore_t fair_sem;
static volatile int wake_n;
static volatile int wake_id[2];

static void sem_worker(void *arg)
{
    int id = (int)(uint64_t)arg;
    int slot;
    sem_wait(&fair_sem);
    cpu_cli();
    slot = wake_n++;
    cpu_sti();
    if (slot >= 0 && slot < 2)
        wake_id[slot] = id;
}

static void wait_blocked(struct task *task)
{
    for (int i = 0; i < 200000; i++) {
        if (sched_task_state(task) == TASK_BLOCKED)
            return;
        sched_yield();
    }
    panic("sync: waiter did not block");
}

void sync_self_test(void)
{
    struct task *a;
    struct task *b;
    mutex_t mtx;
    waitqueue_t wq;
    rwlock_t rw;
    bool order_bad;

    order_bad = sync_order_violation();
#ifndef SOBI_BUILD_RELEASE
    if (!order_bad)
        panic("sync: order not detected");
#endif
    if (order_bad)
        pr_info("sync", "order violation");

    mutex_init(&mtx);
    if (!mutex_lock(&mtx))
        panic("sync: mutex");
    mutex_unlock(&mtx);
    wq_init(&wq);
    rw_init(&rw);
    if (!rw_write_lock(&rw))
        panic("sync: rw");
    rw_write_unlock(&rw);
    if (!rw_read_lock(&rw))
        panic("sync: rlock");
    rw_read_unlock(&rw);

    wake_n = 0;
    wake_id[0] = -1;
    wake_id[1] = -1;
    sem_init(&fair_sem, 0);
    a = task_create(sem_worker, (void *)(uint64_t)0, "sem0");
    wait_blocked(a);
    b = task_create(sem_worker, (void *)(uint64_t)1, "sem1");
    wait_blocked(b);
    sem_post(&fair_sem);
    sem_post(&fair_sem);
    for (int i = 0; i < 200000 && wake_n < 2; i++)
        sched_yield();
    if (wake_n != 2 || wake_id[0] != 0 || wake_id[1] != 1)
        panic("sync: semaphore order");
    pr_info("sync", "semaphore ok");
    pr_info("sync", "all checks passed");
}
