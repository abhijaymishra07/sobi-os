#!/usr/bin/env bash
set -uo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/lib/run_qemu_test.sh"

failures=0

run_qemu_test elf "test=elf" "${QEMU_EXIT_PASS}" \
    "boot: ok" \
    "hello" \
    "elf: malformed rejected" \
    "elf: hello exit 0" \
    "elf: all checks passed" \
    || failures=$((failures + 1))

if [[ ${failures} -eq 0 ]]; then
    echo "elf_test: all tests passed"
    exit 0
fi
echo "elf_test: ${failures} test(s) failed"
exit 1
