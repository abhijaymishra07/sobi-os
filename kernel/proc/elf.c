#include <sobi/elf.h>
#include <sobi/cpu.h>
#include <sobi/proc.h>
#include <sobi/vmm.h>
#include <sobi/pmm.h>
#include <sobi/boot.h>
#include <sobi/kheap.h>
#include <sobi/sched.h>
#include <sobi/string.h>
#include <sobi/log.h>
#include <sobi/panic.h>
#include <sobi/uaccess.h>
#include <sobi/vfs.h>
#include <sobi/kheap.h>
#include <sobi/timer.h>
#include <sobi/syscall.h>
#include <sobi/pipe.h>

#define ET_EXEC 2
#define EM_X86_64 62
#define PT_LOAD 1
#define PF_X 1
#define PF_W 2
#define PH_MAX 16

struct elf64_ehdr {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
};

struct elf64_phdr {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
};

struct spawn_job {
    const uint8_t *image;
    size_t len;
    struct process *proc;
    int error;
    bool owned;
};

extern const unsigned char hello_elf[];
extern const unsigned int hello_elf_len;

static bool range_overlap(uint64_t a, uint64_t asz, uint64_t b, uint64_t bsz)
{
    if (asz == 0 || bsz == 0)
        return false;
    return a < b + bsz && b < a + asz;
}

int elf_validate(const void *image, size_t len)
{
    if (!image || len < sizeof(struct elf64_ehdr))
        return -ENOEXEC;
    const struct elf64_ehdr *eh = image;
    if (eh->ident[0] != 0x7f || eh->ident[1] != 'E' || eh->ident[2] != 'L' || eh->ident[3] != 'F')
        return -ENOEXEC;
    if (eh->ident[4] != 2 || eh->ident[5] != 1)
        return -ENOEXEC;
    if (eh->type != ET_EXEC || eh->machine != EM_X86_64)
        return -ENOEXEC;
    if (eh->phentsize != sizeof(struct elf64_phdr) || eh->phnum == 0 || eh->phnum > PH_MAX)
        return -ENOEXEC;
    if (eh->phoff > len || eh->phnum * sizeof(struct elf64_phdr) > len - eh->phoff)
        return -ENOEXEC;
    if (eh->entry < 0x1000 || eh->entry >= USER_LIMIT)
        return -ENOEXEC;
    const struct elf64_phdr *ph = (const struct elf64_phdr *)((const uint8_t *)image + eh->phoff);
    uint64_t bases[PH_MAX];
    uint64_t sizes[PH_MAX];
    int loads = 0;
    for (uint16_t i = 0; i < eh->phnum; i++) {
        if (ph[i].type != PT_LOAD)
            continue;
        if (ph[i].memsz < ph[i].filesz)
            return -ENOEXEC;
        if (ph[i].offset > len || ph[i].filesz > len - ph[i].offset)
            return -ENOEXEC;
        if (ph[i].vaddr < 0x1000 || ph[i].vaddr >= USER_LIMIT)
            return -ENOEXEC;
        if (ph[i].memsz > USER_LIMIT - ph[i].vaddr)
            return -ENOEXEC;
        for (int j = 0; j < loads; j++) {
            if (range_overlap(ph[i].vaddr, ph[i].memsz, bases[j], sizes[j]))
                return -ENOEXEC;
        }
        bases[loads] = ph[i].vaddr;
        sizes[loads] = ph[i].memsz;
        loads++;
    }
    if (loads == 0)
        return -ENOEXEC;
    return 0;
}

static int map_segment(struct process *proc, const uint8_t *image, const struct elf64_phdr *ph)
{
    uint64_t map_base = ph->vaddr & ~0xfffULL;
    uint64_t page_off = ph->vaddr & 0xfffULL;
    uint64_t span = page_off + ph->memsz;
    uint64_t pages = (span + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t flags = VMM_USER | VMM_NX;
    if (ph->flags & PF_W)
        flags = VMM_USER | VMM_WRITE | VMM_NX;
    else if (ph->flags & PF_X)
        flags = VMM_USER;
    for (uint64_t i = 0; i < pages; i++) {
        uint64_t frame = pmm_alloc();
        if (!frame)
            return -ENOMEM;
        memset((void *)(boot_info->hhdm_offset + frame), 0, PAGE_SIZE);
        if (!vmm_map(map_base + i * PAGE_SIZE, frame, 1, flags))
            return -ENOMEM;
    }
    if (ph->filesz) {
        uint64_t dest_phys = 0;
        if (!vmm_translate(ph->vaddr, &dest_phys, NULL))
            return -ENOEXEC;
        memcpy((void *)(boot_info->hhdm_offset + dest_phys), image + ph->offset, (size_t)ph->filesz);
    }
    (void)proc;
    return 0;
}

static void spawn_finish(struct process *proc, int failure)
{
    if (failure) {
        proc->alive = false;
        proc->exit_status = failure;
    }
    if (proc->out_pipe) {
        pipe_close(proc->out_pipe, PIPE_END_WRITE);
        proc->out_pipe = NULL;
    }
    if (proc->in_pipe) {
        pipe_close(proc->in_pipe, PIPE_END_READ);
        proc->in_pipe = NULL;
    }
}

static void spawn_entry(void *arg)
{
    struct spawn_job *job = arg;
    struct process *proc = job->proc;
    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)job->image;
    const struct elf64_phdr *ph = (const struct elf64_phdr *)(job->image + eh->phoff);
    vmm_switch(proc->cr3);
    for (uint16_t i = 0; i < eh->phnum; i++) {
        if (ph[i].type != PT_LOAD)
            continue;
        if (map_segment(proc, job->image, &ph[i]) != 0) {
            job->error = -ENOEXEC;
            vmm_switch(vmm_kernel_cr3());
            spawn_finish(proc, 126);
            task_exit(1);
        }
    }
    uint64_t stack = pmm_alloc();
    if (!stack || !vmm_map(USER_STACK_TOP - PAGE_SIZE, stack, 1, VMM_USER | VMM_WRITE | VMM_NX)) {
        job->error = -ENOMEM;
        vmm_switch(vmm_kernel_cr3());
        spawn_finish(proc, 126);
        task_exit(1);
    }
    uint64_t entry = eh->entry;
    if (job->owned)
        kfree((void *)job->image);
    proc_enter(proc, entry, USER_STACK_TOP - 16);
    vmm_switch(vmm_kernel_cr3());
    spawn_finish(proc, 0);
    kfree(job);
    task_exit(0);
}

int elf_spawn(const void *image, size_t len, struct process **out)
{
    int rc = elf_validate(image, len);
    if (rc != 0)
        return rc;
    struct process *proc = proc_create(proc_current());
    if (!proc)
        return -ENOMEM;
    struct spawn_job *job = kmalloc(sizeof(*job));
    if (!job)
        return -ENOMEM;
    job->image = image;
    job->len = len;
    job->proc = proc;
    job->error = 0;
    job->owned = false;
    if (!task_create(spawn_entry, job, "elf"))
        return -ENOMEM;
    if (out)
        *out = proc;
    return (int)proc->pid;
}

static int run_image(struct spawn_job *job)
{
    struct process *proc = job->proc;
    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)job->image;
    const struct elf64_phdr *ph = (const struct elf64_phdr *)(job->image + eh->phoff);
    uint64_t entry = eh->entry;
    uint64_t back = cpu_read_cr3() & ~0xfffULL;
    vmm_switch(proc->cr3);
    for (uint16_t i = 0; i < eh->phnum; i++) {
        if (ph[i].type != PT_LOAD)
            continue;
        if (map_segment(proc, job->image, &ph[i]) != 0) {
            vmm_switch(back);
            return -ENOEXEC;
        }
    }
    uint64_t stack = pmm_alloc();
    if (!stack || !vmm_map(USER_STACK_TOP - PAGE_SIZE, stack, 1, VMM_USER | VMM_WRITE | VMM_NX)) {
        vmm_switch(back);
        return -ENOMEM;
    }
    if (job->owned)
        kfree((void *)job->image);
    proc_enter(proc, entry, USER_STACK_TOP - 16);
    vmm_switch(back);
    return 0;
}

int elf_spawn_path(const char *path, struct process **out)
{
    void *image = NULL;
    size_t len = 0;
    int rc = vfs_slurp(path, &image, &len);
    if (rc != 0)
        return rc;
    rc = elf_validate(image, len);
    if (rc != 0) {
        kfree(image);
        return rc;
    }
    struct process *proc = proc_create(proc_current());
    if (!proc) {
        kfree(image);
        return -ENOMEM;
    }
    struct spawn_job job = {
        .image = image,
        .len = len,
        .proc = proc,
        .error = 0,
        .owned = true,
    };
    rc = run_image(&job);
    if (rc != 0)
        return rc;
    if (out)
        *out = proc;
    return (int)proc->pid;
}

void elf_self_test(void)
{
    if (elf_validate(hello_elf, hello_elf_len) != 0)
        panic("elf: hello image rejected");
    uint8_t bad_magic[64];
    memset(bad_magic, 0, sizeof(bad_magic));
    if (elf_validate(bad_magic, sizeof(bad_magic)) != -ENOEXEC)
        panic("elf: bad magic accepted");
    uint8_t truncated[sizeof(struct elf64_ehdr)];
    memcpy(truncated, hello_elf, sizeof(truncated));
    if (elf_validate(truncated, sizeof(truncated)) != -ENOEXEC)
        panic("elf: truncated accepted");
    uint8_t overlap[sizeof(struct elf64_ehdr) + 2 * sizeof(struct elf64_phdr)];
    memset(overlap, 0, sizeof(overlap));
    struct elf64_ehdr *oe = (struct elf64_ehdr *)overlap;
    memcpy(oe->ident, hello_elf, 16);
    oe->type = ET_EXEC;
    oe->machine = EM_X86_64;
    oe->version = 1;
    oe->entry = 0x400000;
    oe->phoff = sizeof(*oe);
    oe->ehsize = sizeof(*oe);
    oe->phentsize = sizeof(struct elf64_phdr);
    oe->phnum = 2;
    struct elf64_phdr *op = (struct elf64_phdr *)(overlap + sizeof(*oe));
    op[0].type = PT_LOAD;
    op[0].flags = PF_X;
    op[0].vaddr = 0x400000;
    op[0].memsz = 0x1000;
    op[1].type = PT_LOAD;
    op[1].flags = PF_W;
    op[1].vaddr = 0x400800;
    op[1].memsz = 0x1000;
    if (elf_validate(overlap, sizeof(overlap)) != -ENOEXEC)
        panic("elf: overlap accepted");
    struct elf64_ehdr *copy = kmalloc(hello_elf_len);
    memcpy(copy, hello_elf, hello_elf_len);
    copy->entry = 0xffffffff80000000ULL;
    if (elf_validate(copy, hello_elf_len) != -ENOEXEC)
        panic("elf: kernel entry accepted");
    kfree(copy);
    pr_info("test", "elf: malformed rejected");

    struct kheap_stats before = kheap_stats();
    if (elf_spawn(bad_magic, sizeof(bad_magic), NULL) != -ENOEXEC)
        panic("elf: spawn of bad image");
    if (kheap_stats().live_bytes != before.live_bytes)
        panic("elf: validate leaked");

    struct process *child = NULL;
    int pid = elf_spawn(hello_elf, hello_elf_len, &child);
    if (pid <= 0 || !child)
        panic("elf: spawn hello");
    uint64_t start = timer_ticks();
    while (child->alive && timer_ticks() < start + 200)
        sched_yield();
    if (child->alive || child->exit_status != 0)
        panic("elf: hello status %d", child->exit_status);
    pr_info("test", "elf: hello exit 0");
    pr_info("test", "elf: all checks passed");
}

void shell_self_test(void)
{
    const char *script = "echo hi\nls\ncat\ncat | upper\nexit\n";
    for (const char *p = script; *p; p++)
        syscall_push_input(*p);
    struct process *sh = NULL;
    int pid = elf_spawn_path("/bin/sh", &sh);
    if (pid <= 0 || !sh)
        panic("shell: spawn failed");
    uint64_t start = timer_ticks();
    while (sh->alive && timer_ticks() < start + 300)
        sched_yield();
    if (sh->alive)
        panic("shell: did not exit");
    if (sh->exit_status != 0)
        panic("shell: exit status %d", sh->exit_status);
    pr_info("test", "shell: script done");
}

void multi_self_test(void)
{
    void *image = NULL;
    size_t len = 0;
    if (vfs_slurp("/bin/spin", &image, &len) != 0)
        panic("multi: /bin/spin missing");
    struct process *a = NULL, *b = NULL;
    if (elf_spawn(image, len, &a) <= 0 || elf_spawn(image, len, &b) <= 0)
        panic("multi: spawn");
    uint64_t start = timer_ticks();
    while ((a->alive || b->alive) && timer_ticks() < start + 500)
        sched_yield();
    if (a->alive || b->alive)
        panic("multi: processes did not finish");
    if (a->exit_status != 0 || b->exit_status != 0)
        panic("multi: exit status %d %d", a->exit_status, b->exit_status);
    pr_info("test", "multi: two processes ok");
}

int elf_spawn_path_async(const char *path, struct pipe *in, struct pipe *out, struct process **out_proc)
{
    void *image = NULL;
    size_t len = 0;
    int rc = vfs_slurp(path, &image, &len);
    if (rc != 0)
        return rc;
    rc = elf_validate(image, len);
    struct process *proc = rc == 0 ? proc_create(proc_current()) : NULL;
    struct spawn_job *job = proc ? kmalloc(sizeof(*job)) : NULL;
    if (!job) {
        kfree(image);
        return rc != 0 ? rc : -ENOMEM;
    }
    proc->in_pipe = in;
    proc->out_pipe = out;
    job->image = image;
    job->len = len;
    job->proc = proc;
    job->error = 0;
    job->owned = true;
    if (!task_create(spawn_entry, job, "elf")) {
        kfree(job);
        kfree(image);
        spawn_finish(proc, 126);
        return -ENOMEM;
    }
    if (out_proc)
        *out_proc = proc;
    return (int)proc->pid;
}
