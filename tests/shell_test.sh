#!/usr/bin/env bash
set -uo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/lib/run_qemu_test.sh"

failures=0

run_qemu_test shell "test=shell" "${QEMU_EXIT_PASS}" \
    "boot: ok" \
    "hi" \
    "/bin:" \
    "  sh" \
    "  etc/" \
    "Welcome to SOBI OS." \
    "shell: script done" \
    || failures=$((failures + 1))

if [[ ${failures} -eq 0 ]]; then
    echo "shell_test: all tests passed"
    exit 0
fi
echo "shell_test: ${failures} test(s) failed"
exit 1
