#!/usr/bin/env bash
set -uo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/lib/run_qemu_test.sh"

failures=0
# SOBI_QEMU_EXTRA is word-split, so the image path must not contain spaces.
disk="$(mktemp /tmp/sobi-disk.XXXXXX)"
trap 'rm -f "${disk}"' EXIT
truncate -s 4M "${disk}"
export SOBI_QEMU_EXTRA="-drive file=${disk},if=none,format=raw,id=d0 -device virtio-blk-pci,drive=d0"

run_qemu_test disk-write "test=disk-write" "${QEMU_EXIT_PASS}" \
    "boot: ok" \
    "virtio-blk: disk 0: 8192 sectors" \
    "block: ramdisk cache ok" \
    "disk: formatted and wrote /docs/note" \
    || failures=$((failures + 1))

run_qemu_test disk-read "test=disk-read" "${QEMU_EXIT_PASS}" \
    "boot: ok" \
    "disk: read back after reboot: 'written on boot 1'" \
    || failures=$((failures + 1))

if [[ ${failures} -eq 0 ]]; then
    echo "disk_test: all tests passed"
    exit 0
fi
echo "disk_test: ${failures} test(s) failed"
exit 1
