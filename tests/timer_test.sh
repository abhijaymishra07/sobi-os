#!/usr/bin/env bash
set -uo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/lib/run_qemu_test.sh"

failures=0

run_qemu_test timer "test=timer" "${QEMU_EXIT_PASS}" \
    "boot: ok" \
    "xapic at " \
    "lapic periodic 100 Hz" \
    "timer: " \
    "ticks in 500 ms" \
    "timer: all checks passed" \
    || failures=$((failures + 1))

if [[ ${failures} -eq 0 ]]; then
    echo "timer_test: all tests passed"
    exit 0
fi
echo "timer_test: ${failures} test(s) failed"
exit 1
