#!/usr/bin/env bash
set -uo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/lib/run_qemu_test.sh"

failures=0

run_qemu_test sobifs "test=sobifs" "${QEMU_EXIT_PASS}" \
    "boot: ok" \
    "sobifs: persist ok" \
    "sobifs: replay ok" \
    "sobifs: full ok" \
    "sobifs: all checks passed" \
    || failures=$((failures + 1))

if [[ ${failures} -eq 0 ]]; then
    echo "sobifs_test: all tests passed"
    exit 0
fi
echo "sobifs_test: ${failures} test(s) failed"
exit 1
