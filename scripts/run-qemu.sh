#!/usr/bin/env bash
set -euo pipefail

[[ $# -ge 1 ]] || { echo "usage: $0 <image.iso> [extra qemu args...]" >&2; exit 2; }
ISO="$1"
shift

exec qemu-system-x86_64 -M q35 -m "${QEMU_MEM:-256M}" -no-reboot \
    -cdrom "${ISO}" -display none -serial stdio "$@"
