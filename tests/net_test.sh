#!/usr/bin/env bash
set -uo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/lib/run_qemu_test.sh"

failures=0

run_qemu_test net "test=net" "${QEMU_EXIT_PASS}" \
    "boot: ok" \
    "net: icmp reply" \
    "net: udp ok" \
    "net: all checks passed" \
    "net: tcp loopback ok" \
    || failures=$((failures + 1))

if [[ ${failures} -eq 0 ]]; then
    echo "net_test: all tests passed"
    exit 0
fi
echo "net_test: ${failures} test(s) failed"
exit 1
