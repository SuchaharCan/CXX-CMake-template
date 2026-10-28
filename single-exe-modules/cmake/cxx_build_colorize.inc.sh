#!/usr/bin/env bash
# =============================================================================
# cxx_build_colorize.inc.sh — single-exe-classic（头文件 / Make|Ninja）
# -----------------------------------------------------------------------------
# 作用：由 build/cxx_build_tee `source`，对 cmake --build 终端输出做：
#   1) 长命令（>160 字符且 ≥4 个 -flag）按参数换行（工具路径亮青、参数暗灰）
#   2) 关键字着色 —— 引擎为 grcat（brew install grc），规则由
#      00_core_buildlog.cmake 从 cmake/cxx.palette 生成 build/cxx_grc.conf
# 日志（tee 左侧）保持纯文本单行原文；仅 stdout 为 TTY 时着色。
# 非 TTY 强制着色：CLICOLOR_FORCE=1 或 CXX_BUILD_COLOR_FORCE=1（日志回放/CI）。
#
# 环境: CXX_GRCAT（grcat 路径）/ CXX_GRC_CONF（conf 路径），由 tee 脚本注入。
# =============================================================================

# _cxx_colorize — 过滤刷屏 → 长命令换行 → grcat 着色（grcat 缺失则保持纯文本）
_cxx_colorize() {
  if [[ ! -t 1 && -z ${CLICOLOR_FORCE:-} && -z ${CXX_BUILD_COLOR_FORCE:-} ]]; then
    cat
    return
  fi
  export _R=$'\033[0m'
  export _TOOL=$'\033[1;96m'      # 长命令首行工具路径 亮青
  export _ARG=$'\033[90m'         # 长命令参数行 暗灰

  grep -v -E \
    -e '^-- ----- ----- ----- ----- ----- ----- ----- -----' \
    -e '^----- ----- ----- ----- ----- ----- ----- -----' \
    -e '\[BUILD\].*PRE_BUILD' \
    -e '\[BUILD\].*POST_BUILD' \
    -e '\[BUILD\].*耗时对比' \
    -e '^-- log PRE_BUILD' \
    -e '^-- log POST_BUILD' \
    -e '=== ccache 统计' \
  | perl -e '
      # 单命令多参数 / cd 命令链 → 换行格式化 + 绝对路径缩短（仅终端显示；日志保持原文）
      my $TOOL=$ENV{_TOOL}; my $ARG=$ENV{_ARG}; my $R=$ENV{_R};
      my $src = $ENV{CXX_SRC_DIR} // "";
      my $bin = $ENV{CXX_BIN_DIR} // "";
      while (<STDIN>) {
        chomp;
        # ninja 在强制着色/非 TTY 下会给每行加 [n/m] 或 [ n%] 进度前缀 → 先摘下
        my $prog = "";
        $prog = $1 if s{^(\[[0-9]+/[0-9]+\]|\[[ ]*[0-9]+%\])\s+}{};
        my $nd = () = /(?:^|\s)-/g;
        my $is_cmd = (length($_) > 160 && ( /^["\x27]?\// || /^cd\s/ ) && $nd >= 4);
        if (!$is_cmd) { print $prog.$_."\n"; next; }

        # Change Dir 前缀与绝对路径 → 相对引用（cd build / build / .）
        my $prefix = "";
        if ($bin ne "" && s{^cd\s+\Q$bin\E\s*&&\s*}{}) { $prefix = "cd build && "; }
        if ($bin ne "") { s{\Q$bin\E}{build}g; }
        if ($src ne "") { s{\Q$src\E}{.}g; }

        # 以 && 切分命令链，逐段换行
        my @segs = split(/\s*&&\s*/, $_);
        my $first = 1;
        for my $seg (@segs) {
          my @t = ($seg =~ /("(?:[^"\\]|\\.)*"|\S+)/g);
          my @ln; my $buf = ""; my $dd = 0;
          for my $tk (@t) {
            my $cls = $tk; $cls =~ s/^"//;
            if ($cls =~ /^-/ || $dd) {
              push @ln, $buf if length $buf;
              $buf = "    $tk";
              $dd = ($tk eq "--") ? 1 : 0;
            } elsif ($buf eq "") {
              $buf = $tk; $dd = 0;
            } else {
              $buf .= " $tk"; $dd = 0;
            }
          }
          push @ln, $buf if length $buf;
          for my $i (0..$#ln) {
            if ($i == 0) {
              my $head = $first ? $prefix : "&& ";
              $first = 0;
              print $prog.$TOOL.$head.$ln[$i].$R."\n";
              $prog = "";
            } else {
              print $ARG.$ln[$i].$R."\n";
            }
          }
        }
      }
    ' \
  | {
      if [[ -n ${CXX_GRCAT:-} && -x ${CXX_GRCAT} && -n ${CXX_GRC_CONF:-} && -f ${CXX_GRC_CONF} ]]; then
        "$CXX_GRCAT" "$CXX_GRC_CONF"
      else
        cat
      fi
    }
}
