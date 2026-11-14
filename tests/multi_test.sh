#!/usr/bin/env bash
set -uo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/lib/run_qemu_test.sh"

failures=0

run_qemu_test multi "test=multi" "${QEMU_EXIT_PASS}" \
    "boot: ok" \
    "spin done" \
    "multi: two processes ok" \
    || failures=$((failures + 1))

if [[ ${failures} -eq 0 ]]; then
    echo "multi_test: all tests passed"
    exit 0
fi
echo "multi_test: ${failures} test(s) failed"
exit 1
