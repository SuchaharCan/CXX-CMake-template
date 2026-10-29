#!/usr/bin/env bash
# =============================================================================
# tests/run_mutation_tests.sh — 两个真实变异体 + 对应接口测试
# 1) kTaskIdLength 20 → 19（GenerateId 测试必须失败）
# 2) backlog 插入头部 → 尾部（CreateTask 顺序测试必须失败）
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d /tmp/stacks-tools-mutation.XXXXXX)"
trap 'if [ "${CI_MUTATION_KEEP:-0}" != "1" ]; then rm -rf "$TMP"; fi' EXIT

copy_tree() {
  local target="$1"
  mkdir -p "$target"
  cp -R "$ROOT/CMakeLists.txt" "$ROOT/cmake" "$ROOT/modules" \
        "$ROOT/src" "$ROOT/tests" "$ROOT/data" "$ROOT/json.hpp" "$target/"
}

run_killing_test() {
  local dir="$1" filter="$2" label="$3"
  cmake -S "$dir" -B "$dir/build-mutation" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCXX_ENABLE_GTEST=ON \
    -DCXX_ENABLE_CCACHE=OFF >/dev/null
  local jobs
  jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
  cmake --build "$dir/build-mutation" --target static_modules_tests -j"$jobs" >/dev/null
  if "$dir/build-mutation/static_modules_tests" "--gtest_filter=${filter}" >/dev/null 2>&1; then
    echo "[SURVIVED] ${label} — 测试未杀死变异体" >&2
    return 1
  fi
  echo "[KILLED]   ${label}"
}

MUTANT1="$TMP/mutant-19"
copy_tree "$MUTANT1"
perl -0pi -e 's/kTaskIdLength = 20;/kTaskIdLength = 19;/' \
  "$MUTANT1/modules/task_manager/include/TaskManager.cppm"
run_killing_test "$MUTANT1" "Boundary.GenerateIdHasFixedLengthAndAlphabet" "kTaskIdLength 20 -> 19"

MUTANT2="$TMP/mutant-backlog"
copy_tree "$MUTANT2"
perl -0pi -e 's/project_json\["backlog"\]\.insert\(project_json\["backlog"\]\.begin\(\), task_id\.value\)/project_json["backlog"].insert(project_json["backlog"].end(), task_id.value)/g' \
  "$MUTANT2/modules/task_manager/include/TaskManager.cppm"
run_killing_test "$MUTANT2" "Boundary.CreateTaskForProjectGoesToBacklogHead" "backlog insert begin -> end"

echo "mutation tests passed: 2/2 killed"
