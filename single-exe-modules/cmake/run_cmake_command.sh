#!/usr/bin/env bash
# =============================================================================
# run_cmake_command.sh — 以独立亮黄色回显并执行 CMake 命令
#
# 用法:
#   run_cmake_command.sh "<label>" -- <command> [args...]
#
# 只给命令回显和最终状态着色；命令自身 stdout/stderr 保持原样，避免吞掉
# CMake/Ninja 的分级颜色、warning 和 error，也保持原始退出码。
#
# 颜色与普通 yellow（1;33，终端中偏灰黄）分离：
#   CXX_CMAKE_BRIGHT_YELLOW  默认 ANSI 256 色 226（亮黄）
#   CXX_CMAKE_COMMAND_COLOR  可选环境变量覆盖
# =============================================================================
set -euo pipefail

if [[ $# -lt 3 || "$2" != "--" ]]; then
  printf 'usage: %s <label> -- <command> [args...]\n' "$0" >&2
  exit 2
fi

label="$1"
shift 2

cxx_cmake_bright_yellow="${CXX_CMAKE_COMMAND_COLOR:-$'\033[38;5;226m'}"
cxx_color_reset=$'\033[0m'

printf '\n%s[cmake] %s%s\n' "$cxx_cmake_bright_yellow" "$label" "$cxx_color_reset"
printf '%s$' "$cxx_cmake_bright_yellow"
printf ' %q' "$@"
printf '%s\n' "$cxx_color_reset"

set +e
"$@"
rc=$?
set -e

if ((rc == 0)); then
  printf '%s[cmake] %s: ok%s\n' "$cxx_cmake_bright_yellow" "$label" "$cxx_color_reset"
else
  printf '%s[cmake] %s: failed (exit %d)%s\n' \
    "$cxx_cmake_bright_yellow" "$label" "$rc" "$cxx_color_reset" >&2
fi
exit "$rc"
