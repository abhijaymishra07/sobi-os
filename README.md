# SOBI OS

An independently developed, modular, 64-bit operating system for x86-64, built
from the ground up in C and assembly. SOBI OS is not derived from Linux, xv6, or
any other existing kernel. It boots through the Limine bootloader protocol and
runs under QEMU (BIOS and UEFI).

Current version: 0.6.0 (Milestone 6: preemptive tasks).

## Status

Every roadmap item is marked with exactly one of: **implemented**, **tested**,
**partially implemented**, **planned**. "Tested" means an automated test in
`tests/` exercises it and `make test` currently passes. See `docs/status.md`
for the full list.

| Capability | Status |
|---|---|
| Reproducible freestanding build (debug / release / test) | tested |
| Limine boot protocol (base revision 6), hybrid BIOS+UEFI ISO | tested |
| Higher-half kernel at `0xffffffff80000000`, own 16 KiB stack | tested |
| Serial console (COM1) and `kprintf` logging | tested |
| Panic with file/line/function and CPU register dump | tested |
| Boot-time memory map parsing and dump | tested |
| ELF layout verification (`scripts/verify-elf.sh`) | tested |
| QEMU test harness with explicit exit-status protocol | tested |
| GDT, TSS, IDT, exception handlers, fault fixups | tested |
| Physical frame allocator | tested |
| Kernel page tables, W^X, NX, bootloader reclaim | tested |
| Slab kernel heap | tested |
| LAPIC timer at 100 Hz, interrupts enabled | tested |
| Preemptive round-robin tasks | tested |
| Preemptive scheduler | planned (M6) |
| User mode, processes, system calls | planned (M7-M8) |
| Initramfs, VFS, ELF loader, init, shell | planned (M9-M11) |
| Persistent filesystem, IPC, networking, SMP | planned (roadmap) |

## Architecture

```
QEMU (SeaBIOS or OVMF)
  -> Limine 12.9.3
       long mode, 4-level paging, HHDM, kernel mapped in the higher half
  -> kernel/arch/x86_64/entry.S   _start: switch to kernel stack, call kmain
  -> kernel/main.c                kmain: serial, banner, memmap, GDT/IDT, PMM, own page tables, heap
```

Source layout:

```
boot/limine.conf          bootloader configuration placed on the ISO
include/limine.h          vendored Limine protocol header (0BSD)
include/sobi/             kernel headers
kernel/arch/x86_64/       entry, GDT, IDT, exceptions
kernel/mm/                pmm.c, vmm.c, kheap.c
kernel/boot/              limine_requests.c, boot.c
kernel/drivers/serial.c   16550 UART driver
kernel/debug/             log.c (kprintf), panic.c, test.c (test build only)
kernel/lib/string.c       mem*/str* for the freestanding environment
kernel/main.c             kmain
scripts/                  fetch-limine.sh, mkiso.sh, run-qemu.sh, gdb.sh, verify-elf.sh
tests/                    boot, exc, pmm, vmm suites
docs/                     design and milestone documentation
linker.ld                 higher-half layout with explicit program headers
Makefile                  BUILD=debug|release|test
```

Details: `docs/architecture.md`, `docs/boot-process.md`.

## Technology stack

- C (gnu11) and x86-64 assembly (GNU `as` via `gcc -c`), freestanding, no libc.
- GNU Make, GNU binutils, gcc 14 (host compiler in freestanding mode).
- Limine 12.9.3 bootloader (downloaded and sha256-verified at build time).
- QEMU 9.2 (TCG), GDB 16, xorriso.

## Build

Prerequisites on Ubuntu/Debian:

```
sudo apt install build-essential qemu-system-x86 gdb xorriso curl
# optional, for make run-uefi:
sudo apt install ovmf
```

```
make                      # debug kernel: build/debug/sobi-kernel.elf
make BUILD=release        # optimised kernel
make iso                  # bootable ISO: build/debug/sobi.iso
make run                  # boot the ISO in QEMU, serial on stdio (Ctrl-C to quit)
make run-uefi             # same, firmware = OVMF
make gdb                  # QEMU with -s -S plus GDB attached, breakpoint on kmain
make test                 # verify-elf + QEMU boot tests on the test build
make clean / distclean    # distclean also removes third_party/limine
```

The first `make iso` runs `scripts/fetch-limine.sh`, which downloads the pinned
Limine release, verifies its sha256, and builds the host `limine` tool. For an
offline build, set `LIMINE_ARCHIVE=/path/to/limine-binary.tar.xz`.

## Demonstration (actual run, `make test`, BIOS firmware)

```
  ____   ___  ____ ___    ___  ____
 / ___| / _ \| __ )_ _|  / _ \/ ___|
 \___ \| | | |  _ \| |  | | | \___ \
  ___) | |_| | |_) | |  | |_| |___) |
 |____/ \___/|____/___|  \___/|____/

SOBI OS 0.4.0 (x86-64, test build)

[info ] boot: bootloader: Limine 12.9.3 (limine protocol base revision 6)
[info ] boot: stack size request honoured
[info ] boot: cmdline: "test=boot"
[info ] boot: hhdm offset: 0xffff800000000000
[info ] boot: kernel phys 0xff7a000 virt 0xffffffff80000000 size 32 KiB
[info ] boot:   .text   0xffffffff80000000-0xffffffff80001f00
[info ] boot:   .rodata 0xffffffff80002000-0xffffffff80002e0c
[info ] boot:   .data   0xffffffff80003000-0xffffffff80007250
[info ] boot:   .bss    0xffffffff800031f0-0xffffffff80007250
[info ] boot: memory map (18 entries):
[info ] boot:   0x0000000000001000-0x0000000000074000        460 KiB bootloader-reclaimable
[info ] boot:   0x0000000000074000-0x000000000009f000        172 KiB usable
...
[info ] boot:   0x0000000000100000-0x000000000ff46000     260376 KiB usable
[info ] boot:   0x000000000ff7a000-0x000000000ff82000         32 KiB executable+modules
...
[info ] boot: memory: 260548 KiB usable, 1040 KiB bootloader-reclaimable, 32 KiB executable+modules
[info ] boot: boot: ok
[info ] test: cmdline: "test=boot"
[info ] test: boot test complete
test: exit 16
```

Panic path (`cmdline: test=panic`):

```
!!! KERNEL PANIC !!!
at kernel/debug/test.c:40 in test_run()
reason: deliberate test panic (value=42)
rsp=0xffffffff80007160 rflags=0x0000000000000086 cr2=0x0000000000000000 cr3=0x000000000ff70000
system halted
test: exit 17
```

## Test status

`make test` on 2026-10-17 (QEMU 9.2.1, TCG):

```
verify-elf: PASS
PASS  boot, panic, unknown-selector, exc, df
PASS  pmm, pmm-dbl, pmm-range
PASS  vmm, kheap, kheap-dbl, timer
test: all suites passed
```

The same test ISO also boots under OVMF (UEFI) with exit 33. See
`docs/testing.md` for the exit-status protocol.

## Roadmap

See `docs/roadmap.md`. M1 through M6 are done. Next is M7 (user mode).

## Known limitations

See `docs/limitations.md`. The kernel owns its GDT, IDT, page tables, frame
allocator, and heap, and it preempts kernel threads from the LAPIC timer.
There is no user mode yet.

## License

MIT (see `LICENSE`). `include/limine.h` is 0BSD, see `NOTICE`.
