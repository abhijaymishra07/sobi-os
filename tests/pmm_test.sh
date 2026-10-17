#!/usr/bin/env bash
set -uo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/lib/run_qemu_test.sh"

failures=0

run_qemu_test pmm "test=pmm" "${QEMU_EXIT_PASS}" \
    "boot: ok" \
    "pmm: stats free=" \
    "pmm: reserved " \
    "pmm: round trip " \
    "pmm: contig " \
    "pmm: exhausted " \
    "pmm: all checks passed" \
    || failures=$((failures + 1))

run_qemu_test pmm-dbl "test=pmm-dbl" "${QEMU_EXIT_PANIC}" \
    "pmm: double free of " \
    "double free" \
    "!!! KERNEL PANIC !!!" \
    || failures=$((failures + 1))

run_qemu_test pmm-range "test=pmm-range" "${QEMU_EXIT_PANIC}" \
    "pmm: freeing an out of range address" \
    "out of range" \
    "!!! KERNEL PANIC !!!" \
    || failures=$((failures + 1))

if [[ ${failures} -eq 0 ]]; then
    echo "pmm_test: all tests passed"
    exit 0
fi
echo "pmm_test: ${failures} test(s) failed"
exit 1
