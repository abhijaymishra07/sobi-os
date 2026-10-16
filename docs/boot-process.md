# Boot process

## Bootloader and protocol

SOBI OS uses the Limine bootloader (release 12.9.3, pinned by sha256 in
`scripts/fetch-limine.sh`) and the Limine Boot Protocol. The protocol header
`include/limine.h` is vendored verbatim from the `limine-protocol` repository at
commit `3a0526b700e356f0eac1b71a77697b3fd1c707a3`. Limine 12.9.3 supports base
revisions 0 through 6; the kernel requests **base revision 6**.

Why revision 6: from revision 5 onward the protocol pins down the entry state
much more tightly (`cr0/cr4/EFER` extra bits cleared, `RFLAGS = 0x2`, no TSS, no
IDT, no LDT, legacy PICs masked, I/O APIC entries masked, every local APIC LVT
masked). Revision 6 only adds aarch64 changes, so for x86-64 it is the same as 5
while being the newest the bootloader accepts.

What the bootloader guarantees at `_start` (x86-64, revision 6):

- 64-bit long mode, 4-level paging (`paging_mode: 4level` in `limine.conf`),
  `PE`, `PG`, `WP`, `PAE`, `LME`, `LMA`, and `NX` (if available) enabled.
- `CS = 0x28`, `DS/ES/SS/FS/GS = 0x30` into a bootloader-owned GDT in
  bootloader-reclaimable memory.
- `IF = 0`, `DF = 0`, IDTR base 0 limit 0 (no IDT), TR base 0 limit 0 (no TSS).
- `rsp` at the top of a bootloader-reclaimable stack of at least 64 KiB (the
  kernel requests 64 KiB explicitly and the response confirms it), with a 0
  return address pushed. All other GPRs are 0.
- The kernel ELF mapped at or above `0xffffffff80000000` with per-`PT_LOAD`
  permissions, and a Higher Half Direct Map (HHDM) whose offset is reported in
  the HHDM response.

## Requests and their placement

`kernel/boot/limine_requests.c` defines, in order:

| Section | Object | Content |
|---|---|---|
| `.limine_requests_start` | `uint64_t[4]` | `LIMINE_REQUESTS_START_MARKER` |
| `.limine_requests` | `uint64_t[3]` | `LIMINE_BASE_REVISION(6)` |
| `.limine_requests` | `struct limine_bootloader_info_request` | name and version strings |
| `.limine_requests` | `struct limine_executable_cmdline_request` | kernel command line |
| `.limine_requests` | `struct limine_hhdm_request` | HHDM offset |
| `.limine_requests` | `struct limine_memmap_request` | memory map |
| `.limine_requests` | `struct limine_executable_address_request` | physical and virtual base |
| `.limine_requests` | `struct limine_stack_size_request` | 64 KiB |
| `.limine_requests_end` | `uint64_t[2]` | `LIMINE_REQUESTS_END_MARKER` |

Every object is `volatile` and `__attribute__((used))`. `linker.ld` places
the three sections, each `KEEP` and 8-byte aligned, inside the RW `PT_LOAD`
segment. From base revision 2 the bootloader must honour the delimiters, so any
request outside them would be ignored; `scripts/verify-elf.sh` checks the
sections exist, are writable, are 8-byte aligned, are ordered start < requests
< end, and lie inside the RW segment.

Observed layout (`readelf -SW build/debug/sobi-kernel.elf`):

```
[ 4] .data                  PROGBITS ffffffff80003000 004000 000008 WA
[ 5] .limine_requests_start PROGBITS ffffffff80003020 004020 000020 WA
[ 6] .limine_requests       PROGBITS ffffffff80003040 004040 000198 WA
[ 7] .limine_requests_end   PROGBITS ffffffff800031e0 0041e0 000010 WA
[ 8] .bss                   NOBITS   ffffffff80003200 0041f0 004050 WA
```

## ELF image

`readelf -lW build/debug/sobi-kernel.elf`:

```
Type: EXEC, Machine: X86-64, Entry point: 0xffffffff80001cfc (= _start)

  LOAD 0x001000 0xffffffff80000000 0x001d10 0x001d10 R E 0x1000   .text
  LOAD 0x003000 0xffffffff80002000 0x000d14 0x000d14 R   0x1000   .rodata .eh_frame
  LOAD 0x004000 0xffffffff80003000 0x0001f0 0x004250 RW  0x1000   .data .limine_requests* .bss
```

The segments are explicit in `linker.ld` (`PHDRS`), each begins on a 4 KiB
boundary (standalone `. = ALIGN(0x1000)` statements, so an empty `.data`
cannot pull the RW segment onto the rodata page), and `.eh_frame` is kept in
the read-only segment so GDB can unwind. `nm -u` is empty: no libgcc or libc
symbols are needed at this stage. `readelf -r` reports no relocations.

The stack is 16 KiB in `.bss`, 16-byte aligned (`.balign 16`). At the
`call kmain` in `_start`, `rsp == boot_stack_top` which is 16-byte aligned, so
`kmain` is entered with the System V alignment (`rsp % 16 == 8` after the
return address is pushed). GDB confirms: at the `kmain` breakpoint
`rsp = 0xffffffff80007228` with `boot_stack_top = 0xffffffff80007230`.

## Initialisation order

1. `_start` (`entry.S`): `cli`, `cld`, `rsp = boot_stack_top`, `rbp = 0`,
   `call kmain`. If `kmain` returns, `cli; hlt` loop.
2. `kmain` (`main.c`): `serial_init()` programs COM1 to 115200 8N1 with FIFOs
   and runs a loopback self-test; on failure, output is silently dropped.
3. `print_banner()`.
4. `boot_collect_info()` (`boot.c`): checks `LIMINE_BASE_REVISION_SUPPORTED`
   and panics with the loaded revision if the bootloader downgraded it; copies
   bootloader name/version, HHDM offset, memory map pointer, executable
   address, and command line into `boot_info`; panics on any missing
   response; asserts that the virtual base equals `__kernel_start`, the HHDM
   offset is canonical higher-half, and the physical base is page aligned.
5. `boot_dump_info()`: prints bootloader, stack-size confirmation, command
   line, HHDM offset, kernel physical/virtual base and section ranges, and the
   complete memory map with per-type totals.
6. `boot: ok`.
7. Test build: `test_run(cmdline)` selects a scenario and exits QEMU through
   the debug-exit port. Other builds: halt forever with interrupts disabled.

## Observed handoff (QEMU 9.2.1, q35, 256 MiB)

SeaBIOS:

```
[info ] boot: bootloader: Limine 12.9.3 (limine protocol base revision 6)
[info ] boot: stack size request honoured
[info ] boot: hhdm offset: 0xffff800000000000
[info ] boot: kernel phys 0xff7a000 virt 0xffffffff80000000 size 32 KiB
[info ] boot: memory map (18 entries):
[info ] boot:   0x0000000000001000-0x0000000000074000        460 KiB bootloader-reclaimable
[info ] boot:   0x0000000000074000-0x000000000009f000        172 KiB usable
[info ] boot:   0x000000000009fc00-0x00000000000a0000          1 KiB reserved
[info ] boot:   0x00000000000f0000-0x00000000000f5000         20 KiB reserved
[info ] boot:   0x00000000000f5000-0x00000000000f6000          4 KiB reserved-mapped
[info ] boot:   0x00000000000f6000-0x0000000000100000         40 KiB reserved
[info ] boot:   0x0000000000100000-0x000000000ff46000     260376 KiB usable
[info ] boot:   0x000000000ff46000-0x000000000ff7a000        208 KiB bootloader-reclaimable
[info ] boot:   0x000000000ff7a000-0x000000000ff82000         32 KiB executable+modules
[info ] boot:   0x000000000ff82000-0x000000000ffdf000        372 KiB bootloader-reclaimable
[info ] boot:   0x000000000ffdf000-0x000000000ffe0000          4 KiB reserved
[info ] boot:   0x000000000ffe0000-0x000000000ffe3000         12 KiB reserved-mapped
[info ] boot:   0x000000000ffe3000-0x0000000010000000        116 KiB reserved
[info ] boot:   0x00000000b0000000-0x00000000c0000000     262144 KiB reserved
[info ] boot:   0x00000000fd000000-0x00000000fd3e8000       4000 KiB framebuffer
[info ] boot:   0x00000000fed1c000-0x00000000fed20000         16 KiB reserved
[info ] boot:   0x00000000fffc0000-0x0000000100000000        256 KiB reserved
[info ] boot:   0x000000fd00000000-0x0000010000000000   12582912 KiB reserved
[info ] boot: memory: 260548 KiB usable, 1040 KiB bootloader-reclaimable, 32 KiB executable+modules
```

OVMF (UEFI): same bootloader and revision, HHDM at the same offset, kernel
physical base `0xe299000`, 31 memory-map entries. Both firmware paths pass
the boot test with QEMU exit status 33.

Notes on the map: page 0 is not reported at all here, but the protocol allows
it to be usable from revision 3, so the allocator will exclude it explicitly.
The `0x1000-0x74000` bootloader-reclaimable region holds Limine's low-memory
structures; the regions around the kernel hold its page tables, stack, GDT, and
responses. None of it may be reused until the conditions in `memory-rules.md`
are met.

## ISO image

`scripts/mkiso.sh` stages:

```
/boot/sobi-kernel.elf
/boot/limine/limine.conf        (+ optional "cmdline: ..." line for tests)
/boot/limine/limine-bios.sys
/boot/limine/limine-bios-cd.bin
/boot/limine/limine-uefi-cd.bin
/EFI/BOOT/BOOTX64.EFI
/EFI/BOOT/BOOTIA32.EFI
```

then runs the `xorriso -as mkisofs` recipe from Limine's USAGE.md (El Torito
BIOS boot image, EFI boot partition, protective MBR, HFS+ for completeness) and
`limine bios-install` on the result. Limine searches
`/boot/limine/limine.conf`, which is one of its documented config locations.

`boot/limine.conf`:

```
timeout: 0
serial: yes
verbose: no
quiet: yes

/SOBI OS
    protocol: limine
    path: boot():/boot/sobi-kernel.elf
    paging_mode: 4level
```
