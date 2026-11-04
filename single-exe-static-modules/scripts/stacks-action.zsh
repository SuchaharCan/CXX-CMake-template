#!/bin/zsh
# Alfred Action 入口：记录 token -> 调用单二进制 -> 根据退出码播放成功/失败音效。
# 用户输入只作为单个 argv 传给 C++，脚本不拼接业务 JSON，也不执行用户文本。
set -u

workflow_dir="${0:A:h:h}"
binary="$workflow_dir/Stacks_Tools"
token="${1:-}"
trace_date="$(date +%Y%m%d)"
trace_log="$workflow_dir/log/trace-$trace_date.log"
debug_enabled="${STACKS_DEBUG:-0}"
logging_enabled="${STACKS_LOGGING-1}"
sound_enabled="${STACKS_SOUND:-0}"
log_active=0
[[ "$logging_enabled" == "1" || "$logging_enabled" == "true" ||
  "$logging_enabled" == "Checked" ]] && log_active=1

trace() {
  # trace 只写 STDERR 和插件 log；Alfred 大字号展示仍使用 stdout。
  (( log_active == 1 )) || return 0
  [[ "$debug_enabled" == "1" || "$debug_enabled" == "true" || "$debug_enabled" == "Checked" ]] ||
    return 0
  local line="[stacks-trace][$(date +%H:%M:%S)] $*"
  print -r -u2 -- "$line"
  mkdir -p "$workflow_dir/log" 2>/dev/null || true
  print -r -- "$line" >>"$trace_log" 2>/dev/null || true
}

play_feedback_sound() {
  # 声音异步播放，避免系统音频设备延迟阻塞 action 返回。
  [[ "$sound_enabled" == "1" || "$sound_enabled" == "true" ||
    "$sound_enabled" == "Checked" ]] || return 0
  local sound_file="$1"
  trace "sound=$sound_file"
  ( /usr/bin/afplay "$sound_file" >/dev/null 2>&1 & ) || true
}

trace "0.watermark=Omni-Stacks developer=https://suchaharcan.github.io/"
trace "1.action_token=<$token>"

# 说明/状态条目不应进入领域 action；避免 Alfred 把空参数显示成 unknown action。
if [[ -z "$token" ]]; then
  print -r -- "该条目仅用于说明，请直接继续输入或选择上方可执行条目"
  exit 0
fi

# 二进制缺失是可诊断的安装错误，返回失败音效并保留可读文本。
if [[ ! -x "$binary" ]]; then
  play_feedback_sound /System/Library/Sounds/Basso.aiff
  print -r -- "❌ Stacks_Tools 未安装，请重新安装 workflow"
  exit 0
fi

if (( log_active == 1 )); then
  mkdir -p "$workflow_dir/log" 2>/dev/null || true
fi

if [[ "$token" == "add-path" ]]; then
  # Alfred 原生目录选择器返回 POSIX path；随后 base64url 编码为安全 action token。
  trace "2.route=add-path(打开目录选择器)"
  selected="$(
    /usr/bin/osascript \
      -e 'POSIX path of (choose folder with prompt "选择 Stacks 2 工作区路径")' \
      2>/dev/null
  )" || {
    print -r -- "已取消路径刷新"
    exit 0
  }
  if [[ -z "$selected" ]]; then
    print -r -- "未选择路径"
    exit 0
  fi
  encoded="$(
    /usr/bin/printf '%s' "$selected" |
      /usr/bin/openssl base64 -A |
      /usr/bin/tr '+/' '-_' |
      /usr/bin/tr -d '='
  )"
  token="add-path:$encoded"
  trace "3.add_path_encoded=$token"
fi

trace "4.argv=<$binary> action <$token>"
stderr_log=/dev/null
# 开启日志时只捕获二进制 stderr；stdout 保持 Alfred 可消费文本。
if (( log_active == 1 )); then
  stderr_log="$workflow_dir/log/trace-$trace_date.binary-stderr"
fi
output="$(
  STACKS_WORKFLOW_DIR="$workflow_dir" "$binary" action "$token" 2>"$stderr_log"
)"
exit_code=$?
trace "5.binary_exit=$exit_code"
output="$(print -r -- "$output" | /usr/bin/sed '/\[Bq\]/d')"
trace "6.postprocess=<$output>"
# 成功/失败使用不同系统音效；脚本自身始终 exit 0，让 Alfred 展示反馈文本。
if (( exit_code != 0 )); then
  trace "7.final=error"
  play_feedback_sound /System/Library/Sounds/Basso.aiff
  print -r -- "❌ ${output:-操作失败}"
else
  trace "7.final=success"
  play_feedback_sound /System/Library/Sounds/Glass.aiff
  print -r -- "$output"
fi
exit 0
