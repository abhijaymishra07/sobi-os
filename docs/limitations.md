# Known limitations (Milestone 5)

What the kernel does not do yet, and sharp edges in what it does.

## Not implemented

- The LAPIC timer is the only live interrupt. The PIC stays masked, and there
  is no I/O APIC, so the keyboard and other devices cannot raise IRQs yet.
  `sleep_poll` busy-halts; it does not block a task, because there are no
  tasks.
- An exception that is not a breakpoint and not an armed fault fixup is fatal.
  There is no recovery for a double fault beyond the panic on IST1.
- No tasks, processes, user mode, system calls, filesystem, or shell.
- The framebuffer is neither requested nor used; output is serial only.
- Single CPU only; APs are left where the firmware parked them.
- The heap window is a fixed 64 MiB. There is no demand paging and no
  per-process address space. `vmm_unmap` drops the mapping and empty page
  tables; it does not free the data frame. Callers that allocated the frame
  free it themselves.

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

- Verified only under QEMU 9.2.1 with TCG (no KVM on the development host)
  on the `q35` machine with SeaBIOS and OVMF. Real hardware has not been
  tried.
- The host gcc is used in freestanding mode instead of a dedicated
  `x86_64-elf` cross toolchain. This works because nothing from the host libc
  or libgcc is linked (`nm -u` is verified empty), but a change in gcc that
  emits a libgcc helper call (for example `__udivti3` for 128-bit division)
  would surface as an undefined symbol at link time and must be fixed in
  kernel code, not by linking the host libgcc.
- Downloading Limine from GitHub was slow on the development host (over two
  minutes for 878 KB). `scripts/fetch-limine.sh` retries, and
  `LIMINE_ARCHIVE` allows a local copy.
