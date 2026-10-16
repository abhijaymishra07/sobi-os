# Memory reservation and reclamation rules

These rules are fixed before the physical allocator (M3) exists so that the
allocator implements them rather than discovering them. They follow from the
Limine protocol at base revision 6 and from what the kernel owns at each
milestone.

## Memory map types and their treatment

| Limine type | At boot | Later |
|---|---|---|
| `USABLE` | free, except physical page 0 | - |
| `BOOTLOADER_RECLAIMABLE` | reserved | freed in one explicit step once the conditions below hold |
| `EXECUTABLE_AND_MODULES` | reserved | never freed (kernel image, initramfs) |
| `ACPI_RECLAIMABLE` | reserved | stays reserved for the whole initial delivery; MADT and other tables are read from it |
| `ACPI_NVS` | reserved | never |
| `RESERVED`, `RESERVED_MAPPED`, `BAD_MEMORY` | reserved | never |
| `FRAMEBUFFER` | reserved | never (MMIO) |

Physical page 0 is excluded from the free pool even if a map entry covers it
(the protocol permits that from revision 3) so that a physical address of 0
can mean "no frame" throughout the kernel.

Guarantees used: entries are sorted by base; `USABLE` and
`BOOTLOADER_RECLAIMABLE` entries are 4 KiB aligned and never overlap any other
entry. Other types may be unaligned (observed: `0x9fc00-0xa0000`); the
allocator rounds reserved ranges outward and free ranges inward.

## What lives in bootloader-reclaimable memory

- Limine's GDT (the one `CS/DS/...` currently refer to).
- The active page tables (`cr3` observed at `0xff70000`, inside a
  bootloader-reclaimable region).
- The 64 KiB stack Limine entered the kernel on. The kernel switched to its
  own `.bss` stack in `_start`, so this is unused but still inside the region.
- Every response structure and the strings and arrays they point to
  (bootloader name/version, command line, the memmap entry array and the
  entries themselves). Observed: the bootloader name string sits at
  `0xffff8000000527f2`, that is physical `0x527f2`, inside
  `0x1000-0x74000 bootloader-reclaimable`.

## Reclamation conditions

`pmm_reclaim_bootloader()` may run only when all of the following are true:

1. The kernel has loaded its own GDT and TSS (M2).
2. The kernel runs on its own stack (done in M1).
3. `cr3` points to page tables the kernel built itself (M4).
4. The memory map, the command line, and any other response data the kernel
   still needs have been copied into kernel-owned memory.

The step is performed once, logged with the number of KiB reclaimed, and
asserts that `cr3` is not inside any region being freed.

## HHDM rules

- Physical memory is accessed through `boot_info->hhdm_offset + phys`. The
  offset is read from the HHDM response every boot; it is never hard-coded
  (observed `0xffff800000000000`, but the protocol allows randomisation).
- At revision 6 the HHDM covers only: `USABLE`, `BOOTLOADER_RECLAIMABLE`,
  `EXECUTABLE_AND_MODULES`, `FRAMEBUFFER`, `ACPI_RECLAIMABLE`, `ACPI_NVS`,
  `RESERVED_MAPPED`. Plain `RESERVED` is **not** mapped. Dereferencing an HHDM
  address for any other physical range is a bug, not "undefined but works".
- MMIO that the kernel needs (local APIC at `IA32_APIC_BASE`, typically
  `0xFEE00000`; I/O APIC; HPET; PCI BARs) is not in the HHDM and must be
  mapped explicitly through the VMM (M4) before use (M5 and later).
- When the kernel builds its own page tables (M4) it reproduces an HHDM over
  exactly the same region types, plus the kernel image from the Executable
  Address response with text R+X, rodata R (NX), data RW (NX).

## Allocator placement (M3)

The frame bitmap is placed in the lowest `USABLE` region large enough to hold
it, and its own frames are marked used before any allocation is served. The
allocator exposes total/free/reserved counts and the number of frames in each
map type so that `boot_dump_info`-style reporting and tests can check them.
