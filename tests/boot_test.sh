#!/usr/bin/env bash
set -uo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/lib/run_qemu_test.sh"

failures=0

run_qemu_test boot "test=boot" "${QEMU_EXIT_PASS}" \
    "SOBI OS 0." \
    "limine protocol base revision 6" \
    "hhdm offset: 0xffff8" \
    "memory map (" \
    "boot: ok" \
    "boot test complete" \
    || failures=$((failures + 1))

run_qemu_test panic "test=panic" "${QEMU_EXIT_PANIC}" \
    "boot: ok" \
    "!!! KERNEL PANIC !!!" \
    "at kernel/debug/test.c:" \
    "reason: deliberate test panic (value=42)" \
    "system halted" \
    || failures=$((failures + 1))

run_qemu_test unknown-selector "test=does-not-exist" "${QEMU_EXIT_FAIL}" \
    "boot: ok" \
    "unknown test selector" \
    || failures=$((failures + 1))

if [[ ${failures} -eq 0 ]]; then
    echo "boot_test: all tests passed"
    exit 0
fi
echo "boot_test: ${failures} test(s) failed"
exit 1
