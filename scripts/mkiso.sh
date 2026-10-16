#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: $0 <kernel.elf> <limine.conf> <output.iso> [cmdline]" >&2
    exit 2
}

[[ $# -ge 3 ]] || usage
KERNEL="$1"
CONF="$2"
ISO="$3"
CMDLINE="${4:-}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LIMINE_DIR="${ROOT}/third_party/limine"
STAGE="$(mktemp -d "$(dirname "${ISO}")/iso_root.XXXXXX")"
trap 'rm -rf "${STAGE}"' EXIT

mkdir -p "${STAGE}/boot/limine" "${STAGE}/EFI/BOOT"
cp "${KERNEL}" "${STAGE}/boot/sobi-kernel.elf"
cp "${CONF}" "${STAGE}/boot/limine/limine.conf"
if [[ -n "${CMDLINE}" ]]; then
    printf 'cmdline: %s\n' "${CMDLINE}" >> "${STAGE}/boot/limine/limine.conf"
fi
cp "${LIMINE_DIR}/limine-bios.sys" "${LIMINE_DIR}/limine-bios-cd.bin" \
   "${LIMINE_DIR}/limine-uefi-cd.bin" "${STAGE}/boot/limine/"
cp "${LIMINE_DIR}/BOOTX64.EFI" "${LIMINE_DIR}/BOOTIA32.EFI" "${STAGE}/EFI/BOOT/"

xorriso -as mkisofs -quiet -R -r -J \
    -b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table \
    -hfsplus -apm-block-size 2048 \
    --efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part --efi-boot-image \
    --protective-msdos-label \
    "${STAGE}" -o "${ISO}"

"${LIMINE_DIR}/limine" bios-install "${ISO}" >/dev/null 2>&1 || "${LIMINE_DIR}/limine" bios-install "${ISO}"
echo "iso: ${ISO}"
