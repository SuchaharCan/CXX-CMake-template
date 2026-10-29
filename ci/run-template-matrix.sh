#!/usr/bin/env bash
# =============================================================================
# ci/run-template-matrix.sh — 模板矩阵执行器（GitHub Actions / 本地通用）
# 作用：对当前选中的模板逐个执行 just 命令；
#       任一步失败会继续跑完并在结尾汇总，最终以非零退出码失败。
# 规则：
#   - 未设置 CI_TEMPLATES 时自动发现所有“直接包含 justfile”的模板
#   - 设置 CI_TEMPLATES（空格分隔）时只运行指定模板，适合增量 CI
#   - 默认执行 env list build check test parallel-test docs bench fuzz
#     coverage iwyu mutation-test verbose
#   - clean 不执行（会删除构建目录，由人工触发）
#   - CI_FRESH_BUILD=1 时先清空该模板 build/（CI 默认开启）
#   - 可用 CI_JUST_RECIPES="build test" 覆盖命令集合
#   - CI_REQUIRED_RECIPES 默认要求 build，防止未定义编译命令时误报通过
# =============================================================================
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RECIPES="${CI_JUST_RECIPES:-env list build check test parallel-test docs bench fuzz coverage iwyu mutation-test verbose}"
REQUIRED_RECIPES="${CI_REQUIRED_RECIPES-build}"
LOGDIR="${CI_LOG_DIR:-${ROOT}/.ci-logs}"
mkdir -p "$LOGDIR"

if [ -z "${RECIPES//[[:space:]]/}" ]; then
  echo "error: CI_JUST_RECIPES 不能为空" >&2
  exit 2
fi

requested_templates=()
if [ -n "${CI_TEMPLATES:-}" ]; then
  for template_name in ${CI_TEMPLATES}; do
    requested_templates+=("$template_name")
  done
fi

template_dirs=()
if [ "${#requested_templates[@]}" -gt 0 ]; then
  for template_name in "${requested_templates[@]}"; do
    case "$template_name" in
      ""|.|..|*/*)
        echo "error: 非法模板名: ${template_name}" >&2
        exit 2
        ;;
    esac
    if [ ! -f "${ROOT}/${template_name}/justfile" ]; then
      echo "error: 模板不存在或缺少 justfile: ${template_name}" >&2
      exit 2
    fi
    template_dirs+=("${ROOT}/${template_name}")
  done
else
  for justfile in "$ROOT"/*/justfile; do
    [ -f "$justfile" ] || continue
    template_dirs+=("$(dirname "$justfile")")
  done
fi

found=0
failed=()

for template_dir in "${template_dirs[@]}"; do
  template_name="$(basename "$template_dir")"
  found=$((found + 1))

  echo
  echo "############################################################"
  echo "# 模板: ${template_name}"
  echo "############################################################"

  if [ "${CI_FRESH_BUILD:-0}" = "1" ] && [ -d "$template_dir/build" ]; then
    echo "[ci] 清理 ${template_name}/build（CI_FRESH_BUILD=1）"
    find "$template_dir/build" -mindepth 1 -depth -delete
  fi

  summary="$(cd "$template_dir" && just --summary 2>/dev/null || true)"
  if [ -z "$summary" ]; then
    failed+=("${template_name}||")
    continue
  fi

  summary_words=" $(printf '%s\n' "$summary" | tr '\n' ' ') "
  for required_recipe in $REQUIRED_RECIPES; do
    case "$summary_words" in
      *" ${required_recipe} "*)
        ;;
      *)
        echo "[FAIL] ${template_name}: 缺少必需 recipe '${required_recipe}'"
        failed+=("${template_name}|required:${required_recipe}|")
        ;;
    esac
  done

  for recipe in $RECIPES; do
    case "$summary_words" in
      *" ${recipe} "*)
        ;;
      *)
        echo "[skip] ${template_name}: just ${recipe}（未定义）"
        continue
        ;;
    esac
    echo
    echo "──────── just ${recipe} @ ${template_name} ────────"
    log="${LOGDIR}/${template_name}-${recipe}.log"
    if (cd "$template_dir" && just "$recipe") 2>&1 | tee "$log"; then
      echo "[ok]   ${template_name}: just ${recipe}"
    else
      echo "[FAIL] ${template_name}: just ${recipe}"
      failed+=("${template_name}|${recipe}|${log}")
    fi
  done
done

echo
echo "============================================================"
if [ "$found" -eq 0 ]; then
  echo "未选择任何含 justfile 的模板目录"
  exit 1
fi
echo "选中模板数量: ${found}"
if [ "${#failed[@]}" -eq 0 ]; then
  echo "结果: 全部通过（${RECIPES}）"
  exit 0
fi
echo "结果: 失败项 ${#failed[@]}"
for f in "${failed[@]}"; do
  IFS='|' read -r name recipe log <<< "$f"
  echo
  if [ -z "${recipe:-}" ]; then
    echo "  ✗ ${name}: justfile 解析失败"
    continue
  fi
  case "$recipe" in
    required:*)
      echo "  ✗ ${name}: 缺少必需 recipe '${recipe#required:}'"
      ;;
    *)
      echo "  ✗ ${name}: just ${recipe}"
      echo "    命令: cd ${ROOT}/${name} && just ${recipe}"
      ;;
  esac
  if [ -n "${log:-}" ] && [ -f "$log" ]; then
    echo "    错误输出（末尾 30 行）:"
    tail -n 30 "$log" | sed 's/^/      /'
  fi
done
exit 1
