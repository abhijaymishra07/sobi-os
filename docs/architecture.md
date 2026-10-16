# Architecture

## Overview

SOBI OS is a monolithic x86-64 kernel written in freestanding C with a small
amount of GNU assembler. Milestone 1 delivers the foundation: a bootable image,
a correct bootloader handoff, serial logging, panic handling, and a test harness.
Nothing else is present yet; see `status.md`.

```
+--------------------------------------------------------------+
| QEMU q35 machine, SeaBIOS or OVMF firmware                   |
|  +--------------------------------------------------------+  |
|  | Limine 12.9.3 (Limine Boot Protocol, base revision 6)  |  |
|  |  loads ELF, builds page tables + HHDM, enters long mode|  |
|  +---------------------------+----------------------------+  |
|                              v                               |
|  kernel/arch/x86_64/entry.S  _start                          |
|     own 16 KiB stack in .bss, rbp = 0, call kmain            |
|                              v                               |
|  kernel/main.c               kmain                           |
|     serial_init -> banner -> boot_collect_info               |
|     -> boot_dump_info -> "boot: ok" -> test_run | halt       |
+--------------------------------------------------------------+
```

## Modules (Milestone 1)

| Path | Responsibility |
|---|---|
| `kernel/arch/x86_64/entry.S` | Entry point. Switches from Limine's stack to the kernel's own stack, zeroes `rbp`, clears `DF`, calls `kmain`, halts if it returns. |
| `kernel/arch/x86_64/cpu.c`, `include/sobi/cpu.h` | `cli`/`sti`/`hlt`, control-register reads, `cpu_halt_forever`. |
| `kernel/boot/limine_requests.c` | Request delimiters, base-revision tag, and the request objects the bootloader fills in. |
| `kernel/boot/boot.c`, `include/sobi/boot.h` | Validates Limine responses, exposes `boot_info`, dumps the memory map. |
| `kernel/drivers/serial.c` | 16550 UART on COM1 (`0x3F8`), polled, 115200 8N1, loopback self-test. |
| `kernel/debug/log.c` | `kvsnprintf`, `kprintf`, `klog` with levels and subsystem tags. |
| `kernel/debug/panic.c` | `panic()`: disables interrupts, prints location and reason, dumps `rsp/rflags/cr2/cr3`, halts. Nested-panic guard. |
| `kernel/debug/test.c` | Compiled only when `SOBI_TEST` is defined. Parses the kernel command line and drives test scenarios, exits QEMU through `isa-debug-exit`. |
| `kernel/lib/string.c` | `memset/memcpy/memmove/memcmp/strlen/strnlen/strcmp/strncmp`. gcc may emit calls to the first three even in freestanding mode. |

## Address space at entry

Limine hands over with 4-level paging enabled and these mappings (base
revision 6):

- The kernel image at `0xffffffff80000000` (virtual), text R+X, rodata R, data
  RW, loaded physically wherever the bootloader chose (observed `0xff7a000` under
  SeaBIOS, `0xe299000` under OVMF). The physical base comes from the
  Executable Address response and is never assumed.
- A Higher Half Direct Map (HHDM) at the offset reported by the HHDM response
  (observed `0xffff800000000000`). Only Usable, Bootloader-reclaimable,
  Executable+Modules, Framebuffer, ACPI, and Reserved-mapped regions are in the
  HHDM. MMIO such as the local APIC is not.
- Limine's own GDT, page tables, stack, and response structures live in
  Bootloader-reclaimable memory and remain in use until the kernel replaces
  them (see `memory-rules.md`).

Kernel linker layout (`linker.ld`): three `PT_LOAD` program headers, each
page aligned, in the order text, rodata (+ `.eh_frame`), data (+ Limine
request sections + `.bss`). Linker symbols `__text_start/_end`,
`__rodata_start/_end`, `__data_start/_end`, `__bss_start/_end`,
`__kernel_start/_end` delimit them and are printed at boot.

## Build configurations

| `BUILD=` | Flags | Purpose |
|---|---|---|
| `debug` (default) | `-O0 -g3` | Development and GDB. |
| `release` | `-O2 -g` | Optimised; symbols kept for post-mortem. |
| `test` | debug flags + `-DSOBI_TEST=1` | Adds `kernel/debug/test.c` and the QEMU exit-port writes. Used by `make test`. |

All configurations share the freestanding flag set described in `build.md`.

## Conventions

- No comments in source files; design rationale lives in `docs/`.
- `-Werror` with an extended warning set; the linker runs with
  `--fatal-warnings`.
- Nothing in the kernel trusts a bootloader value without checking for its
  presence; missing responses are fatal with a specific panic message.
- Milestone evidence (commands, output, exit codes) is appended to
  `milestones.md` before the milestone is committed.
