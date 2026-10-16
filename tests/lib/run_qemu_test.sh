#!/usr/bin/env bash
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TEST_BUILD="${ROOT}/build/test"
KERNEL="${TEST_BUILD}/sobi-kernel.elf"
QEMU_EXIT_PASS=33
QEMU_EXIT_PANIC=35
QEMU_EXIT_FAIL=37
QEMU_EXIT_TIMEOUT=124

run_qemu_test() {
    local name="$1"
    local cmdline="$2"
    local expect_exit="$3"
    shift 3
    local required=("$@")

    local iso="${TEST_BUILD}/${name}.iso"
    local log="${TEST_BUILD}/${name}.serial.log"
    local TIMEOUT_S="${SOBI_TEST_TIMEOUT:-30}"
    local QEMU_MEM="${QEMU_MEM:-256M}"

    [[ -f "${KERNEL}" ]] || { echo "[${name}] missing ${KERNEL}; run 'make BUILD=test'" >&2; return 1; }

    if ! "${ROOT}/scripts/mkiso.sh" "${KERNEL}" "${ROOT}/boot/limine.conf" "${iso}" "${cmdline}" \
            >"${TEST_BUILD}/${name}.mkiso.log" 2>&1; then
        echo "FAIL  ${name}  ISO creation failed"
        cat "${TEST_BUILD}/${name}.mkiso.log"
        return 1
    fi

    : > "${log}"
    timeout --foreground "${TIMEOUT_S}" qemu-system-x86_64 \
        -M q35 -m "${QEMU_MEM}" -no-reboot \
        -cdrom "${iso}" \
        -display none -serial "file:${log}" \
        -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
        ${SOBI_QEMU_EXTRA:-} >/dev/null 2>&1
    local rc=$?

    local verdict=""
    case "${rc}" in
        "${expect_exit}") verdict="" ;;
        "${QEMU_EXIT_TIMEOUT}") verdict="timeout after ${TIMEOUT_S}s (kernel hung)" ;;
        "${QEMU_EXIT_PANIC}") verdict="kernel panic (exit ${rc})" ;;
        "${QEMU_EXIT_PASS}") verdict="kernel reported pass but test expected exit ${expect_exit}" ;;
        "${QEMU_EXIT_FAIL}") verdict="kernel reported failure (exit ${rc})" ;;
        0) verdict="qemu exited 0 (triple fault / reset / shutdown without debug-exit)" ;;
        *) verdict="unexpected qemu exit status ${rc}" ;;
    esac

    if [[ -z "${verdict}" ]]; then
        local pat
        for pat in "${required[@]}"; do
            if ! grep -qF -- "${pat}" "${log}"; then
                verdict="serial log missing required text: '${pat}'"
                break
            fi
        done
    fi

    if [[ -z "${verdict}" ]]; then
        echo "PASS  ${name}  (qemu exit ${rc}, $(wc -l < "${log}") serial lines)"
        return 0
    fi

    echo "FAIL  ${name}  ${verdict}"
    echo "----- last 40 serial lines (${log}) -----"
    tail -n 40 "${log}"
    echo "-----------------------------------------"
    return 1
}
