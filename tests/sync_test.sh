#!/usr/bin/env bash
set -uo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/lib/run_qemu_test.sh"

failures=0

run_qemu_test sync "test=sync" "${QEMU_EXIT_PASS}" \
    "boot: ok" \
    "sync: semaphore ok" \
    "sync: all checks passed" \
    || failures=$((failures + 1))

if [[ ${failures} -eq 0 ]]; then
    echo "sync_test: all tests passed"
    exit 0
fi
echo "sync_test: ${failures} test(s) failed"
exit 1
