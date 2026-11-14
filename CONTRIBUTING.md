# Contributing to SOBI OS

Thanks for your interest. SOBI OS is a learning-focused hobby kernel, and
contributions of any size are welcome: bug reports, tests, docs and features.

## Getting set up

```sh
sudo apt install build-essential qemu-system-x86 xorriso curl python3
git clone https://github.com/abhijaymishra07/sobi-os.git
cd sobi-os
make test        # builds everything and runs the QEMU suite (~30 s)
make run-gui     # boot into the interactive shell
```

The first build downloads and sha256-verifies the pinned Limine release.

## Ground rules

1. **Every change lands with a test.** Add a `test=<name>` scenario in
   `kernel/debug/test.c` and a `run_qemu_test` line in `tests/*_test.sh`
   (see `docs/testing.md`). `make test` must pass.
2. **Keep the build warning-free.** The kernel builds with `-Werror` and an
   extended warning set; the linker uses `--fatal-warnings`.
3. **No libc, no host libgcc.** The kernel is freestanding; `nm -u` on the
   kernel ELF must stay empty (`make verify-elf`).
4. **Document capability changes.** Update `docs/status.md`,
   `docs/limitations.md` and, for milestones, `docs/milestones.md` with the
   commands you ran and the output you observed.
5. **Match the surrounding style**: 4-space indent, K&R braces, `snake_case`,
   comments only for constraints the code cannot express (`.editorconfig`).

## Debugging tips

- `make gdb` boots paused with GDB attached and a breakpoint on `kmain`.
- Panics print the file, line, reason and `rsp/rflags/cr2/cr3`; exceptions
  print the full register frame with a symbolised RIP.
- A failing test prints the last 40 serial lines; the full log is in
  `build/test/<name>.serial.log`.

## Pull requests

Keep PRs focused on one change, describe how you tested it, and make sure the
CI workflow is green.
