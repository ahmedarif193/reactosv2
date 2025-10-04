#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <uefi.efi> <out.symlite>" >&2
  exit 2
fi

EFI="$1"
OUT="$2"

# Pick an nm that can read PE/COFF; try host then cross
NM_BIN="${NM_BIN:-nm}"
if ! "$NM_BIN" -n "$EFI" >/dev/null 2>&1; then
  NM_BIN="x86_64-w64-mingw32-nm"
fi

if ! "$NM_BIN" -n "$EFI" >/dev/null 2>&1; then
  echo "Error: no suitable nm found for $EFI" >&2
  exit 1
fi

# Generate simple RVA->name map for text symbols, subtracting PE image base (0x10000)
"$NM_BIN" -n "$EFI" \
 | awk '($2 ~ /^[Tt]$/){ \
         addr = strtonum("0x" $1) - 0x10000; \
         if (addr < 0) addr = 0; \
         printf "%08X %s\n", addr, $3 \
       }' \
 > "$OUT"

