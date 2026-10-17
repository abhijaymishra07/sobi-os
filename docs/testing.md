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

## Current scenarios (`tests/boot_test.sh`)

| Name | cmdline | Expect | Required serial text |
|---|---|---|---|
| `boot` | `test=boot` | 33 | banner, `limine protocol base revision 6`, `hhdm offset: 0xffff8`, `memory map (`, `boot: ok`, `boot test complete` |
| `panic` | `test=panic` | 35 | `boot: ok`, `!!! KERNEL PANIC !!!`, `at kernel/debug/test.c:`, `reason: deliberate test panic (value=42)`, `system halted` |
| `unknown-selector` | `test=does-not-exist` | 37 | `boot: ok`, `unknown test selector` |

## Results (2026-10-17)

```
$ make test
verify-elf: PASS
PASS  boot              (qemu exit 33, 42 serial lines)
PASS  panic             (qemu exit 35, 47 serial lines)
PASS  unknown-selector  (qemu exit 37, 42 serial lines)
boot_test: all tests passed
```

Additional manual checks performed for M1 (not yet automated):

- The test ISO boots under OVMF (`make run-uefi` equivalent with the exit
  device) and exits 33.
- The timeout path was exercised by running the debug ISO (which never writes
  the exit port) through the harness: classified as `timeout ... (kernel hung)`
  with the log tail printed.
- GDB attach via `make gdb`: breakpoint on `kmain` hit, backtrace
  `kmain <- _start`, `boot_info` fields readable after `boot_collect_info`.

## Writing a new scenario

1. Add a `test=<name>` branch in `kernel/debug/test.c` that ends in
   `test_exit(TEST_EXIT_CODE_PASS)` or a deliberate `panic()`.
2. Add a `run_qemu_test <name> "test=<name>" <expected-exit> "<required text>"...`
   line to the relevant `tests/*.sh` file.
3. Run `make test` and record the result in `docs/milestones.md`.

Tests that exercise faults must never return blindly to a faulting
instruction; M2 introduces an explicit fixup mechanism for that (see
`interrupts-and-timer.md`).
