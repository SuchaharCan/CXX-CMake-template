#!/usr/bin/env bash
# =============================================================================
# ci/run-local-ci.sh — 在本机复现 GitHub Actions 矩阵
# 用法：bash ci/run-local-ci.sh
# 行为：
#   - macOS：检查 Homebrew 依赖（不安装），运行所有模板的完整矩阵
#   - Linux：检查命令与版本；若存在 .deps-libcxx 则用于 modules
#   - CI_FRESH_BUILD=1 / CCACHE_DISABLE=1，与 workflow 保持一致
# 可通过 CI_JUST_RECIPES 覆盖命令集合
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OS_NAME="$(uname -s)"

if [ "$OS_NAME" = "Darwin" ]; then
  CI_DEPS_CHECK_ONLY=1 bash "${ROOT}/ci/install-deps-macos.sh"
  export CLANG_FORMAT="/opt/homebrew/opt/llvm/bin/clang-format"
elif [ "$OS_NAME" = "Linux" ]; then
  for tool in cmake ninja just clang++ g++; do
    command -v "$tool" >/dev/null 2>&1 || {
      echo "error: missing ${tool}（先运行 ci/install-deps-ubuntu.sh）" >&2
      exit 1
    }
  done
  export CLANG_FORMAT="${CLANG_FORMAT:-/usr/local/bin/clang-format}"
  if [ -d "${ROOT}/.deps-libcxx" ]; then
    export CXX_LIBCXX_DEPS_PREFIX="${ROOT}/.deps-libcxx"
  elif [ "${CI_LOCAL_BUILD_DEPS:-0}" = "1" ]; then
    bash "${ROOT}/ci/install-libcxx-deps.sh"
    export CXX_LIBCXX_DEPS_PREFIX="${ROOT}/.deps-libcxx"
  else
    echo "warn: .deps-libcxx 不存在；modules 可能需要 CI_LOCAL_BUILD_DEPS=1"
  fi
else
  echo "error: unsupported OS ${OS_NAME}" >&2
  exit 1
fi

export CI_FRESH_BUILD=1
export CCACHE_DISABLE=1
export CI_LOG_DIR="${CI_LOG_DIR:-${ROOT}/.ci-logs/local}"
mkdir -p "$CI_LOG_DIR"

echo "local CI: ${OS_NAME} — recipes: ${CI_JUST_RECIPES:-default}"
bash "${ROOT}/ci/run-template-matrix.sh"
