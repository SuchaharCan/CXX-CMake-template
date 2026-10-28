# =============================================================================
# 00_core_color.cmake — 配置期颜色库（调色板 + 消息 + 分隔符）
# 分层: core（必选）
# 独立: 是 — 颜色只依赖 cxx.palette，不依赖任何其他模块
# 上游: 无
# 下游: 所有 core/opt 模块与根 CMakeLists（cxx_msg / cxx_sep / cxx_banner）
# 功能:
#   - 解析 cmake/cxx.palette → CXX_COLOR_<名称>（终端 ANSI）/ CXX_GRC_<名称>（grc 名）
#   - cxx_msg(<语义角色> <文本>): 每种颜色都有固定含义（见 cxx.palette）
#   - cxx_sep([标题]): 加粗重线分隔符，可手动调用并支持中间注释
#   - cxx_banner(<标题>): 双线加粗横幅（终端无法改字号，用重线框 + 粗体等效强调）
#   - 尊重 NO_COLOR / CLICOLOR / CLICOLOR_FORCE
# =============================================================================

include_guard(GLOBAL)

# ---- 颜色开关 ----
set(_cxx_color_on TRUE)
if(DEFINED ENV{NO_COLOR} AND NOT "$ENV{NO_COLOR}" STREQUAL "")
  set(_cxx_color_on FALSE)
elseif(DEFINED ENV{CLICOLOR} AND "$ENV{CLICOLOR}" STREQUAL "0")
  set(_cxx_color_on FALSE)
endif()
if(DEFINED ENV{CLICOLOR_FORCE} AND NOT "$ENV{CLICOLOR_FORCE}" STREQUAL "0")
  set(_cxx_color_on TRUE)
endif()

set(CXX_COLOR_NAMES "" CACHE INTERNAL "palette color names" FORCE)

if(_cxx_color_on)
  string(ASCII 27 _cxx_esc)
else()
  set(_cxx_esc "")
endif()

# ---- 解析 cxx.palette（格式: 名称 | 语义 | ANSI | grc 配色名）----
set(_palette_file "${CMAKE_CURRENT_LIST_DIR}/cxx.palette")
if(EXISTS "${_palette_file}")
  file(STRINGS "${_palette_file}" _pal_lines ENCODING UTF-8)
  foreach(_line IN LISTS _pal_lines)
    if(NOT _line MATCHES "^[a-z_]+[ \t]*[|]")
      continue()
    endif()
    string(REGEX REPLACE "^[ \t]*([a-z_]+)[ \t]*[|].*" "\\1" _name "${_line}")
    string(REGEX REPLACE "^[^|]*[|][ \t]*([^|]*)[ \t]*[|].*" "\\1" _desc "${_line}")
    # ANSI 字段用逗号分隔（避免 file(STRINGS) 的 ';' 列表拆分），这里还原为分号
    string(REGEX REPLACE "^[^|]*[|][^|]*[|][ \t]*([0-9,]+)[ \t]*[|].*" "\\1" _ansi "${_line}")
    string(REPLACE "," ";" _ansi "${_ansi}")
    string(REGEX REPLACE "^[^|]*[|][^|]*[|][^|]*[|][ \t]*(.*)$" "\\1" _grc "${_line}")
    string(STRIP "${_name}" _name)
    string(STRIP "${_grc}" _grc)
    if(NOT _name STREQUAL "")
      set(CXX_COLOR_${_name} "${_cxx_esc}[${_ansi}m")
      set(CXX_GRC_${_name} "${_grc}")
      list(APPEND CXX_COLOR_NAMES "${_name}")
      if(NOT _desc MATCHES "^$")
        set(CXX_COLOR_DESC_${_name} "${_desc}")
      endif()
    endif()
  endforeach()
endif()
set(CXX_COLOR_NAMES "${CXX_COLOR_NAMES}" CACHE INTERNAL "palette color names" FORCE)
set(CXX_COLOR_RESET "${_cxx_esc}[0m")
unset(_pal_lines)
unset(_line)

# 兜底：调色板缺失时保证最小组（避免全裸输出）
foreach(_f IN ITEMS red orange yellow green grass blue purple cyan white gray)
  if(NOT DEFINED CXX_COLOR_${_f})
    set(CXX_COLOR_${_f} "")
  endif()
endforeach()

# -----------------------------------------------------------------------------
# _cxx_color_of(out_var role) — 语义角色 → 调色板色（带别名）
#   red|error|alert|failed, orange, yellow|warn|warning,
#   green|ok|success, grass|pass, blue|deps, purple|note|lint,
#   cyan|path|info, white|banner|detail, gray|pale
# -----------------------------------------------------------------------------
function(_cxx_color_of out_var role)
  if(role STREQUAL "error" OR role STREQUAL "alert" OR role STREQUAL "failed")
    set(_name red)
  elseif(role STREQUAL "warn" OR role STREQUAL "warning")
    set(_name yellow)
  elseif(role STREQUAL "ok" OR role STREQUAL "success")
    set(_name green)
  elseif(role STREQUAL "pass")
    set(_name grass)
  elseif(role STREQUAL "deps")
    set(_name blue)
  elseif(role STREQUAL "note" OR role STREQUAL "lint")
    set(_name purple)
  elseif(role STREQUAL "path" OR role STREQUAL "info")
    set(_name cyan)
  elseif(role STREQUAL "banner" OR role STREQUAL "detail")
    set(_name white)
  elseif(role STREQUAL "pale")
    set(_name gray)
  else()
    set(_name "${role}")
  endif()
  if(DEFINED CXX_COLOR_${_name})
    set(${out_var} "${CXX_COLOR_${_name}}" PARENT_SCOPE)
  else()
    set(${out_var} "" PARENT_SCOPE)
  endif()
endfunction()

# -----------------------------------------------------------------------------
# cxx_msg(role text) — 彩色 STATUS；可选镜像到 cmake_configure.log（无 ANSI）
# -----------------------------------------------------------------------------
function(cxx_msg role text)
  _cxx_color_of(_c "${role}")
  message(STATUS "${_c}${text}${CXX_COLOR_RESET}")

  # 与终端 STATUS 同步写入 cmake_configure.log（无颜色）
  if(CXX_MSG_MIRROR_TO_CONFIGURE_LOG AND COMMAND cxx_flog)
    if(text MATCHES "^━" OR text MATCHES "^----")
      cxx_flog_separator(cmake_configure)
    else()
      cxx_flog(cmake_configure INFO "${text}")
    endif()
  endif()
endfunction()

# -----------------------------------------------------------------------------
# _cxx_sep_line(out_var title width) — 生成一条加粗重线（两侧 ━ 填充）
# -----------------------------------------------------------------------------
function(_cxx_sep_line out_var title width)
  set(_line "")
  if(title STREQUAL "")
    string(REPEAT "━" ${width} _line)
  else()
    set(_t "  ${title}  ")
    string(LENGTH "${_t}" _len)
    if(_len GREATER_EQUAL ${width})
      set(_line "${_t}")
    else()
      math(EXPR _total "${width} - ${_len}")
      math(EXPR _left "${_total} / 2")
      math(EXPR _right "${_total} - ${_left}")
      string(REPEAT "━" ${_left} _l)
      string(REPEAT "━" ${_right} _r)
      set(_line "${_l}${_t}${_r}")
    endif()
  endif()
  set(${out_var} "${_line}" PARENT_SCOPE)
endfunction()

# -----------------------------------------------------------------------------
# cxx_sep([title]) — 手动分隔符；带标题时注释居中（加粗亮白）
#   用法: cxx_sep()                  # 纯分隔线
#         cxx_sep("=== 依赖检测 ===")  # 中间带注释
# -----------------------------------------------------------------------------
function(cxx_sep)
  set(_width 78)
  if(DEFINED CXX_SEP_WIDTH AND NOT CXX_SEP_WIDTH EQUAL 0)
    set(_width ${CXX_SEP_WIDTH})
  endif()
  set(_title "")
  if(ARGC GREATER 0)
    set(_title "${ARGV0}")
  endif()
  _cxx_sep_line(_line "${_title}" ${_width})
  cxx_msg(white "${_line}")
endfunction()

# -----------------------------------------------------------------------------
# cxx_banner(title) — 双线加粗横幅（大阶段标题）
# -----------------------------------------------------------------------------
function(cxx_banner title)
  set(_width 78)
  if(DEFINED CXX_SEP_WIDTH AND NOT CXX_SEP_WIDTH EQUAL 0)
    set(_width ${CXX_SEP_WIDTH})
  endif()
  string(LENGTH "${title}" _tlen)
  math(EXPR _inner "${_width} - 4")
  if(_tlen LESS_EQUAL ${_inner})
    math(EXPR _pad "${_inner} - ${_tlen}")
    math(EXPR _left "${_pad} / 2")
    math(EXPR _right "${_pad} - ${_left}")
    string(REPEAT " " ${_left} _l)
    string(REPEAT " " ${_right} _r)
    string(REPEAT "━" ${_width} _top)
    cxx_msg(white "┏${_top}┓")
    cxx_msg(white "┃${_l}${title}${_r}┃")
    cxx_msg(white "┗${_top}┛")
  else()
    cxx_sep("${title}")
  endif()
endfunction()

# -----------------------------------------------------------------------------
# cxx_var_group(role title where_hint) — 变量分组标题
# -----------------------------------------------------------------------------
function(cxx_var_group role title where_hint)
  cxx_msg(${role} "--- ${title} ---")
  cxx_msg(gray "  更新位置: ${where_hint}")
endfunction()

# -----------------------------------------------------------------------------
# cxx_var_line(role name) — 打印「名 = 值」
# -----------------------------------------------------------------------------
function(cxx_var_line role name)
  if(DEFINED ${name})
    cxx_msg(${role} "  ${name} = ${${name}}")
  else()
    cxx_msg(yellow "  ${name} = <undefined>")
  endif()
endfunction()

# -----------------------------------------------------------------------------
# cxx_configure_phase(title) — 配置阶段分隔 + 标题
# -----------------------------------------------------------------------------
function(cxx_configure_phase title)
  cxx_sep()
  cxx_msg(white "${title}")
  cxx_sep()
endfunction()

# 兼容旧调用名（cxx_msg_separator）— 等价于 cxx_sep()
function(cxx_msg_separator)
  cxx_sep()
endfunction()
