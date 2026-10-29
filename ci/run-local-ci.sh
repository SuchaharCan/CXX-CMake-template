#!/usr/bin/env bash
# =============================================================================
# ci/run-local-ci.sh — 在本机复现 GitHub Actions 矩阵
# 用法：
#   bash ci/run-local-ci.sh                    # 全量矩阵（兼容原行为）
#   bash ci/run-local-ci.sh --all              # 明确运行全量矩阵
#   bash ci/run-local-ci.sh --changed [BASE]   # 只构建 BASE..HEAD 影响的模板
# 行为：
#   - macOS：检查 Homebrew 依赖（不安装）
#   - Linux：检查命令与版本；若存在 .deps-libcxx 则用于 modules
#   - CI_FRESH_BUILD=1，并启用 ccache，与 workflow 保持一致
#   - --changed 默认只执行 build；可用 CI_CHANGED_RECIPES 覆盖
#   - 全量模式可通过 CI_JUST_RECIPES 覆盖命令集合
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OS_NAME="$(uname -s)"
MODE="full"
BASE_REF="HEAD^"

usage() {
  cat <<'EOF'
用法: run-local-ci.sh [--all | --changed [BASE]]

  --all             运行全部模板及默认完整 recipe 集合
  --changed BASE    只构建 BASE..HEAD 内变更模板；BASE 默认 HEAD^
EOF
}

case "${1:-}" in
  "")
    ;;
  --all)
    MODE="full"
    shift
    ;;
  --changed)
    MODE="changed"
    shift
    if [ "$#" -gt 0 ]; then
      BASE_REF="$1"
      shift
    fi
    ;;
  -h|--help)
    usage
    exit 0
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac

if [ "$#" -gt 0 ]; then
  usage >&2
  exit 2
fi

if [ "$MODE" = "changed" ]; then
  selected_templates="$(
    bash "${ROOT}/ci/select-changed-templates.sh" \
      --root "$ROOT" --base "$BASE_REF" --head HEAD
  )"
  one_line_templates="$(
    printf '%s\n' "$selected_templates" \
      | awk 'NF { printf "%s%s", separator, $0; separator = " " }'
  )"
  if [ -z "$one_line_templates" ]; then
    echo "local CI: 无模板变更，跳过依赖检查和模板矩阵"
    exit 0
  fi
  export CI_TEMPLATES="$one_line_templates"
  export CI_JUST_RECIPES="${CI_CHANGED_RECIPES:-build}"
fi

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
unset CCACHE_DISABLE
export CI_LOG_DIR="${CI_LOG_DIR:-${ROOT}/.ci-logs/local}"
mkdir -p "$CI_LOG_DIR"

if [ "$MODE" = "changed" ]; then
  echo "local CI: ${OS_NAME} — changed=${CI_TEMPLATES} recipes=${CI_JUST_RECIPES}"
else
  echo "local CI: ${OS_NAME} — all templates recipes: ${CI_JUST_RECIPES:-default}"
fi

bash "${ROOT}/ci/run-template-matrix.sh"
