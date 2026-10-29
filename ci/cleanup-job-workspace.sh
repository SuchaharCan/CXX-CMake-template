#!/usr/bin/env bash
# =============================================================================
# ci/cleanup-job-workspace.sh — 在 Actions Job 完成前清理工作树生成物
#
# 仅清理当前 GITHUB_WORKSPACE 内被 Git 忽略/未跟踪的文件：
#   build/、log/、.ci-logs/、.cache/、.deps-libcxx/、job 日志等。
# 保留源码与 .git，供 actions/checkout 的 post 步骤正常收尾。
# 全局 Homebrew 工具链和跨任务 ccache 缓存不在本脚本清理范围内。
# =============================================================================
set -euo pipefail

WORKSPACE="${GITHUB_WORKSPACE:-$(pwd)}"
WORKSPACE="$(cd "$WORKSPACE" && pwd -P)"
REPO_ROOT="$(git -C "$WORKSPACE" rev-parse --show-toplevel 2>/dev/null)" \
  || {
    echo "warn: ${WORKSPACE} 不是 Git 工作树，跳过 Job 清理" >&2
    exit 0
  }
REPO_ROOT="$(cd "$REPO_ROOT" && pwd -P)"

if [ "$WORKSPACE" != "$REPO_ROOT" ]; then
  echo "error: Git 根目录 ${REPO_ROOT} 与 GITHUB_WORKSPACE ${WORKSPACE} 不一致" >&2
  exit 2
fi

echo "[cleanup] 清理 Job 工作树生成物: ${WORKSPACE}"
git -C "$WORKSPACE" clean -ffdx
echo "[cleanup] 完成；源码与 ccache 跨任务缓存保留"
