#!/bin/bash
# 校验 Alfred workflow 的可发布运行布局；拒绝旧版嵌套二进制目录。
set -euo pipefail

if [[ "$#" -lt 1 || "$#" -gt 2 ]]; then
  echo "usage: tests/check_workflow_layout.sh <workflow-dir> [runtime|package]" >&2
  exit 2
fi

workflow_dir="$1"
layout_mode="${2:-runtime}"
if [[ ! -d "$workflow_dir" ]]; then
  echo "workflow directory does not exist: $workflow_dir" >&2
  exit 1
fi

# 发布/运行共同必需的文件：根二进制、plist、图标和两个 Alfred 脚本。
for required_file in \
  info.plist \
  prefs.plist \
  Stacks_Tools \
  icons/icon.png \
  icons/project.png \
  icons/board.png \
  icons/task.png \
  icons/note.png \
  icons/inbox.png \
  scripts/stacks-filter.zsh \
  scripts/stacks-action.zsh; do
  if [[ ! -f "$workflow_dir/$required_file" ]]; then
    echo "missing workflow file: $required_file" >&2
    exit 1
  fi
done

for required_dir in icons scripts; do
  if [[ ! -d "$workflow_dir/$required_dir" ]]; then
    echo "missing workflow directory: $required_dir" >&2
    exit 1
  fi
done

# runtime 安装必须包含三个运行 JSON 与 log；package 模式则必须排除它们。
if [[ "$layout_mode" == "runtime" ]]; then
  for required_file in data/Project.json data/Stack.json data/Timers.json; do
    if [[ ! -f "$workflow_dir/$required_file" ]]; then
      echo "missing runtime data file: $required_file" >&2
      exit 1
    fi
  done
  for required_dir in data log; do
    if [[ ! -d "$workflow_dir/$required_dir" ]]; then
      echo "missing runtime directory: $required_dir" >&2
      exit 1
    fi
  done
elif [[ "$layout_mode" == "package" ]]; then
  if [[ -e "$workflow_dir/data" || -e "$workflow_dir/log" ]]; then
    echo "release package must not contain data/ or log/" >&2
    exit 1
  fi
else
  echo "unknown layout mode: $layout_mode" >&2
  exit 2
fi

# 二进制必须在根目录且可执行；旧嵌套目录会让 Alfred action 找不到入口。
if [[ ! -x "$workflow_dir/Stacks_Tools" ]]; then
  echo "Stacks_Tools must be an executable file at the workflow root" >&2
  exit 1
fi
if [[ -e "$workflow_dir/Stacks_Tools/Stacks_Tools" ]]; then
  echo "legacy nested binary layout is not allowed" >&2
  exit 1
fi
# 品牌图标只允许 icons/icon.png，避免根文件与 Alfred 图标缓存冲突。
if [[ -e "$workflow_dir/icon.png" ]]; then
  echo "icon.png must live at icons/icon.png" >&2
  exit 1
fi

echo "workflow layout ok: $workflow_dir"
