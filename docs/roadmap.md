# Roadmap

Each milestone is implemented, built, booted, tested, documented in
`milestones.md`, and committed before the next one starts. Order follows
verified dependencies, not feature names.

## Initial delivery

| # | Milestone | Depends on | Acceptance |
|---|---|---|---|
| M1 | Bootable foundation | - | banner over serial, `boot: ok`, panic path, `make test` green (done) |
| M2 | CPU tables and exceptions | M1 | GDT/TSS/IDT installed; `int3`, `ud2`, `#DE`, `#PF`, `#GP` handled through fixups; `#DF` panics on IST1; PICs remapped and masked; `IF` still 0 |
| M3 | Physical memory | M2 | bitmap allocator over the Limine map with the rules in `memory-rules.md`; alloc/free/exhaustion/double-free/reserved tests |
| M4 | Virtual memory and heap | M3 | done: kernel PML4, W^X, map/unmap/invlpg, heap, 1132 KiB reclaimed |
| M5 | Interrupt controller and timer | M4 | done: xAPIC, PIT calibration, 100 Hz (50 ticks / 500 ms), `sti` |
| M6 | Tasks and scheduling | M5 | done: preemption, sleep order, wake-once, exit reclaims stack and task |
| M7 | User mode and processes | M6 | done: iretq, int 0x80, isolation, copy_from_user |
| M8 | System calls | M7 | `syscall`/`sysret`, numbered table, argument validation; exit, getpid, spawn, wait, read, write, sleep |
| M9 | Initramfs and VFS | M8 | ustar module from Limine, read-only VFS, fd table, open/read/close/readdir/stat |
| M10 | ELF loader and user runtime | M9 | ELF64 validation, segment mapping, user stack and argv, minimal libc, programs from the initramfs; malformed-ELF tests |
| M11 | init and shell | M10 | init spawns the shell; help/echo/clear/pwd/ls/cat/ps/meminfo/uptime backed by real syscalls |
| M12 | Test consolidation and docs | M11 | regression suite, fault-injection tests, every doc refreshed from actual runs, status list finalised |

Process creation is `spawn` first; `fork` waits for copy-on-write.

## After the initial delivery

- Block device (virtio-blk or AHCI) and one persistent filesystem with
  tests for persistence across reboot.
- Pipes, then message passing and shared memory; synchronisation primitives
  with lock-order checking.
- Keyboard and framebuffer console through the I/O APIC.
- `fork` with copy-on-write, demand paging, memory-mapped files.
- Networking: virtio-net, Ethernet, ARP, IPv4, ICMP, UDP, TCP, sockets.
- Observability: event log, syscall and context-switch counters, page-fault
  counters, scheduler latency, exported through a controlled interface and a
  shell command.
- Multicore: AP startup, per-CPU data, IPIs, TLB shootdown, SMP scheduling.
- Security audit and fault injection; documented threat model.
- Benchmarks: syscall overhead, context switch, allocation, process launch.
