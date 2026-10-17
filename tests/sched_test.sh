#!/usr/bin/env bash
set -uo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/lib/run_qemu_test.sh"

failures=0

run_qemu_test sched "test=sched" "${QEMU_EXIT_PASS}" \
    "boot: ok" \
    "sched: online" \
    "sched: preempt ok" \
    "sched: sleep order 20,40,80" \
    "sched: wake once" \
    "sched: resources restored" \
    "sched: all checks passed" \
    || failures=$((failures + 1))

if [[ ${failures} -eq 0 ]]; then
    echo "sched_test: all tests passed"
    exit 0
fi
echo "sched_test: ${failures} test(s) failed"
exit 1
