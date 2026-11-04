#!/bin/zsh
# Alfred Script Filter 入口：只做命令别名预处理和 JSON 后处理，业务参数由
# `Stacks_Tools ui "<query>"` 校验。trace 每一步都带编号，便于 Alfred Debugger 对齐。
set -u

workflow_dir="${0:A:h:h}"
binary="$workflow_dir/Stacks_Tools"
trace_date="$(date +%Y%m%d)"
trace_log="$workflow_dir/log/trace-$trace_date.log"
debug_enabled="${STACKS_DEBUG:-0}"
logging_enabled="${STACKS_LOGGING-1}"
log_active=0
[[ "$logging_enabled" == "1" || "$logging_enabled" == "true" ||
  "$logging_enabled" == "Checked" ]] && log_active=1

# 管道追踪：每步编号，STDERR 进 Alfred Debugger，同时追加到 log/trace.log。
trace() {
  (( log_active == 1 )) || return 0
  [[ "$debug_enabled" == "1" || "$debug_enabled" == "true" || "$debug_enabled" == "Checked" ]] ||
    return 0
  local line="[stacks-trace][$(date +%H:%M:%S)] $*"
  print -r -u2 -- "$line"
  mkdir -p "$workflow_dir/log" 2>/dev/null || true
  print -r -- "$line" >>"$trace_log" 2>/dev/null || true
}

trace "0.watermark=Omni-Stacks developer=https://suchaharcan.github.io/"
raw_query="${1:-}"
trace "1.alfred_input=<$raw_query>"

# 1) Alfred 输入
query="${raw_query##[[:space:]]#}"
query="${query%%[[:space:]]#}"
trace "2.trimmed_input=<$query>"

# 2) 分隔符原样交给 C++：核心统一处理自定义字符、全角/半角与转义。
delimiter="${STACKS_DELIMITER:-${Stacks__delimiter:-|}}"
trace "3.delimiter_passthrough=<$delimiter>"

# 3) 命令判定（Alfred 侧路由，简称 -> 核心命令）
first_word="${query%%[[:space:]]*}"
# 别名来自 Workflow Configuration；大小写保持用户配置，不自动改写。
alias_add_path="${STACKS_ALIAS_ADD_PATH:-ap}"
alias_reload="${STACKS_ALIAS_RELOAD:-rl}"
alias_config="${STACKS_ALIAS_CONFIG:-cf}"
alias_view="${STACKS_ALIAS_VIEW:-v}"
alias_tasks="${STACKS_ALIAS_TASKS:-t}"
alias_week="${STACKS_ALIAS_WEEK:-w}"
alias_timers="${STACKS_ALIAS_TIMERS:-tm}"
alias_inbox="${STACKS_ALIAS_INBOX:-i}"
trace "3.aliases=add:$alias_add_path reload:$alias_reload config:$alias_config view:$alias_view tasks:$alias_tasks week:$alias_week timers:$alias_timers inbox:$alias_inbox"
case "$first_word" in
  "$alias_reload"|rl|cl|reload)
    trace "4.route=reload-all(重载全部路径)"
    canonical="reload-all"
    ;;
  "$alias_add_path"|ap|add-path)
    trace "4.route=add-path(添加新路径)"
    canonical="add-path"
    ;;
  "$alias_config"|cf|config)
    trace "4.route=config(路径配置)"
    canonical="config"
    ;;
  a|add)
    trace "4.route=add(录入任务)"
    canonical="add"
    ;;
  b|boards)
    trace "4.route=boards(看板列表)"
    canonical="add"
    ;;
  "$alias_inbox"|i|inbox)
    trace "4.route=inbox(收件箱录入)"
    canonical="inbox"
    ;;
  "$alias_view"|v|view|board)
    trace "4.route=view(查看看板任务)"
    canonical="board"
    ;;
  "$alias_tasks"|t|tasks)
    trace "4.route=tasks(搜索任务)"
    canonical="tasks"
    ;;
  "$alias_week"|w|week)
    trace "4.route=week(本周截止)"
    canonical="week"
    ;;
  "$alias_timers"|tm|timers)
    trace "4.route=timers(运行计时)"
    canonical="timers"
    ;;
  *)
    # 无命令前缀时保留原文，由 C++ 渲染“选择目标/创建预览”或默认筛选。
    trace "4.route=default(录入/筛选)"
    canonical=""
    ;;
esac

if [[ -n "$canonical" ]]; then
  # 只替换首个命令词，剩余内容原样交给核心，避免 shell 自行拆标题/描述。
  remainder="${query#"$first_word"}"
  remainder="${remainder##[[:space:]]#}"
  if [[ -n "$remainder" ]]; then
    final_query="$canonical $remainder"
  else
    final_query="$canonical"
  fi
else
  final_query="$query"
fi
trace "5.canonical_query=<$final_query>"

if [[ ! -x "$binary" ]]; then
  # 返回合法 Alfred JSON（而非空输出），让用户看到可操作安装错误。
  trace "6.binary_missing"
  print -r -- '{"skipknowledge":true,"items":[{"title":"Stacks_Tools 未安装","subtitle":"请运行 just install 或重新安装 workflow","valid":false}]}'
  exit 0
fi

stderr_log=/dev/null
if (( log_active == 1 )); then
  mkdir -p "$workflow_dir/log" 2>/dev/null || true
  stderr_log="$workflow_dir/log/trace-$trace_date.binary-stderr"
fi

# 4) 拼装并调用二进制
trace "6.argv=<$binary> ui <$final_query>"
binary_stdout="$(
  STACKS_WORKFLOW_DIR="$workflow_dir" "$binary" ui "$final_query" 2>"$stderr_log"
)"
binary_exit=$?
trace "7.binary_exit=$binary_exit"
trace "8.binary_stderr_head=<$(head -c 300 "$stderr_log" 2>/dev/null | tr '\n' ' ')>"

# 5) 插件对二进制返回结果的后处理
output="$(print -r -- "$binary_stdout" | /usr/bin/sed '/\[Bq\]/d')"
item_count="$(print -r -- "$output" | jq -r '.items | length' 2>/dev/null || print -r -- 'n/a')"
trace "9.postprocess_items=$item_count bytes=$(print -r -- "$output" | wc -c | tr -d ' ')"

if (( binary_exit != 0 )); then
  # 业务异常统一转换为不可选错误项，防止 Alfred 缓存上次成功结果。
  trace "10.final=error_json"
  print -r -- '{"skipknowledge":true,"items":[{"title":"读取 Stacks 失败","subtitle":"打开 Alfred 调试器查看 [stacks-trace]，或运行 just verify-install","valid":false}]}'
  exit 0
fi
if [[ -z "$output" ]]; then
  # 空 stdout 同样转为合法 JSON，避免 Script Filter 直接崩溃。
  trace "10.final=empty_json"
  print -r -- '{"skipknowledge":true,"items":[{"title":"没有可用结果","subtitle":"请尝试 s reload 刷新工作区，或修改搜索关键词","valid":false}]}'
  exit 0
fi

trace "10.final=pass_through"
print -r -- "$output"
