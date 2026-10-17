# Capability status

Each item is exactly one of:

- **implemented**: code exists and has been observed working in QEMU.
- **tested**: implemented and covered by an automated test run by `make test`.
- **partially implemented**: some of the item works and the gap is named.
- **planned**: not started; see `roadmap.md`.

Last updated: Milestone 9 (2026-10-17).

## Foundation

| Capability | Status | Evidence |
|---|---|---|
| Freestanding build, debug/release/test configurations | tested | `make test` builds `BUILD=test`; debug and release built and booted manually |
| Limine 12.9.3 pinned and sha256-verified, host tool built | implemented | `scripts/fetch-limine.sh` |
| Limine protocol base revision 6 honoured | tested | serial text asserted by `boot` scenario |
| Request delimiters and sections placed correctly | tested | `verify-elf.sh` |
| Higher-half ELF with three page-aligned `PT_LOAD`s | tested | `verify-elf.sh` |
| Kernel-owned 16 KiB stack, aligned entry into `kmain` | tested | `verify-elf.sh`; GDB `rsp` observation |
| Hybrid BIOS/UEFI ISO | implemented | boots under SeaBIOS (automated) and OVMF (manual) |
| Serial console COM1, polled | tested | all scenarios read serial output |
| `kprintf` / `klog` with levels | tested | serial text asserted |
| `panic()` with location, reason, `rsp/rflags/cr2/cr3` | tested | `panic` scenario |
| `ASSERT()` | implemented | used in `boot_collect_info`; no test triggers it yet |
| Bootloader response validation, memory map dump | tested | `boot` scenario asserts HHDM and memmap lines |
| QEMU test harness with exit-status protocol | tested | `tests/lib/run_qemu_test.sh` |
| GDB workflow (`make gdb`) | implemented | manual session recorded in `milestones.md` |
| GDT, TSS (IST1 for #DF, IST2 for NMI/#MC), kernel stack | tested | boot log `gdt loaded`; double-fault test |
| IDT, 256 interrupt gates, assembly stubs, register dump | tested | `exc` and `df` scenarios |
| Symbolised RIP in exception dumps | tested | `exc: ksym ok` |
| Fault fixup (`fault_expect`) for controlled tests | tested | ud2, #DE, #PF (cr2), #GP |
| 8259 PIC remapped to 0x20/0x28 and fully masked | tested | boot log `pic remapped`, masks 0xff/0xff |
| Physical frame allocator (bitmap, page 0 excluded, bootloader memory held) | tested | `pmm` scenario: round trip, contig, exhaustion of 65104 pages, reserved page held |
| Memmap and bootloader strings copied into kernel memory | tested | `pmm` init copies 18 regions before allocation |
| Double-free and out-of-range detection | tested | `pmm-dbl` and `pmm-range` panic |
| Kernel page tables, W^X, NX, global pages, `cr3` switch | tested | `vmm` scenario: `cr3 0x76000 nx on` |
| `vmm_map` / `unmap` / `protect` / `translate`, `invlpg` | tested | execute, NX `#PF`, read-only store, unmap then load |
| HHDM reproduced for the same region types Limine maps | implemented | boot continues after the `cr3` switch; HHDM alias used by the vmm test |
| MMIO window and kernel-stack allocator with guard pages | tested | `vmm_alloc_stack` guard faults; boot stacks' guard page faults |
| Slab kernel heap (16..2048) plus big allocations | tested | `kheap`: alignment, reuse, stress, `live 0` |
| Heap red zone and double-free detection | tested | `kheap-dbl` panics `double free` |
| Bootloader memory reclaimed after the `cr3` switch | tested | `reclaimed 1132 KiB`; no `BOOTLOADER_RECLAIMABLE` entries remain |
| xAPIC mapped, spurious vector `0xFF`, LVTs masked | tested | `xapic at 0xfee00000`, spurious count 0 |
| LAPIC timer 100 Hz calibrated from PIT channel 2 | tested | `50 ticks in 500 ms`, 625598 counts/tick |
| IRQ dispatch and the first `sti` | tested | `timer` scenario runs with `IF` set |
| `uptime_ms` and polled `sleep_poll` | tested | uptime moved `500 -> 600` across a 100 ms sleep |
| Kernel tasks, round-robin, idle, exit reaper | tested | `sched` scenario, resources restored |
| Timer preemption without an explicit yield | tested | `sched: preempt ok switches 14` |
| `sched_block` / `sched_wakeup` and sleep wheel | tested | wake-once and sleep order `20,40,80` |
| Per-process address space cloned from the kernel half | tested | `user: isolation ok` |
| Ring-3 entry and `int 0x80` return | tested | `user: trap ok` |
| User fault kills the process instead of panicking | tested | `killed pid 2 vec 14` |
| `copy_from_user` range checks and fault fixup | tested | kernel and unmapped pointers return `-EFAULT` |
| `syscall` / `sysret` and the core call table | tested | `syscall: wait 42`, bad pointer, bad number, huge size |
| ustar initramfs, VFS, tmpfs, file descriptors | tested | `vfs` scenario: lookup, EOF, tmpfs, fd limit |

## Planned (initial delivery)

| Capability | Status | Milestone |
|---|---|---|
| GDT, TSS, IDT, exception handlers, controlled fault tests | tested | M2 |
| Physical frame allocator with reservation rules | tested | M3 |
| Kernel page tables, map/unmap, MMIO mapping, kernel heap | tested | M4 |
| LAPIC timer, interrupts enabled, uptime | tested | M5 |
| Tasks, context switch, round-robin, preemption | tested | M6 |
| User mode, processes, address-space isolation | tested | M7 |
| System calls | tested | M8 |
| Initramfs and read-only VFS | tested | M9 |
| ELF loader, user runtime, user programs | planned | M10 |
| init process and shell | planned | M11 |
| Test consolidation, docs refresh | planned | M12 |

## Planned (later roadmap)

Persistent filesystem, pipes and IPC, networking, `fork` with copy-on-write,
multicore, observability layer, security hardening, performance measurement:
all **planned**.
