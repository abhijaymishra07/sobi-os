#!/usr/bin/env bash
set -uo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/lib/run_qemu_test.sh"

failures=0

run_qemu_test pipe "test=pipe" "${QEMU_EXIT_PASS}" \
    "boot: ok" \
    "pipe: echo ok" \
    "pipe: eof ok" \
    "pipe: all checks passed" \
    || failures=$((failures + 1))

if [[ ${failures} -eq 0 ]]; then
    echo "pipe_test: all tests passed"
    exit 0
fi
echo "pipe_test: ${failures} test(s) failed"
exit 1
