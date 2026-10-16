#!/usr/bin/env bash
set -uo pipefail

[[ $# -eq 1 ]] || { echo "usage: $0 <kernel.elf>" >&2; exit 2; }
K="$1"
[[ -f "${K}" ]] || { echo "no such file: ${K}" >&2; exit 2; }

HIGHER_HALF=0xffffffff80000000
fail=0

ok()   { printf '  ok    %s\n' "$1"; }
bad()  { printf '  FAIL  %s\n' "$1"; fail=1; }
check() { if eval "$1"; then ok "$2"; else bad "$2"; fi; }

echo "verify-elf: ${K}"

hdr="$(readelf -hW "${K}")"
check "grep -q 'Class:.*ELF64' <<<\"\${hdr}\"" "class is ELF64"
check "grep -q 'Type:.*EXEC' <<<\"\${hdr}\"" "type is EXEC (not DYN)"
check "grep -q 'Machine:.*X86-64' <<<\"\${hdr}\"" "machine is X86-64"

entry="$(awk '/Entry point address/ {print $NF}' <<<"${hdr}")"
start_sym="0x$(nm "${K}" | awk '$3 == "_start" {print $1}')"
check "[[ \$((entry)) -eq \$((start_sym)) ]]" "entry ${entry} == _start ${start_sym}"
check "[[ \$((entry)) -ge \$((HIGHER_HALF)) ]]" "entry is in the higher half"

phdrs="$(readelf -lW "${K}")"
loads="$(grep -E '^\s+LOAD' <<<"${phdrs}")"
nload="$(grep -c . <<<"${loads}")"
check "[[ ${nload} -eq 3 ]]" "exactly 3 PT_LOAD segments (found ${nload})"

flags="$(awk '{f=""; for(i=7;i<=NF-1;i++) f=f $i; print f}' <<<"${loads}" | tr '\n' ' ')"
check "[[ '${flags}' == 'RE R RW ' ]]" "PT_LOAD flags are R E, R, RW (found: ${flags})"

while read -r _ _ vaddr _ _ _ rest; do
    align="$(awk '{print $NF}' <<<"${rest}")"
    check "[[ \$((vaddr)) -ge \$((HIGHER_HALF)) ]]" "PT_LOAD vaddr ${vaddr} in higher half"
    check "[[ '${align}' == '0x1000' ]]" "PT_LOAD at ${vaddr} aligned 0x1000 (found ${align})"
    check "[[ \$((vaddr % 0x1000)) -eq 0 ]]" "PT_LOAD at ${vaddr} starts on a page boundary"
done <<<"${loads}"

check "! grep -qE '^\s+(INTERP|DYNAMIC)' <<<\"\${phdrs}\"" "no PT_INTERP / PT_DYNAMIC"
check "! grep -E '^\s+GNU_STACK' <<<\"\${phdrs}\" | grep -q 'RWE'" "no executable GNU_STACK"

secs="$(readelf -SW "${K}")"
rw_line="$(awk '$1=="LOAD" && $7=="RW" {print $3, $6}' <<<"${phdrs}")"
rw_start="$(cut -d' ' -f1 <<<"${rw_line}")"
rw_memsz="$(cut -d' ' -f2 <<<"${rw_line}")"
rw_end=$(( rw_start + rw_memsz ))

prev=$((HIGHER_HALF))
for s in .limine_requests_start .limine_requests .limine_requests_end; do
    line="$(grep -E "\] ${s} " <<<"${secs}")"
    if [[ -z "${line}" ]]; then
        bad "section ${s} present"
        continue
    fi
    ok "section ${s} present"
    addr="0x$(awk '{for(i=1;i<=NF;i++) if($i=="PROGBITS") print $(i+1)}' <<<"${line}")"
    sflags="$(awk '{for(i=1;i<=NF;i++) if($i=="PROGBITS") print $(i+5)}' <<<"${line}")"
    check "[[ '${sflags}' == 'WA' ]]" "${s} flags WA (found ${sflags})"
    check "[[ \$((addr % 8)) -eq 0 ]]" "${s} at ${addr} is 8-byte aligned"
    check "[[ \$((addr)) -ge ${rw_start} && \$((addr)) -lt ${rw_end} ]]" "${s} inside RW PT_LOAD"
    check "[[ \$((addr)) -gt ${prev} ]]" "${s} ordered after previous request section"
    prev=$((addr))
done

undef="$(nm -u "${K}")"
check "[[ -z '${undef}' ]]" "no undefined symbols"
[[ -n "${undef}" ]] && printf '%s\n' "${undef}"

check "readelf -rW \"${K}\" | grep -q 'There are no relocations'" "no relocation sections"

stack_top="0x$(nm "${K}" | awk '$3 == "boot_stack_top" {print $1}')"
check "[[ \$((stack_top % 16)) -eq 0 ]]" "boot_stack_top ${stack_top} is 16-byte aligned"

dis="$(objdump -d --no-show-raw-insn "${K}" | awk '/<_start>:/,/^$/')"
check "grep -qE 'lea .*,%rsp' <<<\"\${dis}\"" "_start loads rsp from boot_stack_top"
check "grep -qE 'xor +%ebp,%ebp' <<<\"\${dis}\"" "_start zeroes rbp"
check "grep -qE 'call .*<kmain>' <<<\"\${dis}\"" "_start calls kmain"
check "grep -qE '^\s*[0-9a-f]+:\s+hlt' <<<\"\${dis}\"" "_start halts after kmain returns"
rsp_line="$(grep -nE 'lea .*,%rsp' <<<"${dis}" | cut -d: -f1)"
call_line="$(grep -nE 'call .*<kmain>' <<<"${dis}" | cut -d: -f1)"
pushes="$(sed -n "${rsp_line},${call_line}p" <<<"${dis}" | grep -cE 'push|sub .*%rsp')"
check "[[ ${pushes} -eq 0 ]]" "rsp untouched between stack load and call (rsp % 16 == 0 at call)"

if [[ ${fail} -eq 0 ]]; then
    echo "verify-elf: PASS"
    exit 0
fi
echo "verify-elf: FAIL"
exit 1
