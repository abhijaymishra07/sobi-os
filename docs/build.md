# Build system

## Requirements

Verified host: Ubuntu 25.04, gcc 14.2.0, GNU binutils 2.44, GNU Make 4.4.1,
QEMU 9.2.1, GDB 16.2, xorriso 1.5.6, curl. OVMF (`/usr/share/OVMF/OVMF_CODE_4M.fd`)
is optional and only used by `make run-uefi`.

```
sudo apt install build-essential qemu-system-x86 gdb xorriso curl ovmf
```

No cross-compiler is required. The host gcc is driven in freestanding mode
with a kernel code model, which produces correct x86-64 ELF for a bare-metal
target as long as nothing from the host libc or libgcc is linked. The build
enforces this: `nm -u` on the kernel must be empty (checked by
`scripts/verify-elf.sh`), and `ld` runs with `-nostdlib -static`.

## Targets

| Target | Effect |
|---|---|
| `make` / `make kernel` | `build/$(BUILD)/sobi-kernel.elf` and `.map` |
| `make iso` | `build/$(BUILD)/sobi.iso` (fetches Limine on first use) |
| `make run` | QEMU q35, 256 MiB, serial on stdio, no display |
| `make run-uefi` | same with OVMF firmware |
| `make gdb` | QEMU `-s -S` and GDB attached with a breakpoint on `kmain` |
| `make verify-elf` | structural checks on the kernel ELF |
| `make test` | `BUILD=test` verify-elf, ISO, then `tests/boot_test.sh` |
| `make limine` | fetch and build the bootloader and host tool only |
| `make clean` | remove `build/` |
| `make distclean` | also remove `third_party/limine/` |

Variables: `BUILD=debug|release|test` (default `debug`), `QEMU_MEM` (default
`256M`), `OVMF` (firmware path), `LIMINE_ARCHIVE` (offline path to
`limine-binary.tar.xz` for the fetch script), `HOST_CC` (compiler for the
host `limine` tool).

## Compiler and linker flags

```
-std=gnu11 -ffreestanding -fno-stack-protector -fno-stack-check -fno-lto
-fno-PIC -fno-PIE -fno-builtin -m64 -march=x86-64
-mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel
-Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-prototypes
-Wvla -Wundef -Wpointer-arith -Wwrite-strings -Werror
-MMD -MP -Iinclude
```

- `-mno-red-zone`: interrupt handlers (M2) will run on the same stack as the
  interrupted code; the red zone would be clobbered.
- `-mno-sse*`, `-mno-mmx`, `-mno-80387`: no FPU/SIMD state is saved yet, so
  the compiler must not use those registers.
- `-mcmodel=kernel`: code is linked in the top 2 GiB (`0xffffffff80000000`),
  allowing 32-bit sign-extended absolute addressing.
- `-fno-PIC -fno-PIE`: a non-relocatable `EXEC` image; Limine does not need
  to relocate it and `verify-elf.sh` asserts there are no relocations.
- `-fno-builtin`: prevents gcc from turning the kernel's own `memcpy` into a
  recursive builtin expansion.
- `.S` files are compiled with `gcc -c`, so the C preprocessor is available
  and shared constants can live in headers guarded by `__ASSEMBLER__`. Each
  `.S` file ends with `.section .note.GNU-stack,"",@progbits` and the linker
  is given `-z noexecstack`.

Linker:

```
ld -nostdlib -static -z max-page-size=0x1000 -z noexecstack
   --warn-common --fatal-warnings -T linker.ld -Map=build/.../sobi-kernel.map
```

`--gc-sections` is deliberately not used yet: with `-O0` and no
`-ffunction-sections` it bought nothing and it discarded the `boot_info`
variable from the debug build (nothing referenced it there), which broke GDB
inspection. It can return together with `-ffunction-sections -fdata-sections`
once there is dead code worth removing.

## Build configurations

| `BUILD` | Extra flags | Notes |
|---|---|---|
| `debug` | `-O0 -g3 -DSOBI_BUILD_DEBUG=1` | default |
| `release` | `-O2 -g -DSOBI_BUILD_RELEASE=1` | symbols retained; strip separately if needed |
| `test` | `-O0 -g3 -DSOBI_BUILD_TEST=1 -DSOBI_TEST=1` | enables `kernel/debug/test.c` and QEMU exit-port writes |

Each configuration has its own `build/<name>/` tree, so they never share
object files. `Makefile` is a prerequisite of every object and of the link, so
flag changes trigger a rebuild. Header dependencies are tracked with `-MMD`.

## Reproducibility

- Limine is pinned to release 12.9.3 and the archive's sha256 is checked.
- The protocol header is vendored at a fixed commit (see `NOTICE`).
- No network access is needed after the first `make iso` (or none at all
  with `LIMINE_ARCHIVE`).
- Build output never lands outside `build/` and `third_party/limine/`, both
  ignored by Git.
