#!/usr/bin/env bash
set -uo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/lib/run_qemu_test.sh"

failures=0

run_qemu_test vfs "test=vfs" "${QEMU_EXIT_PASS}" \
    "boot: ok" \
    "vfs: lookup ok" \
    "vfs: eof ok" \
    "vfs: double close" \
    "vfs: tmpfs ok" \
    "vfs: fd limit" \
    "vfs: syscall bad pointer" \
    "vfs: all checks passed" \
    || failures=$((failures + 1))

if [[ ${failures} -eq 0 ]]; then
    echo "vfs_test: all tests passed"
    exit 0
fi
echo "vfs_test: ${failures} test(s) failed"
exit 1
