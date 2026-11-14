#!/usr/bin/env bash
set -uo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/lib/run_qemu_test.sh"

failures=0

run_qemu_test trace "test=trace" "${QEMU_EXIT_PASS}" \
    "boot: ok" \
    "trace: wrap ok" \
    "trace: all checks passed" \
    "multi: two processes ok" \
    "trace: hooks ok" \
    || failures=$((failures + 1))

if [[ ${failures} -eq 0 ]]; then
    echo "trace_test: all tests passed"
    exit 0
fi
echo "trace_test: ${failures} test(s) failed"
exit 1
