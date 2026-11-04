#!/bin/bash
# 在空白 workflow 中验证旧数据迁移、备份、根目录安装和 Alfred 可执行性。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SANDBOX="$ROOT/tests/.runtime/install-layout"
WORKFLOW="$SANDBOX/workflow"

# 构建一个包含“旧嵌套数据 + 根数据”的模拟 workflow，验证迁移优先级。
rm -rf "$SANDBOX"
mkdir -p "$WORKFLOW/Stacks_Tools/data" "$WORKFLOW/data" "$WORKFLOW/log" "$WORKFLOW/scripts"
cp "$ROOT/alfred/info.plist" "$WORKFLOW/info.plist"
cp "$ROOT/alfred/prefs.plist" "$WORKFLOW/prefs.plist"
cp "$ROOT/icons/icon.png" "$WORKFLOW/icon.png"

# 旧嵌套 Project 使用 `/nested/priority`，根目录使用 `/root/fallback`。
# 正确安装后应保留旧嵌套值，证明迁移顺序确定且不会误用根目录数据。
jq '.library_paths = ["/nested/priority"]' "$ROOT/data/Project.json" \
  >"$WORKFLOW/Stacks_Tools/data/Project.json"
cp "$ROOT/data/Stack.json" "$WORKFLOW/Stacks_Tools/data/Stack.json"
cp "$ROOT/data/Timers.json" "$WORKFLOW/Stacks_Tools/data/Timers.json"
printf '{"handles":{"task":{"✅nested":"nested-task"}}}\n' \
  >"$WORKFLOW/Stacks_Tools/data/UiState.json"
jq '.library_paths = ["/root/fallback"]' "$ROOT/data/Project.json" \
  >"$WORKFLOW/data/Project.json"
cp "$ROOT/data/Stack.json" "$WORKFLOW/data/Stack.json"
cp "$ROOT/data/Timers.json" "$WORKFLOW/data/Timers.json"
printf '{"handles":{"task":{"✅root":"root-task"}}}\n' \
  >"$WORKFLOW/data/UiState.json"
printf 'preserved\n' >"$WORKFLOW/log/preserved.log"
printf 'legacy\n' >"$WORKFLOW/scripts/legacy.txt"
printf '{"version":2.1}\n' >"$WORKFLOW/update.json"

bash "$ROOT/scripts/install-workflow.sh" "$WORKFLOW"
bash "$ROOT/tests/check_workflow_layout.sh" "$WORKFLOW"

# 配置契约：八个别名、短键哨兵、完成/归档开关、Logging、两个反馈节点。
plutil -extract userconfigurationconfig json -o - "$WORKFLOW/info.plist" |
  jq -e '([.[] | select(.variable | startswith("STACKS_ALIAS_"))] | length) == 8' >/dev/null
plutil -extract userconfigurationconfig json -o - "$WORKFLOW/info.plist" |
  jq -e 'any(.variable == "STACKS_LOGGING" and .config.default == true)' >/dev/null
plutil -extract userconfigurationconfig json -o - "$WORKFLOW/info.plist" |
  jq -e 'any(.variable == "STACKS_HANDLE_SENTINEL" and .type == "textfield" and .config.default == "✅ ")' >/dev/null
plutil -extract userconfigurationconfig json -o - "$WORKFLOW/info.plist" |
  jq -e 'any(.variable == "STACKS_INCLUDE_DONE_ARCHIVED" and .type == "checkbox" and .config.default == false)' >/dev/null
test ! -e "$WORKFLOW/update.json"
test "$(plutil -extract name raw -o - "$WORKFLOW/info.plist")" = "Omni-Stacks"
plutil -extract objects json -o - "$WORKFLOW/info.plist" |
  jq -e 'map(select(.type == "alfred.workflow.utility.conditional")) | length == 2' >/dev/null
cmp "$ROOT/build/Stacks_Tools" "$WORKFLOW/Stacks_Tools"
jq -e '.library_paths == ["/nested/priority"]' "$WORKFLOW/data/Project.json" >/dev/null
jq -e '.handles.task["✅nested"] == "nested-task"' "$WORKFLOW/data/UiState.json" >/dev/null
test -f "$WORKFLOW/log/preserved.log"
test -L "$WORKFLOW/icon.png"
test "$(readlink "$WORKFLOW/icon.png")" = "icons/icon.png"
test -f "$WORKFLOW/icons/icon.png"

backup="$(find "$WORKFLOW/.backups" -mindepth 1 -maxdepth 1 -type d -print -quit)"
# 正式替换前，旧根目录和旧嵌套目录都应完整进入 backup。
test -n "$backup"
test -f "$backup/Stacks_Tools/data/Project.json"
test -f "$backup/data/Project.json"
test -f "$backup/log/preserved.log"
test -f "$backup/update.json"
test ! -e "$WORKFLOW/Stacks_Tools/Stacks_Tools"

"$WORKFLOW/scripts/stacks-filter.zsh" "" |
  sed '/\[Bq\]/d' |
  jq -e '.items | length > 0' >/dev/null
"$WORKFLOW/scripts/stacks-filter.zsh" "cf" |
  sed '/\[Bq\]/d' |
  jq -e '.items[] | select(.title == "路径索引移除说明 / Path index removal")' >/dev/null
"$WORKFLOW/scripts/stacks-action.zsh" "" |
  grep -q "该条目仅用于说明"

echo "install layout smoke passed: migration/backup/root executable"
