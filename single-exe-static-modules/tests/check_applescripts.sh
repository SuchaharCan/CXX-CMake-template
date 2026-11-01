#!/bin/bash
# 把 C++ 生成的 Reminders/OmniFocus AppleScript 分别编译为 `.scpt`。
# 本测试只验证语法与字典兼容性，不执行真实同步，也不请求自动化权限。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/build/Stacks_Tools"
RUNTIME="$ROOT/tests/.runtime/applescript"
LOGS="$ROOT/tests/logs"

mkdir -p "$RUNTIME" "$LOGS"
# 先让核心生成脚本文本；BqLog banner 从 stdout 中过滤掉。
"$BIN" reminder-script reminders | sed '/\[Bq\]/d' >"$RUNTIME/reminders.applescript"
"$BIN" reminder-script omnifocus | sed '/\[Bq\]/d' >"$RUNTIME/omnifocus.applescript"

# osacompile 的错误写入独立日志，便于区分 Reminders/OmniFocus 字典问题。
/usr/bin/osacompile -o "$RUNTIME/reminders.scpt" "$RUNTIME/reminders.applescript" \
  2>&1 | tee "$LOGS/applescript-reminders.log"
/usr/bin/osacompile -o "$RUNTIME/omnifocus.scpt" "$RUNTIME/omnifocus.applescript" \
  2>&1 | tee "$LOGS/applescript-omnifocus.log"
echo "AppleScript compile check passed"
