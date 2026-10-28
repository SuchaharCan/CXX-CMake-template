#!/usr/bin/env bash
# =============================================================================
# ci/install-deps-macos.sh — GitHub macOS runner / 自建 macOS 27 依赖安装
# 用途：Homebrew 补齐 cmake≥4.4.4 / clang++≥23.1 / ninja / just / grc
#       / spdlog / fmt / googletest / google-benchmark / doxygen / ccache / bqlog
# 模式：默认缺失则 brew install；版本不足则 brew upgrade；
#       CI_DEPS_CHECK_ONLY=1 只检查不安装。
# 环境：macOS 26/27（arm64），已装 Homebrew + Xcode Command Line Tools。
# =============================================================================
set -euo pipefail

CHECK_ONLY="${CI_DEPS_CHECK_ONLY:-0}"

if ! command -v brew >/dev/null 2>&1; then
  echo "error: Homebrew not found (https://brew.sh)" >&2
  exit 1
fi
if ! xcode-select -p >/dev/null 2>&1; then
  echo "error: Xcode Command Line Tools not installed (xcode-select --install)" >&2
  exit 1
fi

export HOMEBREW_NO_AUTO_UPDATE=1

FORMULAE="cmake ninja just grc ccache spdlog fmt googletest google-benchmark doxygen llvm"

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
check_min "$cmake_ver" "4.4.4" cmake || upgrade+=(cmake)
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

check_min "$cmake_ver" "4.4.4" cmake
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
