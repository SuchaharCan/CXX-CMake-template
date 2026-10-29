#!/usr/bin/env bash
# =============================================================================
# ci/run-template-matrix.sh — 模板矩阵执行器（GitHub Actions / 本地通用）
# 作用：自动发现仓库内所有含 justfile 的模板目录，逐个执行 just 命令；
#       任一步失败会继续跑完并在结尾汇总，最终以非零退出码失败。
# 规则：
#   - 只处理“目录直接包含 justfile”的模板（新增模板自动纳入）
#   - 默认执行 env list build check test docs bench fuzz coverage iwyu verbose
#   - clean 不执行（会删除构建目录，由人工触发）
#   - CI_FRESH_BUILD=1 时先清空该模板 build/（CI 默认开启）
#   - 可用 CI_JUST_RECIPES="build test" 覆盖命令集合
# =============================================================================
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RECIPES="${CI_JUST_RECIPES:-env list build check test parallel-test docs bench fuzz coverage iwyu mutation-test verbose}"
LOGDIR="${CI_LOG_DIR:-${ROOT}/.ci-logs}"
mkdir -p "$LOGDIR"

found=0
failed=()

for justfile in "$ROOT"/*/justfile; do
  [ -f "$justfile" ] || continue
  template_dir="$(dirname "$justfile")"
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

  for recipe in $RECIPES; do
    if ! printf '%s\n' "$summary" | tr ' ' '\n' | grep -qx "$recipe"; then
      echo "[skip] ${template_name}: just ${recipe}（未定义）"
      continue
    fi
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
  echo "未发现含 justfile 的模板目录"
  exit 1
fi
echo "模板数量: ${found}"
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
  echo "  ✗ ${name}: just ${recipe}"
  echo "    命令: cd ${ROOT}/${name} && just ${recipe}"
  if [ -n "${log:-}" ] && [ -f "$log" ]; then
    echo "    错误输出（末尾 30 行）:"
    tail -n 30 "$log" | sed 's/^/      /'
  fi
done
exit 1
