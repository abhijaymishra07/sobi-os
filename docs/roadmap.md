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
| M8 | System calls | M7 | done: syscall/sysret, wait 42, pointer and size checks |
| M9 | Initramfs and VFS | M8 | done: ustar, `/` and `/tmp`, fd table, file syscalls |
| M10 | ELF loader and user runtime | M9 | done: hello exits 0, malformed ELFs rejected |
| M11 | init and shell | M10 | done: init spawns the shell; help/echo/clear/pwd/uptime built-ins, ls/cat/upper programs, `a \| b` pipelines, interactive on the framebuffer (`ps`/`meminfo` still missing) |
| M12 | Test consolidation and docs | M11 | regression suite, fault-injection tests, every doc refreshed from actual runs, status list finalised |

Process creation is `spawn` first; `fork` waits for copy-on-write.

## After the initial delivery

| Item | Status |
|---|---|
| Block device and a persistent filesystem, tested across reboot | done: virtio-blk + sobifs; not yet mounted into the VFS |
| Pipes | done (two-stage pipelines); message passing and shared memory planned |
| Synchronisation primitives | done (spinlock, mutex, semaphore); lock-order checking planned |
| Keyboard and framebuffer console through the I/O APIC | done |
| Observability: event log, syscall/context-switch/page-fault counters, scheduler latency | done in the kernel; shell command planned |
| Networking: virtio-net, Ethernet, ARP, IPv4, ICMP, UDP, TCP | done; socket syscalls planned |
| Mount sobifs at `/disk`, shell redirection, file utilities | next |
| Interrupt-driven virtio, asynchronous `spawn`, process memory reclamation | planned |
| `fork` with copy-on-write, demand paging, memory-mapped files | planned |
| Multicore: AP startup, per-CPU data, IPIs, TLB shootdown, SMP scheduling | planned |
| Security audit and fault injection; documented threat model | planned |
| Benchmarks: syscall overhead, context switch, allocation, process launch | planned |
