#!/bin/bash
# 生成不含源码、用户 data/ 和运行时 log/ 的 Alfred 安装包。
# 安装目录先做布局校验，再压缩为单一 `.alfredworkflow` 文件。
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_binary="$project_root/build/Stacks_Tools"
dist="$project_root/dist"
stage="$dist/Omni-Stacks"
archive="$dist/Omni-Stacks.alfredworkflow"

# 打包只接受已成功构建的根级单二进制。
if [[ ! -x "$build_binary" ]]; then
  echo "missing build/Stacks_Tools; run: just build" >&2
  exit 1
fi

mkdir -p "$dist"
rm -rf "$stage"
mkdir -p "$stage/icons" "$stage/scripts"

# 只复制发布运行契约需要的文件；data/log 由二进制首次启动自动创建。
cp "$build_binary" "$stage/Stacks_Tools"
cp "$project_root/alfred/info.plist" "$stage/info.plist"
cp "$project_root/alfred/prefs.plist" "$stage/prefs.plist"
cp "$project_root/icons/"*.png "$stage/icons/"
cp "$project_root/scripts/stacks-filter.zsh" "$stage/scripts/stacks-filter.zsh"
cp "$project_root/scripts/stacks-action.zsh" "$stage/scripts/stacks-action.zsh"
chmod 755 "$stage/Stacks_Tools" \
  "$stage/scripts/stacks-filter.zsh" "$stage/scripts/stacks-action.zsh"

# package 模式会额外拒绝 data/log，防止把开发机用户数据带入 Release。
bash "$project_root/tests/check_workflow_layout.sh" "$stage" package
rm -f "$archive"
(
  cd "$stage"
  # 使用 ditto-compatible 布局压缩，保留图标/脚本权限和资源叉。
  /usr/bin/zip -qry "$archive" info.plist prefs.plist icons scripts Stacks_Tools
)
echo "workflow package: $archive"
du -h "$archive"
