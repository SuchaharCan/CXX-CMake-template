#!/usr/bin/env bash
# =============================================================================
# ci/install-deps-ubuntu.sh — GitHub Actions（ubuntu-24.04）依赖安装
# 兼容 root（容器）与 GitHub runner（非 root + passwordless sudo）。
# 安装：cmake≥4.4.4 / clang++ 23.1 / libc++ / ninja / just / grc
#       / spdlog / fmt / gtest / benchmark / doxygen / ccache
# bqlog 不在 Ubuntu 仓库：模板会 warning 并跳过（非阻断）。
# =============================================================================
set -euo pipefail

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
  SUDO="sudo"
fi
export DEBIAN_FRONTEND=noninteractive

$SUDO apt-get update
$SUDO apt-get install -y --no-install-recommends \
  ca-certificates curl git xz-utils build-essential ninja-build ccache \
  python3 python3-pip libspdlog-dev libfmt-dev libgtest-dev libbenchmark-dev \
  doxygen

# 增强工具：缺失时模板自动降级，不阻断流水线
$SUDO apt-get install -y --no-install-recommends grc || echo "warn: grc unavailable"
$SUDO apt-get install -y --no-install-recommends iwyu || echo "warn: iwyu unavailable"

# ---- CMake ≥4.4.4 ----
_cmake_need=1
if command -v cmake >/dev/null 2>&1; then
  _cmake_ver="$(cmake --version | head -1 | awk '{print $3}')"
  if [ "$(printf '%s\n%s\n' "4.4.4" "$_cmake_ver" | sort -V | head -1)" = "4.4.4" ]; then
    _cmake_need=0
  fi
fi
if [ "$_cmake_need" -eq 1 ]; then
  $SUDO python3 -m pip install --break-system-packages "cmake>=4.4.4"
fi

# ---- just ≥1.20 ----
JUST_VERSION="${JUST_VERSION:-1.59.0}"
_just_need=1
if command -v just >/dev/null 2>&1; then
  _just_ver="$(just --version | awk '{print $2}')"
  if [ "$(printf '%s\n%s\n' "1.20.0" "$_just_ver" | sort -V | head -1)" = "1.20.0" ]; then
    _just_need=0
  fi
fi
if [ "$_just_need" -eq 1 ]; then
  curl -fsSL -o /tmp/just.tar.gz \
    "https://github.com/casey/just/releases/download/${JUST_VERSION}/just-${JUST_VERSION}-x86_64-unknown-linux-musl.tar.gz"
  $SUDO tar -xzf /tmp/just.tar.gz -C /usr/local/bin just
fi

# ---- clang 23.1（apt.llvm.org）----
if ! command -v clang++-23 >/dev/null 2>&1; then
  curl -fsSL -o /tmp/llvm.sh https://apt.llvm.org/llvm.sh
  $SUDO bash /tmp/llvm.sh 23
fi
$SUDO apt-get install -y --no-install-recommends \
  clang-tools-23 libc++-23-dev libc++abi-23-dev
$SUDO apt-get install -y --no-install-recommends clang-format-23 clang-tidy-23 \
  || echo "warn: clang-format-23/clang-tidy-23 unavailable (use clang-tools-23 binaries)"
$SUDO ln -sf /usr/lib/llvm-23/bin/clang   /usr/local/bin/clang
$SUDO ln -sf /usr/lib/llvm-23/bin/clang++ /usr/local/bin/clang++
$SUDO ln -sf /usr/lib/llvm-23/bin/clang-scan-deps /usr/local/bin/clang-scan-deps
$SUDO ln -sf /usr/lib/llvm-23/bin/clang-format /usr/local/bin/clang-format
$SUDO ln -sf /usr/lib/llvm-23/bin/clang-tidy   /usr/local/bin/clang-tidy
$SUDO ln -sf /usr/lib/llvm-23/bin/clang-doc    /usr/local/bin/clang-doc
$SUDO ln -sf /usr/lib/llvm-23/bin/llvm-ar      /usr/local/bin/llvm-ar
$SUDO ln -sf /usr/lib/llvm-23/bin/llvm-ranlib  /usr/local/bin/llvm-ranlib

# ---- 版本自检 ----
cmake --version | head -1
clang++ --version | head -1
just --version
ninja --version
grcat --version >/dev/null 2>&1 && echo "grcat: ok" || echo "grcat: missing (着色降级)"
