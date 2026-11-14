# Testing

`make test` has existed since Milestone 1 and grows with every milestone. It
runs three layers:

1. `scripts/verify-elf.sh` on the test kernel: static structural checks.
2. `scripts/mkiso.sh`: builds the test ISO.
3. Every `tests/*_test.sh`: boots the kernel in QEMU with a command line and
   checks the exit status and serial output. Suites as of M5: `boot`, `exc`,
   `pmm`, `vmm`, `timer`.

## ELF verification

`scripts/verify-elf.sh <kernel.elf>` prints one line per check and exits
non-zero if any fails. Checks (41 as of M1):

- `readelf -h`: `ELF64`, `EXEC`, `X86-64`, entry equals the `nm` address of
  `_start`, entry in the higher half.
- `readelf -l`: exactly three `PT_LOAD` with flags `R E`, `R`, `RW`, each
  with `Align 0x1000` and a page-aligned higher-half `VirtAddr`; no
  `PT_INTERP`, `PT_DYNAMIC`, or executable `GNU_STACK`.
- `readelf -S`: `.limine_requests_start`, `.limine_requests`,
  `.limine_requests_end` present, flags `WA`, 8-byte aligned, strictly
  ordered, inside the RW segment.
- `nm -u`: empty. `readelf -r`: no relocations.
- `objdump -d _start`: `boot_stack_top` 16-byte aligned, `rsp` loaded from
  it, `rbp` zeroed, `call kmain` with no stack adjustment between the load and
  the call, `hlt` afterwards.

The script was also run against an unrelated host ELF (the `limine` tool) to
confirm it fails loudly (13 failures, exit 1) rather than passing vacuously.

## QEMU exit-status protocol

The test build is the only configuration that writes to the QEMU
`isa-debug-exit` device (`-device isa-debug-exit,iobase=0xf4,iosize=0x04`).
QEMU exits with status `(value << 1) | 1`, which was confirmed on QEMU 9.2.1:

| Kernel writes | QEMU exit | Meaning |
|---|---|---|
| `0x10` (`TEST_EXIT_CODE_PASS`) | 33 | scenario completed successfully |
| `0x11` (`TEST_EXIT_CODE_PANIC`) | 35 | `panic()` was reached |
| `0x12` (`TEST_EXIT_CODE_FAIL`) | 37 | scenario detected a failure |
| none, `timeout` fires | 124 | kernel hung (default 30 s, `SOBI_TEST_TIMEOUT`) |
| none, QEMU exits itself | 0 | triple fault / reset / shutdown (`-no-reboot` is set) |
| anything else | n | unexpected, treated as failure |

Each scenario declares the exit status it expects, so a test that is supposed
to panic asserts 35 and fails on 33. In addition to the exit status a scenario
lists strings that must appear in the serial log. On failure the harness prints
the classification and the last 40 serial lines.

Scenario selection uses the Limine `cmdline:` key: `scripts/mkiso.sh` appends
`cmdline: <text>` to `limine.conf` for a per-scenario ISO, and
`kernel/debug/test.c` matches whitespace-separated `test=<name>` tokens.

## Current scenarios

`make test` runs `verify-elf` and then 28 QEMU scenarios. `disk` and
`net-live` attach extra QEMU devices through `SOBI_QEMU_EXTRA` (a temporary
virtio-blk image, and virtio-net on QEMU user networking with echo servers on
the host).

| Scenario | Script | cmdline | Expect | What it checks |
|---|---|---|---|---|
| `boot` | `boot_test.sh` | `test=boot` | PASS | banner, boot protocol, memory map |
| `panic` | `boot_test.sh` | `test=panic` | PANIC | deliberate panic is reported and classified |
| `unknown-selector` | `boot_test.sh` | `test=does-not-exist` | FAIL | bad selector is reported as failure |
| `console` | `console_test.sh` | `test=console` | PASS | framebuffer text, keyboard scancodes, real IRQ 1 via I/O APIC |
| `disk-write` | `disk_test.sh` | `test=disk-write` | PASS | virtio-blk + block cache; sobifs format and write |
| `disk-read` | `disk_test.sh` | `test=disk-read` | PASS | second boot reads the file back |
| `elf` | `elf_test.sh` | `test=elf` | PASS | ELF validation, hello exits 0 |
| `exc` | `exc_test.sh` | `test=exc` | PASS | #UD, #DE, #PF, #GP, int3 handled through fixups; symbolised RIP |
| `df` | `exc_test.sh` | `test=df` | PANIC | double fault lands on IST1 and panics |
| `multi` | `multi_test.sh` | `test=multi` | PASS | two user processes run concurrently |
| `net-live` | `net_live_test.sh` | `test=net-live` | PASS | virtio-net UDP and TCP echo with host servers |
| `net` | `net_test.sh` | `test=net` | PASS | ARP/IPv4/ICMP/UDP/TCP/DNS over loopback |
| `pipe` | `pipe_test.sh` | `test=pipe` | PASS | pipe blocking, wrap-around, EOF |
| `pmm` | `pmm_test.sh` | `test=pmm` | PASS | frame allocator round trip, contiguous runs, exhaustion |
| `pmm-dbl` | `pmm_test.sh` | `test=pmm-dbl` | PANIC | double free panics |
| `pmm-range` | `pmm_test.sh` | `test=pmm-range` | PANIC | out-of-range free panics |
| `sched` | `sched_test.sh` | `test=sched` | PASS | preemption, sleep ordering, wake-once, exit reaping |
| `shell` | `shell_test.sh` | `test=shell` | PASS | scripted shell session incl. ls, cat, cat | upper, clean exit |
| `sobifs` | `sobifs_test.sh` | `test=sobifs` | PASS | journalled FS on ramdisk: persist, torn-journal replay, disk full |
| `sync` | `sync_test.sh` | `test=sync` | PASS | mutex, semaphore, spinlock |
| `syscall` | `syscall_test.sh` | `test=syscall` | PASS | syscall/sysret table, bad pointers, spawn errors |
| `timer` | `timer_test.sh` | `test=timer` | PASS | LAPIC calibration, 100 Hz ticks, uptime |
| `trace` | `trace_test.sh` | `test=trace` | PASS | ring buffer + live scheduler/syscall hooks |
| `user` | `user_test.sh` | `test=user` | PASS | ring 3 entry, isolation, user fault kills process |
| `vfs` | `vfs_test.sh` | `test=vfs` | PASS | lookup, EOF, tmpfs, fd limit |
| `vmm` | `vmm_test.sh` | `test=vmm` | PASS | map/unmap/protect, W^X, NX faults, guard pages |
| `kheap` | `vmm_test.sh` | `test=kheap` | PASS | slab heap alignment, reuse, stress |
| `kheap-dbl` | `vmm_test.sh` | `test=kheap-dbl` | PANIC | heap double free panics |

## Results

`make test` on 2026-11-14 (QEMU 10.2.1, TCG, gcc 15.2) from a fresh clone:
`verify-elf: PASS` and all 28 scenarios `PASS` (`test: all suites
passed`) in about 30 seconds. The same suite runs in GitHub Actions on every
push (`.github/workflows/ci.yml`).

## Writing a new scenario

1. Add a `test=<name>` branch in `kernel/debug/test.c` that ends in
   `test_exit(TEST_EXIT_CODE_PASS)` or a deliberate `panic()`.
2. Add a `run_qemu_test <name> "test=<name>" <expected-exit> "<required text>"...`
   line to the relevant `tests/*.sh` file.
3. Run `make test` and record the result in `docs/milestones.md`.

Tests that exercise faults must never return blindly to a faulting
instruction; M2 introduces an explicit fixup mechanism for that (see
`interrupts-and-timer.md`).
