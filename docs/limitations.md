# Known limitations (Milestone 11)

What the kernel does not do yet, and sharp edges in what it does.

## Not implemented

- One CPU and one run queue; APs stay parked. There is no `fork` or `exec`;
  programs are started with `spawn` or `SYS_PIPELINE`.
- `spawn` runs the child synchronously inside the parent's syscall; only
  `SYS_PIPELINE` (and the kernel `elf_spawn` helpers) run processes
  concurrently. Pipelines are limited to two stages.
- sobifs works on a virtio-blk disk but is not mounted into the VFS, so user
  programs cannot see it. Only one disk is used (block device 0).
- The network stack has no socket syscalls; it is reachable only from kernel
  code. virtio-blk and virtio-net are polled (no MSI or IRQ), and both use
  the legacy virtio transport.
- The keyboard handles set-1 scancodes with shift only (no caps lock, ctrl or
  extended keys). The framebuffer console has no cursor and no colours.
- The heap window is a fixed 64 MiB. There is no demand paging or
  copy-on-write. `vmm_unmap` does not free the data frame.

## Sharp edges in what exists

- Serial output is polled and blocking. If the UART loopback self-test fails,
  `serial_init` returns `false` and all output is dropped silently; there is
  no alternative console.
- `kvprintf` formats into a 512-byte stack buffer and truncates longer lines.
- `kvsnprintf` supports `%c %s %d %i %u %x %X %o %p %%` with `l`, `ll`, `z`
  modifiers, width, precision, `-`, `0`, `#`, `+`, space. No floating point,
  no `%n`.
- `panic()` prints `rsp/rflags/cr2/cr3`. Exception paths print the full
  `int_frame` and a symbolised RIP before panicking. There is still no
  stack walk.
- `ASSERT()` is always compiled in, including release builds.
- `boot_info` pointers (name, version, cmdline, memmap) refer to copies in
  kernel `.bss`. `pmm_reclaim_bootloader()` runs at the end of `vmm_init`,
  after `cr3` holds kernel page tables. Observed reclaim: 1132 KiB. `cr3`
  itself came from `pmm_alloc` (`0x76000`).
- The ISO's `limine.conf` sets `timeout: 0`, so there is no boot menu. Press
  nothing; the kernel boots immediately. Change `timeout` for interactive use.
- `make run` uses `-serial stdio`; Ctrl-C terminates QEMU (it is not passed
  to the guest).

## Environment

- Verified only under QEMU (9.2.1 and 10.2.1) with TCG on the `q35` machine
  with SeaBIOS and OVMF. Real hardware has not been tried; the drivers assume
  QEMU's devices (legacy virtio, i8042, the I/O APIC at its default address).
- The host gcc is used in freestanding mode instead of a dedicated
  `x86_64-elf` cross toolchain. This works because nothing from the host libc
  or libgcc is linked (`nm -u` is verified empty), but a change in gcc that
  emits a libgcc helper call (for example `__udivti3` for 128-bit division)
  would surface as an undefined symbol at link time and must be fixed in
  kernel code, not by linking the host libgcc.
- Downloading Limine from GitHub was slow on the development host (over two
  minutes for 878 KB). `scripts/fetch-limine.sh` retries, and
  `LIMINE_ARCHIVE` allows a local copy.
