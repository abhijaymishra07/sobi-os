#!/usr/bin/env bash
set -uo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/lib/run_qemu_test.sh"

failures=0

run_qemu_test user "test=user" "${QEMU_EXIT_PASS}" \
    "boot: ok" \
    "user: trap ok" \
    "killed pid" \
    "user: isolation ok" \
    "user: all checks passed" \
    || failures=$((failures + 1))

if [[ ${failures} -eq 0 ]]; then
    echo "user_test: all tests passed"
    exit 0
fi
echo "user_test: ${failures} test(s) failed"
exit 1
