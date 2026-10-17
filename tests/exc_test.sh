#!/usr/bin/env bash
set -uo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/lib/run_qemu_test.sh"

failures=0

run_qemu_test exc "test=exc" "${QEMU_EXIT_PASS}" \
    "boot: ok" \
    "exc: int3 ok" \
    "exc: ksym ok" \
    "exc: ud2 ok" \
    "exc: de ok" \
    "exc: pf ok cr2=0x1000" \
    "exc: gp ok" \
    "exc: all faults handled" \
    || failures=$((failures + 1))

run_qemu_test df "test=df" "${QEMU_EXIT_PANIC}" \
    "exc: triggering double fault" \
    "double fault" \
    "!!! KERNEL PANIC !!!" \
    "system halted" \
    || failures=$((failures + 1))

if [[ ${failures} -eq 0 ]]; then
    echo "exc_test: all tests passed"
    exit 0
fi
echo "exc_test: ${failures} test(s) failed"
exit 1
