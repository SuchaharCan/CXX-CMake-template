#!/usr/bin/env bash
# =============================================================================
# ci/install-deps-ubuntu.sh — GitHub Actions（ubuntu-24.04）依赖安装
# 兼容 root（容器）与 GitHub runner（非 root + passwordless sudo）。
# 安装：cmake≥4.4.4 / clang++ 23.1 / libc++ / ninja / just / grc
#       / spdlog / fmt / gtest / benchmark / doxygen / ccache
# bqlog：官方方式 brew install bqlog（Linux 使用 Linuxbrew）；
# 无 Linuxbrew 时模板 warning 并跳过（可选依赖，非阻断）。
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
  python3 zsh pkg-config libspdlog-dev libfmt-dev libgtest-dev libbenchmark-dev \
  doxygen

# 增强工具：缺失时模板自动降级，不阻断流水线
$SUDO apt-get install -y --no-install-recommends grc || echo "warn: grc unavailable"
# IWYU 版本与 Clang 23 常不匹配：不强制安装，just iwyu 会自动跳过
echo "info: IWYU 未安装（可选工具，just iwyu 将降级跳过）"

# ---- CMake ≥4.4.4（官方二进制，不依赖 Python/pip）----
_cmake_need=1
if command -v cmake >/dev/null 2>&1; then
  _cmake_ver="$(cmake --version | head -1 | awk '{print $3}')"
  if [ "$(printf '%s\n%s\n' "4.4.4" "$_cmake_ver" | sort -V | head -1)" = "4.4.4" ]; then
    _cmake_need=0
  fi
fi
if [ "$_cmake_need" -eq 1 ]; then
  CMAKE_VERSION_PIN="${CMAKE_VERSION_PIN:-4.4.5}"
  CMAKE_TARBALL="cmake-${CMAKE_VERSION_PIN}-linux-x86_64.tar.gz"
  curl -fsSL -o "/tmp/${CMAKE_TARBALL}" \
    "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION_PIN}/${CMAKE_TARBALL}"
  $SUDO mkdir -p /opt
  $SUDO tar -xzf "/tmp/${CMAKE_TARBALL}" -C /opt
  for tool in cmake ctest cpack; do
    $SUDO ln -sf "/opt/cmake-${CMAKE_VERSION_PIN}-linux-x86_64/bin/${tool}" \
      "/usr/local/bin/${tool}"
  done
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
# libc++ 运行库（-stdlib=libc++ 链接 -pthread 检查所必需）
$SUDO apt-get install -y --no-install-recommends libc++1 libc++abi1 \
  || echo "warn: libc++ runtime package name mismatch; check apt.llvm.org"
$SUDO apt-get install -y --no-install-recommends llvm-libunwind1 libunwind-23-dev \
  || echo "warn: libunwind package name mismatch; check apt.llvm.org"
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

# ---- bqlog（官方推荐 brew install bqlog；Linux 使用 Linuxbrew）----
BREW_BIN=""
if command -v brew >/dev/null 2>&1; then
  BREW_BIN="$(command -v brew)"
elif [ -x /home/linuxbrew/.linuxbrew/bin/brew ]; then
  BREW_BIN="/home/linuxbrew/.linuxbrew/bin/brew"
fi
if [ -n "$BREW_BIN" ]; then
  if ! "$BREW_BIN" list --versions bqlog >/dev/null 2>&1; then
    if ! "$BREW_BIN" info --formula bqlog >/dev/null 2>&1; then
      echo "Homebrew index missing bqlog — running brew update"
      "$BREW_BIN" update --quiet || true
    fi
    "$BREW_BIN" install bqlog || echo "warn: bqlog brew install failed (optional)"
  fi
else
  echo "info: Linuxbrew 未安装；bqlog 跳过（可选依赖）。安装方式：brew install bqlog"
fi

# ---- 预检：-stdlib=libc++ + -pthread 必须能编译链接（spdlog/benchmark 依赖 Threads）----
printf '#include <thread>\nint main(){ std::thread t([]{}); t.join(); return 0; }\n' > /tmp/pthread_probe.cpp
if ! clang++ -std=c++23 -stdlib=libc++ -pthread /tmp/pthread_probe.cpp -o /tmp/pthread_probe; then
  echo "error: libc++ -pthread link failed — check libc++ runtime packages" >&2
  clang++ -v -std=c++23 -stdlib=libc++ -pthread /tmp/pthread_probe.cpp -o /tmp/pthread_probe 2>&1 | tail -20 >&2
  exit 1
fi
echo "libc++ -pthread link: ok"

# ---- 版本自检 ----
cmake --version | head -1
clang++ --version | head -1
just --version
ninja --version
grcat --version >/dev/null 2>&1 && echo "grcat: ok" || echo "grcat: missing (着色降级)"
