#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 2 ]] || { echo "usage: $0 <kernel.elf> <out.c>" >&2; exit 2; }
ELF="$1"
OUT="$2"

tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT

nm -n "$ELF" | python3 -c '
import sys, re
pat = re.compile(r"^[A-Za-z_][A-Za-z0-9_.]*$")
for line in sys.stdin:
    parts = line.split()
    if len(parts) < 3:
        continue
    addr, typ, name = parts[0], parts[1], parts[2]
    if typ not in ("T", "t"):
        continue
    if not pat.match(name):
        continue
    if int(addr, 16) < 0xffffffff80000000:
        continue
    print(addr, name)
' > "$tmp"

{
    echo '#include <sobi/types.h>'
    echo '#include <sobi/ksym.h>'
    echo 'const struct ksym ksym_table[] = {'
    while read -r addr name; do
        printf '    { 0x%sULL, "%s" },\n' "$addr" "$name"
    done < "$tmp"
    echo '};'
    echo "const size_t ksym_count = sizeof(ksym_table) / sizeof(ksym_table[0]);"
} > "$OUT"

count="$(wc -l < "$tmp")"
echo "gensyms: ${count} symbols -> ${OUT}"
