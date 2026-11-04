#!/bin/bash
# 发布二进制必须有效签名，且符号表只保留动态导入和 Mach-O 入口头。
set -euo pipefail

if [[ "$#" -ne 1 || ! -x "$1" ]]; then
  echo "usage: tests/check_release_binary.sh <release-binary>" >&2
  exit 2
fi

binary="$1"
if ! /usr/bin/codesign --verify --strict "$binary" >/dev/null 2>&1; then
  echo "release binary has an invalid code signature: $binary" >&2
  exit 1
fi

defined_symbols="$(
  /usr/bin/nm -a "$binary" 2>/dev/null |
    awk 'NF >= 2 {
      symbol_type = $(NF - 1)
      symbol_name = $NF
      if (symbol_type ~ /^[A-Za-z]$/ && symbol_type != "U" &&
          symbol_name != "__mh_execute_header") {
        print
      }
    }'
)"
if [[ -n "$defined_symbols" ]]; then
  echo "release binary still contains defined symbols:" >&2
  printf '%s\n' "$defined_symbols" | head -20 >&2
  exit 1
fi

if /usr/bin/otool -l "$binary" | grep -q '__DWARF'; then
  echo "release binary still contains DWARF debug sections" >&2
  exit 1
fi

echo "release binary symbols stripped: $binary"
