#include <sobi/pipe.h>
#include <sobi/sched.h>
#include <sobi/kheap.h>
#include <sobi/string.h>
#include <sobi/cpu.h>
#include <sobi/panic.h>
#include <sobi/log.h>

struct pipe {
    uint8_t data[PIPE_SIZE];
    uint32_t rpos;
    uint32_t wpos;
    uint32_t used;
    int readers;
    int writers;
    struct task *rhead;
    struct task *rtail;
    struct task *whead;
    struct task *wtail;
};

static void q_add(struct task **head, struct task **tail, struct task *task)
{
    task->wait_next = NULL;
    if (*tail)
        (*tail)->wait_next = task;
    else
        *head = task;
    *tail = task;
}

static struct task *q_take(struct task **head, struct task **tail)
{
    struct task *task = *head;
    *head = NULL;
    *tail = NULL;
    return task;
}

static void wake_list(struct task *task)
{
    while (task) {
        struct task *next = task->wait_next;
        task->wait_next = NULL;
        sched_wakeup(task);
        task = next;
    }
}

struct pipe *pipe_alloc(void)
{
    struct pipe *p = kmalloc(sizeof(*p));
    if (!p)
        return NULL;
    memset(p, 0, sizeof(*p));
    p->readers = 1;
    p->writers = 1;
    return p;
}

int pipe_read(struct pipe *p, void *buf, size_t len)
{
    uint8_t *out = buf;
    size_t got = 0;
    if (!p || !buf)
        return -EINVAL;
    if (len == 0)
        return 0;
    while (got == 0) {
        struct task *sleeper;
        cpu_cli();
        if (p->used == 0) {
            if (p->writers == 0) {
                cpu_sti();
                return 0;
            }
            q_add(&p->rhead, &p->rtail, sched_current());
            sched_block();
            continue;
        }
        while (got < len && p->used > 0) {
            out[got++] = p->data[p->rpos];
            p->rpos = (p->rpos + 1) % PIPE_SIZE;
            p->used--;
        }
        sleeper = q_take(&p->whead, &p->wtail);
        cpu_sti();
        wake_list(sleeper);
    }
    return (int)got;
}

int pipe_write(struct pipe *p, const void *buf, size_t len)
{
    const uint8_t *in = buf;
    size_t put = 0;
    if (!p || !buf)
        return -EINVAL;
    if (len == 0)
        return 0;
    while (put < len) {
        struct task *sleeper;
        cpu_cli();
        if (p->readers == 0) {
            cpu_sti();
            return put > 0 ? (int)put : -EPIPE;
        }
        if (p->used == PIPE_SIZE) {
            q_add(&p->whead, &p->wtail, sched_current());
            sched_block();
            continue;
        }
        while (put < len && p->used < PIPE_SIZE) {
            p->data[p->wpos] = in[put++];
            p->wpos = (p->wpos + 1) % PIPE_SIZE;
            p->used++;
        }
        sleeper = q_take(&p->rhead, &p->rtail);
        cpu_sti();
        wake_list(sleeper);
    }
    return (int)put;
}

void pipe_close(struct pipe *p, int end)
{
    struct task *readers;
    struct task *writers;
    int dead;
    if (!p)
        return;
    cpu_cli();
    if (end == PIPE_END_WRITE) {
        if (p->writers > 0)
            p->writers--;
    } else if (p->readers > 0) {
        p->readers--;
    }
    readers = q_take(&p->rhead, &p->rtail);
    writers = q_take(&p->whead, &p->wtail);
    dead = p->readers == 0 && p->writers == 0;
    cpu_sti();
    wake_list(readers);
    wake_list(writers);
    if (dead)
        kfree(p);
}

static struct pipe *test_pipe;
static volatile int echo_ok;
static volatile int eof_ok;
static volatile int reader_done;

static void pipe_writer(void *arg)
{
    static const char msg[] = "hello-pipe";
    (void)arg;
    if (pipe_write(test_pipe, msg, sizeof(msg) - 1) != (int)(sizeof(msg) - 1))
        panic("pipe: write");
    pipe_close(test_pipe, PIPE_END_WRITE);
}

static void pipe_reader(void *arg)
{
    char buf[16];
    int n;
    int n2;
    (void)arg;
    memset(buf, 0, sizeof(buf));
    n = pipe_read(test_pipe, buf, sizeof(buf));
    if (n == 10 && memcmp(buf, "hello-pipe", 10) == 0)
        echo_ok = 1;
    n2 = pipe_read(test_pipe, buf, sizeof(buf));
    if (n2 == 0)
        eof_ok = 1;
    pipe_close(test_pipe, PIPE_END_READ);
    reader_done = 1;
}

void pipe_self_test(void)
{
    struct pipe *closed;
    int rc;
    echo_ok = 0;
    eof_ok = 0;
    reader_done = 0;
    test_pipe = pipe_alloc();
    if (!test_pipe)
        panic("pipe: alloc");
    task_create(pipe_reader, NULL, "pipe-rd");
    task_create(pipe_writer, NULL, "pipe-wr");
    for (int i = 0; i < 400000 && !reader_done; i++)
        sched_yield();
    if (!echo_ok)
        panic("pipe: echo");
    pr_info("pipe", "echo ok");
    if (!eof_ok)
        panic("pipe: eof");
    pr_info("pipe", "eof ok");
    closed = pipe_alloc();
    if (!closed)
        panic("pipe: alloc");
    pipe_close(closed, PIPE_END_READ);
    rc = pipe_write(closed, "x", 1);
    pipe_close(closed, PIPE_END_WRITE);
    if (rc != -EPIPE)
        panic("pipe: epipe");
    pr_info("pipe", "all checks passed");
}
