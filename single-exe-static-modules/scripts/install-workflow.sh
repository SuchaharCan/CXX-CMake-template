#!/bin/bash
# 将构建结果安装到已有 Alfred workflow。安装流程先写临时 stage，完成布局/plist 校验
# 后再替换正式目录；旧 data/log 优先迁移，刷新失败不会直接覆盖用户索引。
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
workflow_dir="${1:-}"
build_binary="$project_root/build/Stacks_Tools"

# 先验证目标和构建产物，任何缺失都在创建 backup/stage 前失败。
if [[ -z "$workflow_dir" || ! -f "$workflow_dir/info.plist" ]]; then
  echo "invalid Alfred workflow directory: $workflow_dir" >&2
  exit 1
fi
if [[ ! -x "$build_binary" ]]; then
  echo "missing build/Stacks_Tools; run: just build" >&2
  exit 1
fi

stamp="$(date +%Y%m%d-%H%M%S)"
stage="$(mktemp -d "$workflow_dir/.stacks-tools-install.XXXXXX")"
backup_root="$workflow_dir/.backups"
backup_dir="$backup_root/$stamp"
cleanup() {
  # stage 只保存本次未提交文件；异常退出时清理，不碰正式数据。
  rm -rf "$stage"
}
trap cleanup EXIT

# 所有产物先进入 stage，最后才替换 workflow 根目录。
mkdir -p "$stage/data" "$stage/icons" "$stage/log" "$stage/scripts" "$backup_dir"

copy_data_file() {
  local filename="$1"
  local nested="$workflow_dir/Stacks_Tools/data/$filename"
  local current="$workflow_dir/data/$filename"
  local fallback="$project_root/data/$filename"

  # 迁移优先级：旧嵌套 -> 根目录 -> 源码空模板，保证升级不丢路径。
  if [[ -f "$nested" ]]; then
    cp -p "$nested" "$stage/data/$filename"
  elif [[ -f "$current" ]]; then
    cp -p "$current" "$stage/data/$filename"
  elif [[ -f "$fallback" ]]; then
    cp -p "$fallback" "$stage/data/$filename"
  else
    echo "missing data template: $filename" >&2
    exit 1
  fi
}

copy_data_file Project.json
copy_data_file Stack.json
copy_data_file Timers.json

# UiState 是本机短句柄/草稿状态；存在时随用户数据一起保留，缺失则由首次 UI 调用创建。
if [[ -f "$workflow_dir/Stacks_Tools/data/UiState.json" ]]; then
  cp -p "$workflow_dir/Stacks_Tools/data/UiState.json" "$stage/data/UiState.json"
elif [[ -f "$workflow_dir/data/UiState.json" ]]; then
  cp -p "$workflow_dir/data/UiState.json" "$stage/data/UiState.json"
fi

# Alfred 元数据、图标、脚本和二进制统一使用插件根目录布局。
cp "$build_binary" "$stage/Stacks_Tools"
cp "$project_root/alfred/info.plist" "$stage/info.plist"
cp "$project_root/icons/"*.png "$stage/icons/"
ln -s icons/icon.png "$stage/icon.png"
cp "$project_root/scripts/stacks-filter.zsh" "$stage/scripts/stacks-filter.zsh"
cp "$project_root/scripts/stacks-action.zsh" "$stage/scripts/stacks-action.zsh"
if [[ -f "$workflow_dir/prefs.plist" ]]; then
  cp -p "$workflow_dir/prefs.plist" "$stage/prefs.plist"
else
  cp "$project_root/alfred/prefs.plist" "$stage/prefs.plist"
fi
if [[ -d "$workflow_dir/log" ]]; then
  # 日志是运行证据；安装时保留，不把历史 trace 清空。
  ditto "$workflow_dir/log" "$stage/log"
elif [[ -d "$workflow_dir/Stacks_Tools/log" ]]; then
  ditto "$workflow_dir/Stacks_Tools/log" "$stage/log"
fi
printf '' >"$stage/log/.keep"
chmod 755 "$stage/Stacks_Tools" "$stage/scripts/stacks-filter.zsh" \
  "$stage/scripts/stacks-action.zsh"

/usr/bin/plutil -lint "$stage/info.plist" "$stage/prefs.plist" >/dev/null

# 在替换正式目录前验证完整布局，失败时正式 workflow 保持原样。
bash "$project_root/tests/check_workflow_layout.sh" "$stage"

# 备份当前正式文件；备份失败由 set -e 中止，不进入替换阶段。
for item in info.plist icon.png update.json prefs.plist Stacks_Tools icons data log scripts; do
  source_path="$workflow_dir/$item"
  if [[ -e "$source_path" || -L "$source_path" ]]; then
    ditto "$source_path" "$backup_dir/$item"
  fi
done

# 旧安装可能仍带有 update.json；先备份，再停止随插件分发更新元数据。
if [[ -e "$workflow_dir/update.json" || -L "$workflow_dir/update.json" ]]; then
  rm -f -- "$workflow_dir/update.json"
fi

# 正式替换：逐个移动 stage 项。update.json 只备份，不再进入新安装。
for item in info.plist icon.png prefs.plist Stacks_Tools icons data log scripts; do
  target_path="$workflow_dir/$item"
  if [[ -e "$target_path" || -L "$target_path" ]]; then
    rm -rf -- "$target_path"
  fi
  mv "$stage/$item" "$target_path"
done

echo "installed workflow: $workflow_dir"
echo "runtime layout: $workflow_dir/{Stacks_Tools,data,log,scripts}"
echo "backup: $backup_dir"
