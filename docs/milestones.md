# Milestone log

One entry per milestone, appended when the milestone is complete and before it
is committed. Each entry records what was built, the exact commands, what was
observed, and what remains.

## M1: Bootable foundation (2026-10-17)

### Objective

A reproducible freestanding build that produces a hybrid ISO, boots through
Limine in QEMU, prints a startup banner and the machine's memory map over
serial, has working panic/assert diagnostics, and is covered by an automated
test with an explicit pass/fail protocol.

### Changed files

45 files, 3202 insertions (1235 lines of C, assembly, linker script, and
Makefile; the rest is the vendored `limine.h`, scripts, and documentation).

```
Makefile linker.ld boot/limine.conf .gitignore LICENSE NOTICE README.md
include/limine.h
include/sobi/{assert,boot,compiler,cpu,io,log,panic,serial,string,test,types}.h
kernel/arch/x86_64/{entry.S,cpu.c}
kernel/boot/{limine_requests.c,boot.c}
kernel/drivers/serial.c
kernel/debug/{log.c,panic.c,test.c}
kernel/lib/string.c
kernel/main.c
scripts/{fetch-limine.sh,mkiso.sh,run-qemu.sh,gdb.sh,verify-elf.sh}
tests/boot_test.sh tests/lib/run_qemu_test.sh
docs/{architecture,boot-process,build,testing,memory-rules,interrupts-and-timer,status,roadmap,limitations,milestones}.md
```

### Commands

```
LIMINE_ARCHIVE=/path/limine-binary.tar.xz ./scripts/fetch-limine.sh   # or plain ./scripts/fetch-limine.sh
make BUILD=debug iso
make BUILD=release iso
make test
qemu-system-x86_64 -M q35 -m 256M -no-reboot -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
    -cdrom build/test/sobi.iso -display none -serial file:build/test/uefi.log -device isa-debug-exit,iobase=0xf4,iosize=0x04
make gdb
```

### Observed results

Fetch script: sha256 of `limine-binary.tar.xz` matched
`e02a473c6dc6df25159697f94930272397f21fd86e5d4eeee61bba72c95ec8d2`; host tool
reports `Limine 12.9.3`; second invocation is a no-op.

Builds: debug, release, and test all compile with `-Werror` and link with
`--fatal-warnings` without diagnostics. `nm -u` empty for all three.

`make test`:

```
verify-elf: PASS                                   (41 checks)
PASS  boot              (qemu exit 33, 42 serial lines)
PASS  panic             (qemu exit 35, 47 serial lines)
PASS  unknown-selector  (qemu exit 37, 42 serial lines)
boot_test: all tests passed
```

Serial excerpt (SeaBIOS, test build, `test=boot`):

```
SOBI OS 0.1.0 (x86-64, test build)
[info ] boot: bootloader: Limine 12.9.3 (limine protocol base revision 6)
[info ] boot: stack size request honoured
[info ] boot: hhdm offset: 0xffff800000000000
[info ] boot: kernel phys 0xff7a000 virt 0xffffffff80000000 size 32 KiB
[info ] boot: memory map (18 entries):
...
[info ] boot: memory: 260548 KiB usable, 1040 KiB bootloader-reclaimable, 32 KiB executable+modules
[info ] boot: boot: ok
[info ] test: boot test complete
test: exit 16
```

Panic path (`test=panic`):

```
!!! KERNEL PANIC !!!
at kernel/debug/test.c:40 in test_run()
reason: deliberate test panic (value=42)
rsp=0xffffffff80007160 rflags=0x0000000000000086 cr2=0x0000000000000000 cr3=0x000000000ff70000
system halted
test: exit 17
```

UEFI (OVMF): QEMU exit 33, same bootloader and revision, kernel physical base
`0xe299000`, 31 memory-map entries.

Timeout path: running the debug ISO (no exit-port write) through the harness
produced `FAIL timeout-probe timeout after 30s (kernel hung)` with the log
tail, as designed.

GDB (`make gdb` equivalent, batch mode):

```
Breakpoint 1, kmain () at kernel/main.c:27
#0  kmain () at kernel/main.c:27
#1  0xffffffff80001cfc in _start () at kernel/arch/x86_64/entry.S:19
rip 0xffffffff80001c91  rsp 0xffffffff80007228  rbp 0x0
(after boot_collect_info)
boot_info->hhdm_offset      = 0xffff800000000000
boot_info->kernel_phys_base = 0xff7c000
boot_info->bootloader_name  = 0xffff8000000527f2 "Limine"
boot_info->memmap->entry_count = 18
```

### Problems found and fixed during the milestone

- The binary archive's `limine.c` includes `limine-bios-hdd.h`; the fetch
  script initially did not copy it, so the host tool failed to build. Fixed by
  copying the header.
- With an empty `.data` input section, `.data : ALIGN(0x1000)` had no effect
  and the RW segment began at `0x...2d20`, sharing a page with rodata. Fixed
  with standalone `. = ALIGN(0x1000)` statements before each segment.
- `--gc-sections` discarded `boot_info` in the debug build (unreferenced
  there), making it invisible to GDB. Removed the flag for now.
- `verify-elf.sh` mis-parsed the `readelf -S` flags column and compared a
  higher-half address against 0 in signed bash arithmetic. Both fixed; the
  script was then checked against an unrelated ELF to confirm it fails.
- The first summary line reported "mapped total" by summing every map entry
  including reserved ranges (12.8 GiB), which was misleading. Replaced with
  usable / bootloader-reclaimable / executable totals.

### Assumptions verified in this milestone

- Limine 12.9.3 hybrid ISO boots under QEMU 9.2.1 TCG with SeaBIOS and OVMF.
- `isa-debug-exit` on this QEMU yields `(value << 1) | 1` (0x10 -> 33,
  0x11 -> 35, 0x12 -> 37).
- The host `limine` tool compiles with gcc 14.2.
- `nm -u` is empty with the chosen flags.
- `LIMINE_ARCHIVE` offline path works; the retry path was not needed in the
  run that used it.

### Remaining limitations

See `limitations.md`. Headline: no IDT, no allocator, no interrupts, no tasks,
Limine's GDT and page tables still in use.

### Next

M2: GDT, TSS with IST1, IDT, exception stubs and handlers, controlled-fault
tests with the fixup mechanism, PIC remap and mask.

## M2: CPU tables and exceptions (2026-10-17)

### Objective

Own GDT and TSS, a 256-vector IDT, exception stubs that preserve a uniform
register frame, fatal dumps with a symbolised RIP, a fixup path so tests can
recover from a chosen fault, and the 8259 PIC remapped and masked. `IF` stays
clear.

### Commands

```
make BUILD=release
make test
```

### Observed results

Release and test builds are warning-free. `make test`:

```
verify-elf: PASS
PASS  boot              (qemu exit 33)
PASS  panic             (qemu exit 35)
PASS  unknown-selector  (qemu exit 37)
PASS  exc               (qemu exit 33)
PASS  df                (qemu exit 35)
test: all suites passed
```

Boot log (every scenario):

```
[info ] cpu: gdt loaded, tss 0x28, ist1 0xffffffff8000c350, ist2 0xffffffff80010350
[info ] cpu: idt: 256 gates, df on ist1, nmi/mc on ist2
[info ] cpu: pic remapped to 0x20/0x28, masked 0xff/0xff
```

`test=exc` reports `int3 ok`, `ksym ok` (lookup of `exc_ud2_insn` is exact),
`ud2 ok`, `de ok`, `pf ok cr2=0x1000`, `gp ok`.

`test=df` sets RSP to the non-canonical address `0x0000800000000000` and
executes `ud2`. The `#DF` handler runs on IST1 and panics:

```
vector 8 (double fault) error 0x0
rip=0xffffffff80002eb9 <exc_df_trigger+0xa> cs=0x8 rflags=0x86
rsp=0x0000800000000000 ss=0x10
reason: double fault
test: exit 17
```

The handler's own RSP at the panic (`0xffffffff8000c150`) is inside the IST1
stack, not the faulting RSP, so the IST switch happened.

### Notes

- GDT order is the SYSRET layout: user data at `0x18`, user code at `0x20`.
  `syscall`/`sysret` are not enabled yet.
- `#BP` returns without a fixup because it is a trap; every other vector is
  fatal unless `fault_expect` matches both the vector and the faulting RIP.
- Symbol table is produced by a second link (`scripts/gensyms.sh`) so text
  addresses stay stable. `nm -u` remains empty.

### Remaining limitations

No physical allocator, still on Limine's page tables, interrupts disabled,
no timer. See `limitations.md`.

### Next

M3: bitmap physical frame allocator.

## M3: Physical memory (2026-10-17)

### Objective

A bitmap frame allocator over the Limine map, with the reservation rules in
`memory-rules.md`, a kernel-owned copy of the map and bootloader strings, and
tests for round trips, contiguous allocation, exhaustion, reserved memory,
double free, and out-of-range free.

### Commands

```
make test
```

### Observed results

```
PASS  pmm        (qemu exit 33)
PASS  pmm-dbl    (qemu exit 35, panic "double free")
PASS  pmm-range  (qemu exit 35, panic "out of range")
test: all suites passed
```

Serial (QEMU q35, 256 MiB, SeaBIOS):

```
[info ] pmm: 260416 KiB free across 65503 pages, bitmap 8 KiB at 0x74000 (18 regions copied)
[info ] test: pmm: stats free=65104 used=399 npages=65503
[info ] test: pmm: reserved 0xf0000 held
[info ] test: pmm: round trip 0x76000
[info ] test: pmm: contig 0x78000
[info ] test: pmm: exhausted 65104 pages and recovered
```

`free + used == npages`. The reserved page `0xf0000` is not free. Contiguous
8-page allocation is 32 KiB aligned. Exhaustion allocates exactly the free
count, a further alloc returns 0, and freeing the list restores the count.
Release builds return `-1` from `pmm_free` on those errors instead of panicking.

### Remaining limitations

Still on Limine's page tables. No heap. Bootloader-reclaimable memory is not
freed. See `limitations.md`.

### Next

M4: kernel page tables, map/unmap, MMIO window, slab heap, reclaim bootloader memory.
