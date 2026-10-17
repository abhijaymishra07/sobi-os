#!/usr/bin/env bash
set -uo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/lib/run_qemu_test.sh"

failures=0

run_qemu_test syscall "test=syscall" "${QEMU_EXIT_PASS}" \
    "boot: ok" \
    "syscall: wait 42" \
    "syscall: getpid ok" \
    "syscall: bad number" \
    "syscall: bad pointer" \
    "syscall: huge size" \
    "syscall: all checks passed" \
    || failures=$((failures + 1))

if [[ ${failures} -eq 0 ]]; then
    echo "syscall_test: all tests passed"
    exit 0
fi
echo "syscall_test: ${failures} test(s) failed"
exit 1
