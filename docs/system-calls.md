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

`spawn(const char *path, uint64_t argv, uint64_t envp)`. `path` is copied out
of user memory. Until an executable loader exists, a well-formed path returns
`-ENOENT`. A bad pointer returns `-EFAULT`.

### `wait` (4)

`wait(uint64_t pid, int *status)`. `pid` 0 or -1 waits for any child; any other
value waits for that pid. The call blocks until the child has exited, then
returns the child’s exit status. If `status` is not NULL, the 32-bit status is
also stored there. No matching child returns `-ECHILD`.

### `read` (5)

`read(int fd, void *buf, size_t len)`. Only fd 0 is accepted. The call blocks
until at least one byte is available from the kernel input queue or the serial
port, copies those bytes to `buf`, and returns the count. A null read returns
0. A bad buffer returns `-EFAULT`. Any other fd returns `-EBADF`. Counts above
1 MiB return `-EINVAL`.

### `write` (6)

`write(int fd, const void *buf, size_t len)`. Fd 1 and fd 2 copy from the user
buffer and send the bytes to the serial port. The return value is the number of
bytes written. A bad buffer returns `-EFAULT`. A count above 1 MiB returns
`-EINVAL`. Any other fd returns `-EBADF`.

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
