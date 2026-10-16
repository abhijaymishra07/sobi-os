#!/usr/bin/env bash
set -euo pipefail

LIMINE_VERSION="12.9.3"
LIMINE_SHA256="e02a473c6dc6df25159697f94930272397f21fd86e5d4eeee61bba72c95ec8d2"
LIMINE_URL="https://github.com/limine-bootloader/limine/releases/download/v${LIMINE_VERSION}/limine-binary.tar.xz"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${ROOT}/third_party/limine"
STAMP="${DEST}/.version"

if [[ -f "${STAMP}" && "$(cat "${STAMP}")" == "${LIMINE_VERSION}" && -x "${DEST}/limine" ]]; then
    echo "limine ${LIMINE_VERSION} already present in ${DEST}"
    exit 0
fi

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT
ARCHIVE="${WORK}/limine-binary.tar.xz"

if [[ -n "${LIMINE_ARCHIVE:-}" ]]; then
    echo "using local archive ${LIMINE_ARCHIVE}"
    cp "${LIMINE_ARCHIVE}" "${ARCHIVE}"
else
    echo "downloading ${LIMINE_URL}"
    attempt=1
    until curl -fL --retry 3 --connect-timeout 20 --max-time 600 -o "${ARCHIVE}" "${LIMINE_URL}"; do
        if (( attempt >= 4 )); then
            echo "download failed after ${attempt} attempts" >&2
            echo "set LIMINE_ARCHIVE=/path/to/limine-binary.tar.xz to use a local copy" >&2
            exit 1
        fi
        attempt=$((attempt + 1))
        echo "retrying (${attempt})..."
        sleep 5
    done
fi

ACTUAL="$(sha256sum "${ARCHIVE}" | cut -d' ' -f1)"
if [[ "${ACTUAL}" != "${LIMINE_SHA256}" ]]; then
    echo "sha256 mismatch for limine-binary.tar.xz" >&2
    echo "  expected ${LIMINE_SHA256}" >&2
    echo "  actual   ${ACTUAL}" >&2
    exit 1
fi
echo "sha256 verified"

rm -rf "${DEST}"
mkdir -p "${DEST}"
tar -xJf "${ARCHIVE}" -C "${WORK}"
cp "${WORK}/limine-binary/"{limine-bios.sys,limine-bios-cd.bin,limine-uefi-cd.bin,BOOTX64.EFI,BOOTIA32.EFI,limine.c,limine-bios-hdd.h,Makefile,LICENSE} "${DEST}/"

echo "building host limine tool"
make -C "${DEST}" -s limine CC="${HOST_CC:-cc}"
"${DEST}/limine" --version

echo "${LIMINE_VERSION}" > "${STAMP}"
echo "limine ${LIMINE_VERSION} installed to ${DEST}"
