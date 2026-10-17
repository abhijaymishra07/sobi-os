#!/usr/bin/env bash
set -uo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/lib/run_qemu_test.sh"

failures=0

run_qemu_test vmm "test=vmm" "${QEMU_EXIT_PASS}" \
    "boot: ok" \
    "nx on" \
    "vmm: map " \
    "vmm: execute ok" \
    "vmm: nx fault cr2=" \
    "vmm: ro fault ok" \
    "vmm: unmap tlb ok" \
    "vmm: stack guard " \
    "vmm: alloc stack " \
    "vmm: all checks passed" \
    || failures=$((failures + 1))

run_qemu_test kheap "test=kheap" "${QEMU_EXIT_PASS}" \
    "boot: ok" \
    "kheap: live 0 " \
    "kheap: all checks passed" \
    || failures=$((failures + 1))

run_qemu_test kheap-dbl "test=kheap-dbl" "${QEMU_EXIT_PANIC}" \
    "kheap: double free of " \
    "double free" \
    "!!! KERNEL PANIC !!!" \
    || failures=$((failures + 1))

if [[ ${failures} -eq 0 ]]; then
    echo "vmm_test: all tests passed"
    exit 0
fi
echo "vmm_test: ${failures} test(s) failed"
exit 1
