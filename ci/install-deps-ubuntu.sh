#!/usr/bin/env bash
# =============================================================================
# ci/install-deps-ubuntu.sh — Ubuntu 24.04 依赖安装
# 兼容 root（容器）与 GitHub runner（非 root + passwordless sudo）。
# 安装：cmake≥4.4.4 / gcc 16 / clang++ 23.1 / libc++ / ninja / just / grc
#       / spdlog / fmt / gtest / benchmark / doxygen / ccache
# bqlog：官方方式 brew install bqlog（Linux 使用 Linuxbrew）；
# 无 Linuxbrew 时模板 warning 并跳过（可选依赖，非阻断）。
# =============================================================================
set -euo pipefail

GCC_VERSION="${GCC_VERSION:-16}"
LLVM_VERSION="${LLVM_VERSION:-23}"
GITHUB_PROXY="${GITHUB_PROXY:-}"

download_github() {
  local url="$1" output="$2"
  local request_url="$url"
  if [ -n "$GITHUB_PROXY" ]; then
    request_url="${GITHUB_PROXY%/}/${url}"
  fi
  echo "download: ${request_url}"
  curl -fL --silent --show-error \
    --retry 3 --retry-delay 2 --connect-timeout 20 \
    -o "$output" "$request_url"
}

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
  SUDO="sudo"
fi
export DEBIAN_FRONTEND=noninteractive

$SUDO apt-get update
$SUDO apt-get install -y --no-install-recommends \
  ca-certificates curl software-properties-common

# GCC 16 不在 Ubuntu 24.04 官方仓库中；使用 Ubuntu Toolchain PPA。
$SUDO add-apt-repository -y ppa:ubuntu-toolchain-r/test
$SUDO apt-get update

$SUDO apt-get install -y --no-install-recommends \
  git xz-utils build-essential gcc-${GCC_VERSION} g++-${GCC_VERSION} \
  ninja-build ccache \
  python3 zsh pkg-config libspdlog-dev libfmt-dev libgtest-dev libbenchmark-dev \
  doxygen

# 增强工具：缺失时模板自动降级，不阻断流水线
$SUDO apt-get install -y --no-install-recommends grc || echo "warn: grc unavailable"
# IWYU 版本与 Clang 23 常不匹配：不强制安装，just iwyu 会自动跳过
echo "info: IWYU 未安装（可选工具，just iwyu 将降级跳过）"

# ---- CMake ≥4.4.4（官方二进制，不依赖 Python/pip）----
CASE_ARCH="$(dpkg --print-architecture)"
case "${CASE_ARCH}" in
  amd64) CMAKE_ARCH="x86_64"; JUST_ARCH="x86_64" ;;
  arm64) CMAKE_ARCH="aarch64"; JUST_ARCH="aarch64" ;;
  *)
    echo "error: unsupported Debian architecture: ${CASE_ARCH}" >&2
    exit 1
    ;;
esac

_cmake_need=1
if command -v cmake >/dev/null 2>&1; then
  _cmake_ver="$(cmake --version | head -1 | awk '{print $3}')"
  if [ "$(printf '%s\n%s\n' "4.4.4" "$_cmake_ver" | sort -V | head -1)" = "4.4.4" ]; then
    _cmake_need=0
  fi
fi
if [ "$_cmake_need" -eq 1 ]; then
  CMAKE_VERSION_PIN="${CMAKE_VERSION_PIN:-4.4.5}"
  CMAKE_TARBALL="cmake-${CMAKE_VERSION_PIN}-linux-${CMAKE_ARCH}.tar.gz"
  download_github \
    "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION_PIN}/${CMAKE_TARBALL}" \
    "/tmp/${CMAKE_TARBALL}"
  $SUDO mkdir -p /opt
  $SUDO tar -xzf "/tmp/${CMAKE_TARBALL}" -C /opt
  for tool in cmake ctest cpack; do
    $SUDO ln -sf "/opt/cmake-${CMAKE_VERSION_PIN}-linux-${CMAKE_ARCH}/bin/${tool}" \
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
  download_github \
    "https://github.com/casey/just/releases/download/${JUST_VERSION}/just-${JUST_VERSION}-${JUST_ARCH}-unknown-linux-musl.tar.gz" \
    "/tmp/just.tar.gz"
  $SUDO tar -xzf /tmp/just.tar.gz -C /usr/local/bin just
fi

# ---- clang 23.1（apt.llvm.org）----
if ! command -v "clang++-${LLVM_VERSION}" >/dev/null 2>&1; then
  curl -fsSL -o /tmp/llvm.sh https://apt.llvm.org/llvm.sh
  $SUDO bash /tmp/llvm.sh "${LLVM_VERSION}"
fi
$SUDO apt-get install -y --no-install-recommends \
  clang-tools-${LLVM_VERSION} llvm-${LLVM_VERSION} lld-${LLVM_VERSION} \
  lldb-${LLVM_VERSION} libc++-${LLVM_VERSION}-dev \
  libc++abi-${LLVM_VERSION}-dev
# libc++ 运行库（-stdlib=libc++ 链接 -pthread 检查所必需）
$SUDO apt-get install -y --no-install-recommends libc++1 libc++abi1 \
  || echo "warn: libc++ runtime package name mismatch; check apt.llvm.org"
$SUDO apt-get install -y --no-install-recommends \
  llvm-libunwind1 libunwind-${LLVM_VERSION}-dev \
  || echo "warn: libunwind package name mismatch; check apt.llvm.org"
$SUDO apt-get install -y --no-install-recommends \
  clang-format-${LLVM_VERSION} clang-tidy-${LLVM_VERSION} \
  || echo "warn: clang-format/clang-tidy unavailable (use clang-tools binaries)"
$SUDO ln -sfn /usr/bin/gcc-${GCC_VERSION} /usr/local/bin/gcc
$SUDO ln -sfn /usr/bin/g++-${GCC_VERSION} /usr/local/bin/g++
$SUDO ln -sfn /usr/lib/llvm-${LLVM_VERSION}/bin/clang   /usr/local/bin/clang
$SUDO ln -sfn /usr/lib/llvm-${LLVM_VERSION}/bin/clang++ /usr/local/bin/clang++
$SUDO ln -sfn /usr/lib/llvm-${LLVM_VERSION}/bin/clang-scan-deps /usr/local/bin/clang-scan-deps
$SUDO ln -sfn /usr/lib/llvm-${LLVM_VERSION}/bin/clang-format /usr/local/bin/clang-format
$SUDO ln -sfn /usr/lib/llvm-${LLVM_VERSION}/bin/clang-tidy   /usr/local/bin/clang-tidy
$SUDO ln -sfn /usr/lib/llvm-${LLVM_VERSION}/bin/clang-doc    /usr/local/bin/clang-doc
$SUDO ln -sfn /usr/lib/llvm-${LLVM_VERSION}/bin/llvm-ar      /usr/local/bin/llvm-ar
$SUDO ln -sfn /usr/lib/llvm-${LLVM_VERSION}/bin/llvm-ranlib  /usr/local/bin/llvm-ranlib
$SUDO ln -sfn /usr/lib/llvm-${LLVM_VERSION}/bin/llvm-cov     /usr/local/bin/llvm-cov
$SUDO ln -sfn /usr/lib/llvm-${LLVM_VERSION}/bin/llvm-profdata /usr/local/bin/llvm-profdata

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
g++ --version | head -1
clang++ --version | head -1
just --version
ninja --version
grcat --version >/dev/null 2>&1 && echo "grcat: ok" || echo "grcat: missing (着色降级)"
