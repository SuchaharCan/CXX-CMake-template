#!/usr/bin/env bash
# =============================================================================
# ci/install-deps-macos.sh — GitHub macOS runner / 自建 macOS 27 依赖安装
# 用途：Homebrew 补齐 cmake≥4.5.0 / clang++≥23.1 / ninja / just / grc
#       / spdlog / fmt / googletest / google-benchmark / doxygen / ccache / bqlog
# 模式：默认缺失则 brew install；版本不足则 brew upgrade；
#       CI_DEPS_CHECK_ONLY=1 只检查不安装。
# 环境：macOS 26/27（arm64），已装 Homebrew + Xcode Command Line Tools。
# =============================================================================
set -euo pipefail

CHECK_ONLY="${CI_DEPS_CHECK_ONLY:-0}"
SUDO_PREFIX=""
if [ "$(id -u)" -ne 0 ]; then
  SUDO_PREFIX="sudo"
fi

if ! command -v brew >/dev/null 2>&1; then
  echo "error: Homebrew not found (https://brew.sh)" >&2
  exit 1
fi
if ! xcode-select -p >/dev/null 2>&1; then
  echo "error: Xcode Command Line Tools not installed (xcode-select --install)" >&2
  exit 1
fi

export HOMEBREW_NO_AUTO_UPDATE=1

FORMULAE="cmake ninja just grc ccache spdlog fmt googletest google-benchmark doxygen graphviz llvm"

missing=()
for f in $FORMULAE; do
  if ! brew list --versions "$f" >/dev/null 2>&1; then
    missing+=("$f")
  fi
done

if [ "${#missing[@]}" -gt 0 ]; then
  if [ "$CHECK_ONLY" = "1" ]; then
    echo "error: missing Homebrew formulae: ${missing[*]}" >&2
    exit 1
  fi
  echo "installing: ${missing[*]}"
  brew install "${missing[@]}"
fi

# HOMEBREW_NO_AUTO_UPDATE 会保留跑器镜像里的旧索引；cmake 4.5.0 需要新索引。
# 先显式刷新索引，避免 `brew upgrade cmake` 因索引过旧而空转。
if [ "$CHECK_ONLY" = "1" ]; then
  echo "info: CI_DEPS_CHECK_ONLY — 跳过 Homebrew 索引刷新"
else
  echo "refreshing Homebrew index"
  brew update --quiet || echo "warn: brew update failed（沿用现有索引）"
fi

check_min() {
  local actual="$1" minimum="$2" label="$3"
  if [ "$(printf '%s\n%s\n' "$minimum" "$actual" | sort -V | head -1)" != "$minimum" ]; then
    echo "error: ${label} ${actual} < ${minimum}" >&2
    return 1
  fi
  return 0
}

cmake_ver="$(cmake --version | head -1 | awk '{print $3}')"
just_ver="$(just --version | awk '{print $2}')"
clang_ver="$(/opt/homebrew/opt/llvm/bin/clang++ --version | head -1 | sed -E 's/.* version ([0-9.]+).*/\1/')"

upgrade=()
check_min "$cmake_ver" "4.5.0" cmake || upgrade+=(cmake)
check_min "$just_ver" "1.20.0" just || upgrade+=(just)
check_min "$clang_ver" "23.1.0" clang++ || upgrade+=(llvm)

if [ "${#upgrade[@]}" -gt 0 ]; then
  if [ "$CHECK_ONLY" = "1" ]; then
    echo "error: upgrade required for: ${upgrade[*]}" >&2
    exit 1
  fi
  echo "upgrading: ${upgrade[*]}"
  brew upgrade "${upgrade[@]}"
  cmake_ver="$(cmake --version | head -1 | awk '{print $3}')"
  just_ver="$(just --version | awk '{print $2}')"
  clang_ver="$(/opt/homebrew/opt/llvm/bin/clang++ --version | head -1 | sed -E 's/.* version ([0-9.]+).*/\1/')"
fi

# ---- cmake 回退：Homebrew 若仍无 4.5.0 bottle，安装 Kitware 官方二进制 ----
cmake_ver="$(cmake --version | head -1 | awk '{print $3}')"
if [ "$(printf '%s\n%s\n' "4.5.0" "$cmake_ver" | sort -V | head -1)" != "4.5.0" ]; then
  if [ "$CHECK_ONLY" = "1" ]; then
    echo "error: cmake ${cmake_ver} < 4.5.0（CHECK_ONLY 模式不安装官方二进制）" >&2
    exit 1
  fi
  CMAKE_VERSION_PIN="${CMAKE_VERSION_PIN:-4.5.0}"
  CMAKE_TARBALL="cmake-${CMAKE_VERSION_PIN}-macos-universal.tar.gz"
  echo "installing official cmake ${CMAKE_VERSION_PIN}（Homebrew bottle 不可用）"
  curl -fL --silent --show-error --retry 3 --retry-delay 2 \
    -o "/tmp/${CMAKE_TARBALL}" \
    "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION_PIN}/${CMAKE_TARBALL}"
  $SUDO_PREFIX mkdir -p /opt
  $SUDO_PREFIX tar -xzf "/tmp/${CMAKE_TARBALL}" -C /opt
  for tool in cmake ctest cpack; do
    $SUDO_PREFIX ln -sf "/opt/cmake-${CMAKE_VERSION_PIN}-macos-universal/CMake.app/Contents/bin/${tool}" \
      "/usr/local/bin/${tool}"
  done
  hash -r
  cmake_ver="$(cmake --version | head -1 | awk '{print $3}')"
fi

# bqlog 是可选依赖：GitHub 的 Homebrew 可能没有该 formula，不应阻断 CI
if ! brew list --versions bqlog >/dev/null 2>&1; then
  if [ "$CHECK_ONLY" = "1" ]; then
    brew info --formula bqlog >/dev/null 2>&1 \
      || echo "warn: bqlog missing / local brew index stale (optional)"
  else
    # GitHub runner 的 Homebrew 索引可能较旧；官方安装方式就是 brew install bqlog
    if ! brew info --formula bqlog >/dev/null 2>&1; then
      echo "Homebrew index missing bqlog — running brew update"
      brew update --quiet || true
    fi
    brew install bqlog || echo "warn: bqlog unavailable on this Homebrew (optional)"
  fi
fi

check_min "$cmake_ver" "4.5.0" cmake
check_min "$just_ver" "1.20.0" just
check_min "$clang_ver" "23.1.0" "clang++"

echo "cmake ${cmake_ver}"
echo "just ${just_ver}"
echo "clang++ ${clang_ver} (/opt/homebrew/opt/llvm/bin/clang++)"
ninja --version | sed 's/^/ninja /'

# ---- 让 clang 工具链命令在 PATH 中可用（Homebrew llvm 是 keg-only）----
if [ "$CHECK_ONLY" != "1" ]; then
  for tool in clang-format clang-tidy clang-doc clang-scan-deps; do
    if [ -x "/opt/homebrew/opt/llvm/bin/${tool}" ]; then
      ln -sf "/opt/homebrew/opt/llvm/bin/${tool}" "/usr/local/bin/${tool}" 2>/dev/null \
        || sudo -n ln -sf "/opt/homebrew/opt/llvm/bin/${tool}" "/usr/local/bin/${tool}" 2>/dev/null \
        || echo "warn: cannot symlink ${tool} into /usr/local/bin"
    fi
  done
fi

echo "macOS $(sw_vers -productVersion) — deps ok"
