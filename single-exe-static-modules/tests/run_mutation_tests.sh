#!/usr/bin/env bash
# 源码内隔离变异测试。每个 mutant 均留下构建/测试日志。
#
# 流程：复制最小源码树到 tests/.runtime/mutation -> 修改单个代码不变式 ->
# 编译指定测试 -> 测试必须失败（mutant killed）-> 恢复原始源码。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNTIME="$ROOT/tests/.runtime/mutation"
WORK="$RUNTIME/worktree"
LOG_DIR="$ROOT/tests/logs"
TASK_MODULE="$WORK/modules/task_manager/include/TaskManager.cppm"
ORIGINAL="$RUNTIME/TaskManager.original.cppm"

mkdir -p "$RUNTIME" "$LOG_DIR"
rm -rf "$WORK"
# 隔离副本不需要构建产物；每个 mutant 都从同一份干净的源码开始。
mkdir -p "$WORK/tests/.runtime" "$WORK/tests/logs"
cp -R "$ROOT/CMakeLists.txt" "$ROOT/cmake" "$ROOT/modules" "$ROOT/src" \
  "$ROOT/data" "$ROOT/json.hpp" "$WORK/"
cp "$ROOT/tests/static_modules_tests.cxx" \
  "$ROOT/tests/run_mutation_tests.sh" "$WORK/tests/"
cp "$TASK_MODULE" "$ORIGINAL"

restore_source() {
  # 无论正常结束或中途失败都恢复 TaskManager，防止变异文本留在源码仓库。
  cp "$ORIGINAL" "$TASK_MODULE"
}
trap restore_source EXIT

run_mutant() {
  local name="$1" filter="$2" description="$3"
  local log="$LOG_DIR/mutation-${name}.log"
  echo "== mutant: $name — $description ==" | tee "$log"
  # 重新 configure/build 指定 GTest target；构建失败视为测试基础设施错误。
  if ! cmake -S "$WORK" -B "$WORK/build" -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCXX_ENABLE_GTEST=ON \
      -DCXX_ENABLE_CCACHE=ON >>"$log" 2>&1; then
    echo "BUILD FAILED: $name (see $log)" >&2
    return 1
  fi
  if ! cmake --build "$WORK/build" --target static_modules_tests \
      -j"${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}" >>"$log" 2>&1; then
    echo "BUILD FAILED: $name (see $log)" >&2
    return 1
  fi
  if "$WORK/build/static_modules_tests" "--gtest_filter=$filter" >>"$log" 2>&1; then
    # 测试仍通过说明 mutant survived，变异测试整体失败。
    echo "[SURVIVED] $name" | tee -a "$log" >&2
    return 1
  fi
  echo "[KILLED] $name" | tee -a "$log"
}

restore_source
# 1) 固定 ID 长度：19 应被 GenerateId 边界测试杀死。
perl -0pi -e 's/kTaskIdLength = 20;/kTaskIdLength = 19;/' "$TASK_MODULE"
run_mutant "id-length" "Boundary.GenerateIdFixedLengthAndAlphabet" "task ID 20 -> 19"
restore_source

# 2) 完成进度：99 应被完成字段测试杀死。
perl -0pi -e 's/task\["progress"\] = 100;/task["progress"] = 99;/' "$TASK_MODULE"
run_mutant "completion-progress" \
  "Boundary.DoneWritesStacksCompletionFieldsAndReopenClearsThem" "progress 100 -> 99"
restore_source

# 3) 本周边界：周日回退到周六应被周边界测试杀死。
perl -0pi -e 's/monday \+ std::chrono::days\{6\}/monday + std::chrono::days{5}/' "$TASK_MODULE"
run_mutant "week-sunday" "Boundary.NormalizeDateAndWeekBounds" "week end Sunday -> Saturday"
restore_source

# 4) Inbox 项目键：backlog 应被 Inbox 语义测试杀死。
perl -0pi -e 's/\{"project", "inbox"\}/{"project", "backlog"}/' "$TASK_MODULE"
run_mutant "inbox-project" "Boundary.CreateInboxUsesInboxProject" "inbox -> backlog"
restore_source

echo "mutation tests passed: 4/4 killed" | tee "$LOG_DIR/mutation-summary.log"
