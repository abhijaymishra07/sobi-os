# Changelog

Versions follow the milestone number (`0.<milestone>.<patch>`). Each
milestone is documented with commands and observed output in
[`docs/milestones.md`](docs/milestones.md).

## [0.11.0] - 2026-11-14

### Added
- `init` and an interactive shell with `help`, `echo`, `pwd`, `uptime`,
  `clear`, `exit`, external commands from `/bin`, and `a | b` pipelines.
- User programs `ls` (real directory listings), `cat`, `upper`, `spin`;
  `stat`, `readdir` and `pipeline` system calls.
- Concurrent user processes: per-task syscall stack, user return pointer,
  TSS `rsp0` and `cr3` are switched with every task.
- Framebuffer console with an 8x16 VGA font and minimal ANSI (clear, home);
  I/O APIC; PS/2 keyboard on IRQ 1 with shift; non-test builds boot straight
  into the shell (`make run-gui`).
- PCI enumeration with BAR sizing, a block layer with a write-through sector
  cache, and a polled virtio-blk driver.
- sobifs, a journalled filesystem with replay, persisting across reboots on a
  virtio-blk disk.
- TCP/IP stack (ARP, IPv4, ICMP, UDP, TCP, DNS parsing) with loopback and a
  legacy virtio-net driver tested against host echo servers.
- Trace ring buffer hooked into the scheduler, syscalls and user page faults,
  with a scheduling-latency histogram.
- Spinlocks, mutexes, semaphores and blocking pipes.
- GitHub Actions CI and tagged releases with a bootable ISO.

### Fixed
- Nested `spawn` corrupted the parent's callee-saved registers.
- A stale "syscall stopped" flag unwound the parent after a child exited.
- Returning from a child switched to the kernel page tables instead of the
  parent's, killing the shell.
- A missed edge-triggered keyboard IRQ could leave input permanently stuck.
- `tcp_connect` discarded sockets whose handshake was still in progress.

## [0.10.0] - 2026-10-17
### Added
- ELF64 loader with validation, user runtime (crt0, syscall stubs), `hello`.

## [0.9.0] - 2026-10-17
### Added
- ustar initramfs at `/`, writable tmpfs at `/tmp`, VFS and file descriptors.

## [0.8.0] - 2026-10-17
### Added
- `syscall`/`sysret` entry and the core system call table.

## [0.7.0] - 2026-10-17
### Added
- Ring 3, per-process address spaces, user faults kill only the process.

## [0.6.0] - 2026-10-17
### Added
- Kernel tasks, round-robin preemptive scheduling, sleep and wakeup.

## [0.5.0] - 2026-10-17
### Added
- xAPIC, LAPIC timer at 100 Hz calibrated against the PIT, interrupts on.

## [0.4.0] - 2026-10-17
### Added
- Kernel page tables with W^X and NX, MMIO and stack windows, slab heap.

## [0.3.0] - 2026-10-17
### Added
- Bitmap physical frame allocator with reservation rules.

## [0.2.0] - 2026-10-17
### Added
- GDT, TSS with IST stacks, IDT, exception handlers and fault fixups.

## [0.1.0] - 2026-10-17
### Added
- Limine boot, serial console, logging, panic, QEMU test harness.

[0.11.0]: https://github.com/abhijaymishra07/sobi-os/releases/tag/v0.11.0
