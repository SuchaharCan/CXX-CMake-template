#!/usr/bin/env bash
# =============================================================================
# ci/select-changed-templates.sh — 根据 Git 差异选择需要验证的模板
#
# 输出（stdout）：每行一个模板目录名，按 Git 差异中的出现顺序去重
# 诊断（stderr）：选中的模式、共享 CI 变更、模板范围
#
# 用法：
#   bash ci/select-changed-templates.sh --all
#   bash ci/select-changed-templates.sh --base origin/master --head HEAD
#
# 规则：
#   - 模板 = 仓库一级目录中直接包含 justfile 的目录
#   - 模板目录内任一变更 => 选中该模板
#   - CI 运行脚本、当前 workflow、action 变更 => 选中所有模板
#   - 删除的模板不运行；新增同时包含 justfile 的模板会自动选中
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
MODE="all"
BASE_REF=""
HEAD_REF="HEAD"

usage() {
  cat <<'EOF'
用法: select-changed-templates.sh [选项]

选项:
  --all                选择全部模板（默认）
  --base REF           比较 REF..HEAD，只选择受影响模板
  --head REF           比较的终点，默认 HEAD；需与 --base 一起使用
  --root PATH          指定 Git 仓库根目录（测试/脚本复用）
  -h, --help           显示帮助
EOF
}

fail() {
  echo "error: $*" >&2
  exit 2
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --all)
      MODE="all"
      BASE_REF=""
      shift
      ;;
    --base)
      [ "$#" -ge 2 ] || fail "--base 缺少参数"
      MODE="changed"
      BASE_REF="$2"
      shift 2
      ;;
    --head)
      [ "$#" -ge 2 ] || fail "--head 缺少参数"
      HEAD_REF="$2"
      shift 2
      ;;
    --root)
      [ "$#" -ge 2 ] || fail "--root 缺少参数"
      ROOT="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      fail "未知参数: $1"
      ;;
  esac
done

ROOT="$(cd "$ROOT" && pwd)"
git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1 \
  || fail "不是 Git 工作树: ${ROOT}"

list_all_templates() {
  local found=0
  local justfile
  local name

  for justfile in "$ROOT"/*/justfile; do
    [ -f "$justfile" ] || continue
    name="$(basename "$(dirname "$justfile")")"
    printf '%s\n' "$name"
    found=$((found + 1))
  done

  if [ "$found" -eq 0 ]; then
    fail "未发现直接包含 justfile 的模板目录"
  fi
}

if [ "$MODE" = "all" ]; then
  echo "[select] mode=all" >&2
  list_all_templates
  exit 0
fi

[ -n "$BASE_REF" ] || fail "增量模式必须提供 --base REF"

base_oid="$(git -C "$ROOT" rev-parse --verify "${BASE_REF}^{commit}" 2>/dev/null)" \
  || fail "基线引用不存在: ${BASE_REF}"
head_oid="$(git -C "$ROOT" rev-parse --verify "${HEAD_REF}^{commit}" 2>/dev/null)" \
  || fail "终点引用不存在: ${HEAD_REF}"

tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/cmake-template-selection.XXXXXX")"
trap 'rm -rf "$tmp_dir"' EXIT
changed_paths="${tmp_dir}/changed-paths"

git -C "$ROOT" diff --name-only -z --no-renames "$base_oid" "$head_oid" \
  > "$changed_paths"

if [ ! -s "$changed_paths" ]; then
  echo "[select] ${BASE_REF}..${HEAD_REF}: 无文件变更" >&2
  exit 0
fi

shared_change=""
while IFS= read -r -d '' path; do
  case "$path" in
    ci/*.sh|.github/workflows/template-matrix.yml|.github/actions/*)
      shared_change="$path"
      break
      ;;
  esac
done < "$changed_paths"

if [ -n "$shared_change" ]; then
  echo "[select] 共享 CI 变更触发全部模板: ${shared_change}" >&2
  list_all_templates
  exit 0
fi

selected=""
selected_count=0
while IFS= read -r -d '' path; do
  top_level="${path%%/*}"
  [ "$top_level" != "$path" ] || continue
  if ! git -C "$ROOT" cat-file -e "${head_oid}:${top_level}/justfile" 2>/dev/null; then
    if git -C "$ROOT" cat-file -e "${base_oid}:${top_level}/justfile" 2>/dev/null \
      && git -C "$ROOT" cat-file -e "${head_oid}:${top_level}" 2>/dev/null; then
      fail "模板目录仍存在但 justfile 已被移除: ${top_level}"
    fi
    continue
  fi

  case $'\n'"${selected}"$'\n' in
    *$'\n'"${top_level}"$'\n'*)
      ;;
    *)
      printf '%s\n' "$top_level"
      selected="${selected}${selected:+$'\n'}${top_level}"
      selected_count=$((selected_count + 1))
      ;;
  esac
done < "$changed_paths"

echo "[select] mode=changed range=${BASE_REF}..${HEAD_REF} templates=${selected_count}" >&2
