# System calls

User programs enter the kernel with the `syscall` instruction. `syscall` does
not switch stacks and this kernel does not use `swapgs`. `syscall_entry` saves
the user stack pointer, loads `syscall_kernel_rsp` (the top of the current
kernel stack), builds a register frame, and calls `syscall_dispatch`. The
value returned in `rax` is the syscall result. `sysretq` restores the user
instruction pointer from `rcx` and `rflags` from `r11`, using the selectors
programmed into `STAR`.

`syscall_init` enables the mechanism. It sets `EFER.SCE`, `LSTAR` to
`syscall_entry`, `SFMASK` to `0x47700` (clears IF, DF, TF, and the other
status bits in that mask), and `STAR` as follows:

| Field | Value | Effect |
|---|---|---|
| `STAR[47:32]` | `0x08` | `syscall` loads CS `0x08` and SS `0x10` |
| `STAR[63:48]` | `0x10` | `sysretq` loads user CS `0x23` and SS `0x1b` |

`int 0x80` from user mode is delivered through the IDT. The process trap path
counts it and returns to the kernel; it is not the system-call ABI.

## Registers

| Register | On entry | On return |
|---|---|---|
| `rax` | syscall number | return value, or a negative errno |
| `rdi` | arg0 | clobbered |
| `rsi` | arg1 | clobbered |
| `rdx` | arg2 | clobbered |
| `r10` | arg3 | clobbered |
| `r8` | arg4 | clobbered |
| `r9` | arg5 | clobbered |
| `rcx` | destroyed by `syscall` (user RIP is saved here by the CPU) | clobbered |
| `r11` | destroyed by `syscall` (user RFLAGS are saved here by the CPU) | clobbered |
| `rbx`, `rbp`, `r12`–`r15` | preserved | preserved |

Arguments that are pointers must be canonical user addresses, in the range
`[0, 0x0000800000000000)`. `copy_from_user`, `copy_to_user`, and
`strncpy_from_user` reject a kernel address, a non-canonical address, or a
range that would overflow that window with `-EFAULT` before they touch the
bytes. A range that passes the check but is not mapped also returns `-EFAULT`.

## Errors

These positive numbers are returned negated (`-ENOENT` is `-2`):

| Name | Value | When |
|---|---|---|
| `ENOENT` | 2 | `spawn` has no loader yet, or the path is empty |
| `EBADF` | 9 | file descriptor is not valid for that call |
| `ECHILD` | 10 | `wait` has no matching child |
| `EFAULT` | 14 | bad user pointer or a fault while copying |
| `EINVAL` | 22 | size or break adjustment is not allowed |
| `ENOSYS` | 38 | unknown syscall number |

## Calls

### `exit` (0)

`exit(int status)` does not return. The status is stored on the process, the
process becomes a zombie, a parent blocked in `wait` is woken, and the kernel
task exits.

### `getpid` (1)

Returns the caller’s pid.

### `getppid` (2)

Returns the parent’s pid, or 0 if there is no parent.

### `spawn` (3)

`spawn(const char *path)`. Loads the ELF at `path` from the VFS and runs it as
a child process. The child runs to completion inside the parent's system call
(nested entry into user mode), then `spawn` returns the child's pid; `wait`
then returns its exit status. A bad pointer returns `-EFAULT`, a missing file
`-ENOENT`, and a malformed executable `-ENOEXEC`.

### `wait` (4)

`wait(uint64_t pid, int *status)`. `pid` 0 or -1 waits for any child; any other
value waits for that pid. The call blocks until the child has exited, then
returns the child’s exit status. If `status` is not NULL, the 32-bit status is
also stored there. No matching child returns `-ECHILD`.

### `read` (5)

`read(int fd, void *buf, size_t len)`.

- fd 0 reads the process's input pipe if it is the right side of a pipeline.
  Otherwise it returns one byte typed on the PS/2 keyboard or received on the
  serial port, or `-EAGAIN` when none is queued.
- fd 3 and above read from an open VFS file and advance its offset. Reading at
  end of file returns 0.

A bad buffer returns `-EFAULT`. A count above 4096 returns `-EINVAL`. Any
other fd returns `-EBADF`.

### `write` (6)

`write(int fd, const void *buf, size_t len)`.

- fd 1 writes to the process's output pipe if it is the left side of a
  pipeline; otherwise fd 1 and fd 2 go to the serial port and the framebuffer
  console. The console understands `ESC[2J` (clear) and `ESC[H` (home).
- fd 3 and above write to an open VFS file (tmpfs only; the initramfs is
  read-only and returns `-EROFS`).

The return value is the number of bytes written. A bad buffer returns
`-EFAULT`. A count above 4096 returns `-EINVAL`. Any other fd returns `-EBADF`.

### `sleep` (7)

`sleep(uint64_t ms)` blocks the caller for at least `ms` milliseconds and
returns 0. A zero delay yields the CPU.

### `uptime` (8)

Returns milliseconds since the timer was started.

### `yield` (9)

Yields the remainder of the current time slice and returns 0.

### `sbrk` (10)

`sbrk(int64_t increment)` moves the process break and returns the previous
break. An increment of 0 returns the current break without mapping. A positive
increment maps new user read/write pages; a negative increment unmaps pages
that are no longer covered. The break starts at `0x400000`. An adjustment that
would leave the user image or run into the user stack returns `-EINVAL`.

### `open` (11)

`open(const char *path, int flags)` returns a file descriptor (3 and above).
Flags: `O_RDONLY`, `O_WRONLY`, `O_RDWR`, `O_CREAT` (create in a writable
filesystem), `O_DIRECTORY` (required to open a directory for `readdir`).
Returns `-ENOENT`, `-ENOTDIR`, `-EROFS`, `-EINVAL`, `-EMFILE` (16 descriptors
per process) or `-EFAULT`.

### `close` (12)

`close(int fd)` releases a descriptor. Closing twice returns `-EBADF`.

### `lseek` (13)

`lseek(int fd, int64_t off, int whence)` with `SEEK_SET`, `SEEK_CUR` or
`SEEK_END`. Returns the new offset.

### `mkdir` (14) and `unlink` (15)

`mkdir(const char *path)` and `unlink(const char *path)` operate on tmpfs
(`/tmp`). The read-only initramfs returns `-EROFS`; removing a non-empty
directory returns `-ENOTEMPTY`.

### `stat` (16)

`stat(const char *path, struct vfs_stat *st)` fills `{ mode, ino, size }`.
`mode` carries `S_IFDIR` (`0x4000`) or `S_IFREG` (`0x8000`).

### `readdir` (17)

`readdir(int fd, struct vfs_dirent *ent)` returns the next entry of a
directory opened with `O_DIRECTORY` and advances the position. The entry is
`{ char name[100]; uint32_t ino; uint32_t mode; }`. Returns 0 per entry and a
negative value at the end of the directory.

### `pipeline` (18)

`pipeline(const char *a, const char *b)` runs `a | b`: both programs start as
concurrent processes, `a`'s fd 1 feeds a kernel pipe and `b`'s fd 0 drains it.
Each end of the pipe is closed when its process exits, so `b` sees end of file
after `a` finishes. Returns `b`'s exit status, or a negative errno if either
program cannot be loaded.
