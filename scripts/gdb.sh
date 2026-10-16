#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 2 ]] || { echo "usage: $0 <kernel.elf> \"<qemu command with -s -S>\"" >&2; exit 2; }
KERNEL="$1"
QEMU_CMD="$2"

${QEMU_CMD} &
QEMU_PID=$!
trap 'kill ${QEMU_PID} 2>/dev/null || true' EXIT

gdb -q "${KERNEL}" \
    -ex 'set architecture i386:x86-64' \
    -ex 'target remote localhost:1234' \
    -ex 'break kmain' \
    -ex 'continue' \
    "${@:3}"
