#!/bin/bash
# 将真实 Stacks 库复制到 tests/.runtime 后执行完整 CRUD；不会修改源库。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE_BIN="$ROOT/build/Stacks_Tools"
SANDBOX="$ROOT/tests/.runtime/real-data"
RUNTIME="$SANDBOX/runtime"
LIBRARIES="$RUNTIME/libraries"
BIN="$RUNTIME/Stacks_Tools"

# 只允许在复制后的 runtime 上执行写入；源库路径只用于 ditto 导入。
if [[ ! -x "$SOURCE_BIN" ]]; then
  echo "missing build/Stacks_Tools" >&2
  exit 1
fi
if [[ "$#" -eq 0 ]]; then
  echo "usage: tests/real_data_smoke.sh <library-dir> [library-dir ...]" >&2
  exit 2
fi

rm -rf "$SANDBOX"
mkdir -p "$RUNTIME/data" "$LIBRARIES"

# 运行时二进制和空索引进入隔离目录，不触碰源工作区。
cp "$SOURCE_BIN" "$BIN"
cp "$ROOT/data/Project.json" "$RUNTIME/data/Project.json"
cp "$ROOT/data/Stack.json" "$RUNTIME/data/Stack.json"
cp "$ROOT/data/Timers.json" "$RUNTIME/data/Timers.json"

for source_library in "$@"; do
  # 每个真实工作区先复制到 tests/.runtime，再调用 sync 建立索引。
  if [[ ! -d "$source_library" ]]; then
    echo "missing source library: $source_library" >&2
    exit 1
  fi
  name="$(basename "$source_library")"
  destination="$LIBRARIES/$name"
  mkdir -p "$destination"
  ditto "$source_library" "$destination"
  "$BIN" sync "$destination" | sed '/\[Bq\]/d'
done

echo "== boards"
"$BIN" boards | sed '/\[Bq\]/d'
echo "== imported task count"
"$BIN" tasks | sed '/\[Bq\]/d' | wc -l
echo "== current week"
"$BIN" week 2026-11-01 | sed '/\[Bq\]/d'

target="$("$BIN" boards | sed '/\[Bq\]/d' | awk -F '\t' '$2 == "project" { print $1; exit }')"
if [[ -z "$target" ]]; then
  echo "no project target found" >&2
  exit 1
fi

created="$(
  "$BIN" add "$target" "Codex integration task|created from cloned Stacks data" \
    --start 2026-10-30 --due 2026-11-01 --priority high
)"
# CLI 输出以 created: 行为准；失败输出不会进入 task_id 解析。
created="$(printf '%s\n' "$created" | sed '/\[Bq\]/d' | grep '^created:')"
task_id="$(printf '%s\n' "$created" | sed -E 's/.*id=([^ ]+).*/\1/')"
if [[ -z "$task_id" ]]; then
  echo "failed to parse created task id: $created" >&2
  exit 1
fi
task_file="$(find "$LIBRARIES" -type f -name "$task_id.task" -print -quit)"
echo "== crud task=$task_id file=$task_file"

"$BIN" edit "$task_id" --title "Codex integration edited" \
  --description "edit verified" --start 2026-10-30 --due 2026-11-01 --priority high |
  sed '/\[Bq\]/d'
"$BIN" get "$task_id" | sed '/\[Bq\]/d'
jq -e '.priority == "high"' "$task_file" >/dev/null
"$BIN" "done" "$task_id" | sed '/\[Bq\]/d'
jq -e '.done == true and .progress == 100 and (.completed | length > 0)' "$task_file" >/dev/null
"$BIN" reopen "$task_id" | sed '/\[Bq\]/d'
# 重开后必须删除 completed 字段，而不仅是把 done 设为 false。
jq -e '.done == false and .progress == 0 and (has("completed") | not)' "$task_file" >/dev/null

# 计时至少产生 1 秒记录，并立即从 Timers.json 清理运行态。
"$BIN" timer-start "$task_id" | sed '/\[Bq\]/d'
"$BIN" timers | sed '/\[Bq\]/d'
"$BIN" timer-stop "$task_id" | sed '/\[Bq\]/d'
jq -e '.timelogs | length == 1 and .[0].duration >= 1' "$task_file" >/dev/null

"$BIN" ui "task:$task_id" | sed '/\[Bq\]/d' | jq -e '.items | length > 5' >/dev/null
"$BIN" sync-reminder "$task_id" both --dry-run | sed '/\[Bq\]/d'

inbox="$("$BIN" inbox "Codex inbox integration|inbox payload")"
inbox="$(printf '%s\n' "$inbox" | sed '/\[Bq\]/d' | grep '^created:')"
inbox_id="$(printf '%s\n' "$inbox" | sed -E 's/.*id=([^ ]+).*/\1/')"
if [[ -z "$inbox_id" ]]; then
  echo "failed to parse Inbox task id: $inbox" >&2
  exit 1
fi
inbox_file="$(find "$LIBRARIES" -type f -name "$inbox_id.task" -print -quit)"
jq -e '.project == "inbox"' "$inbox_file" >/dev/null
"$BIN" delete "$inbox_id" | sed '/\[Bq\]/d'

# 最终删除原任务，确认源文件消失且 `.trash` 中留有可恢复副本。
"$BIN" delete "$task_id" | sed '/\[Bq\]/d'
if [[ -f "$task_file" ]]; then
  echo "original task file still exists after delete" >&2
  exit 1
fi
if ! find "$LIBRARIES" -path '*/tasks/.trash/*' -name "*.task" -print -quit | grep -q .; then
  echo "trash file not created" >&2
  exit 1
fi

echo "real-data smoke passed: create/read/update/done/reopen/timer/inbox/delete"
