#!/usr/bin/env bash
set -uo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/lib/run_qemu_test.sh"

failures=0

run_qemu_test console "test=console" "${QEMU_EXIT_PASS}" \
    "boot: ok" \
    "console: glyph ok" \
    "console: key a" \
    "console: all checks passed" \
    || failures=$((failures + 1))

if [[ ${failures} -eq 0 ]]; then
    echo "console_test: all tests passed"
    exit 0
fi
echo "console_test: ${failures} test(s) failed"
exit 1
