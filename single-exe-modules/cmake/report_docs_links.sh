#!/usr/bin/env bash
# 打印可点击的 Doxygen / clang-doc HTML 入口（粗体黄色，OSC 8 超链接）。
set -euo pipefail

template_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-build}"
case "$build_dir" in
  /*) ;;
  *) build_dir="${template_root}/${build_dir}" ;;
esac

emit_link() {
  local backend="$1"
  local index_path="$2"
  if [ -f "$index_path" ]; then
    local uri_path="${index_path// /%20}"
    printf '\033[1;33m%s 文档：\033]8;;file://%s\033\\点击此链接查看 %s HTML 文档\033]8;;\033\\ → %s\033[0m\n' \
      "$backend" "$uri_path" "$backend" "$index_path"
  else
    printf '\033[1;33m%s 文档：未生成（%s）\033[0m\n' "$backend" "$index_path"
  fi
}

emit_link "Doxygen" "${build_dir}/docs/doxygen/html/index.html"
emit_link "clang-doc" "${build_dir}/docs/clang-doc/html/index.html"
