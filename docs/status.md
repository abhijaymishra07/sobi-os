# Capability status

Each item is exactly one of:

- **implemented**: code exists and has been observed working in QEMU.
- **tested**: implemented and covered by an automated test run by `make test`.
- **partially implemented**: some of the item works and the gap is named.
- **planned**: not started; see `roadmap.md`.

Last updated: Milestone 2 (2026-10-17).

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

## Planned (initial delivery)

| Capability | Status | Milestone |
|---|---|---|
| GDT, TSS, IDT, exception handlers, controlled fault tests | tested | M2 |
| Physical frame allocator with reservation rules | planned | M3 |
| Kernel page tables, map/unmap, MMIO mapping, kernel heap | planned | M4 |
| LAPIC timer, interrupts enabled, uptime | planned | M5 |
| Tasks, context switch, round-robin, preemption | planned | M6 |
| User mode, processes, address-space isolation | planned | M7 |
| System calls | planned | M8 |
| Initramfs and read-only VFS | planned | M9 |
| ELF loader, user runtime, user programs | planned | M10 |
| init process and shell | planned | M11 |
| Test consolidation, docs refresh | planned | M12 |

## Planned (later roadmap)

Persistent filesystem, pipes and IPC, networking, `fork` with copy-on-write,
multicore, observability layer, security hardening, performance measurement:
all **planned**.
