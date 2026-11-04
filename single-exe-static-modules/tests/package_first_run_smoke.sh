#!/bin/bash
# 验证无 data/log 的发布包首次运行会自动创建运行时目录和空索引。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCHIVE="${1:-$ROOT/dist/Omni-Stacks.alfredworkflow}"
SANDBOX="$(mktemp -d "$ROOT/tests/.runtime/package-first-run.XXXXXX")"

# 解包后必须先确认 release 不含用户 data/log，再运行首次初始化。
unzip -q "$ARCHIVE" -d "$SANDBOX"
test ! -e "$SANDBOX/data"
test ! -e "$SANDBOX/log"
test -L "$SANDBOX/icon.png"
test "$(readlink "$SANDBOX/icon.png")" = "icons/icon.png"
test ! -e "$SANDBOX/update.json"

"$SANDBOX/scripts/stacks-filter.zsh" "" |
  jq -e '.items | length > 0' >/dev/null

# 首次运行应自动创建三个空索引和 log 目录。
test -f "$SANDBOX/data/Project.json"
test -f "$SANDBOX/data/Stack.json"
test -f "$SANDBOX/data/Timers.json"
test -d "$SANDBOX/log"
jq -e '.library_paths == [] and .nodes == []' "$SANDBOX/data/Project.json" >/dev/null
jq -e '.nodes == []' "$SANDBOX/data/Stack.json" >/dev/null
jq -e '.timers == {}' "$SANDBOX/data/Timers.json" >/dev/null

log_file="$(find "$SANDBOX/log" -maxdepth 1 -name 'interface.log_*' -print -quit)"
# 日志开启时必须包含 UI 与看板查询两条关键接口记录。
test -n "$log_file"
grep -q 'WorkflowUI::filter' "$log_file"
grep -q 'TaskManager::list_boards' "$log_file"

OFF_SANDBOX="$(mktemp -d "$ROOT/tests/.runtime/package-logging-off.XXXXXX")"
unzip -q "$ARCHIVE" -d "$OFF_SANDBOX"
# 以空 STACKS_LOGGING 运行，验证“自动建 log 目录但不写日志”。
STACKS_LOGGING='' "$OFF_SANDBOX/scripts/stacks-filter.zsh" "" |
  jq -e '.items | length > 0' >/dev/null
test -d "$OFF_SANDBOX/data"
test -d "$OFF_SANDBOX/log"
if find "$OFF_SANDBOX/log" -maxdepth 1 -type f -print -quit | grep -q .; then
  echo "logging-disabled workflow unexpectedly wrote log files" >&2
  exit 1
fi

echo "package first-run smoke passed: data/log auto-created, logging on/off verified"
