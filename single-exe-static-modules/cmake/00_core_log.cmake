# =============================================================================
# 00_core_log.cmake — log/ 目录文件日志（纯文本，无 ANSI）
# 分层: core（必选）
# 独立: 是 — 只依赖核心 CMake 与 src/ScenarioLog.hxx 的 CXX_LOG_MAX_LINES
# 上游: 无
# 下游: 00_core_color.cmake（cxx_msg 镜像写 configure 日志）
#       00_core_buildlog.cmake（CXX_LOG_DIR / cmake_build.log）
#       00_core_deps.cmake / 90_opt_gtest.cmake（cxx_flog）
# 功能:
#   - 维护 CXX_LOG_DIR / CXX_LOG_MAX_LINES（与 ScenarioLog.hxx 同步）
#   - cxx_flog(scenario level text) 按场景写 log/*.log
#   - cxx_trim_log_file 裁剪日志到 CXX_LOG_MAX_LINES 行
# =============================================================================

include_guard(GLOBAL)

get_filename_component(_CXX_TEMPLATE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# 与 src/ScenarioLog.hxx 中 CXX_LOG_MAX_LINES 同步（仅值变化时写入 CACHE）
set(_cxx_log_max_from_hdr "")
if(EXISTS "${_CXX_TEMPLATE_ROOT}/src/ScenarioLog.hxx")
  file(STRINGS "${_CXX_TEMPLATE_ROOT}/src/ScenarioLog.hxx" _cxx_log_hdr_lines
       REGEX "^#define[ \t]+CXX_LOG_MAX_LINES[ \t]+")
  foreach(_cxx_log_line IN LISTS _cxx_log_hdr_lines)
    if(_cxx_log_line MATCHES "^#define[ \t]+CXX_LOG_MAX_LINES[ \t]+([0-9]+)")
      set(_cxx_log_max_from_hdr "${CMAKE_MATCH_1}")
      break()
    endif()
  endforeach()
endif()
if(_cxx_log_max_from_hdr STREQUAL "")
  set(_cxx_log_max_from_hdr "1000")
endif()
if(NOT DEFINED CXX_LOG_DIR OR NOT CXX_LOG_DIR STREQUAL "${_CXX_TEMPLATE_ROOT}/log")
  set(CXX_LOG_DIR "${_CXX_TEMPLATE_ROOT}/log" CACHE PATH "Scenario log directory" FORCE)
endif()
if(NOT DEFINED CXX_LOG_MAX_LINES OR NOT CXX_LOG_MAX_LINES STREQUAL "${_cxx_log_max_from_hdr}")
  set(CXX_LOG_MAX_LINES "${_cxx_log_max_from_hdr}" CACHE STRING
      "Max lines per log file (synced from src/ScenarioLog.hxx)" FORCE)
endif()
unset(_cxx_log_max_from_hdr)
unset(_cxx_log_hdr_lines)
unset(_cxx_log_line)
file(MAKE_DIRECTORY "${CXX_LOG_DIR}")

# cxx_msg → 终端（彩色）+ cmake_configure.log（无 ANSI，与终端文案一致）
set(CXX_MSG_MIRROR_TO_CONFIGURE_LOG ON)

# -----------------------------------------------------------------------------
# cxx_trim_log_file(logfile) — 裁剪日志为末尾 CXX_LOG_MAX_LINES 行
# -----------------------------------------------------------------------------
function(cxx_trim_log_file logfile)
  if(NOT EXISTS "${logfile}")
    return()
  endif()
  # 必须 UTF-8，否则裁剪会把中文弄成换行/丢失
  file(STRINGS "${logfile}" _lines ENCODING UTF-8)
  list(LENGTH _lines _n)
  if(_n LESS_EQUAL CXX_LOG_MAX_LINES)
    return()
  endif()
  math(EXPR _start "${_n} - ${CXX_LOG_MAX_LINES}")
  set(_keep "")
  set(_i 0)
  foreach(_line IN LISTS _lines)
    if(_i GREATER_EQUAL _start)
      list(APPEND _keep "${_line}")
    endif()
    math(EXPR _i "${_i} + 1")
  endforeach()
  file(WRITE "${logfile}" "")
  foreach(_line IN LISTS _keep)
    file(APPEND "${logfile}" "${_line}\n")
  endforeach()
endfunction()

# -----------------------------------------------------------------------------
# cxx_flog(scenario level text) — 写各类日志文件（无 ANSI）
# -----------------------------------------------------------------------------
function(cxx_flog scenario level text)
  string(TIMESTAMP _ts "%Y-%m-%d %H:%M:%S")
  if(scenario STREQUAL "cmake_configure")
    set(_tag "CONFIGURE")
    set(_file "${CXX_LOG_DIR}/cmake_configure.log")
  elseif(scenario STREQUAL "cmake_build")
    set(_tag "BUILD")
    set(_file "${CXX_LOG_DIR}/cmake_build.log")
  elseif(scenario STREQUAL "exe")
    set(_tag "EXE")
    set(_file "${CXX_LOG_DIR}/exe.log")
  elseif(scenario STREQUAL "gtest")
    set(_tag "GTEST")
    set(_file "${CXX_LOG_DIR}/gtest.log")
  elseif(scenario STREQUAL "lldb_debug")
    set(_tag "LLDB")
    set(_file "${CXX_LOG_DIR}/lldb_debug.log")
  else()
    message(FATAL_ERROR "cxx_flog: unknown scenario '${scenario}'")
  endif()
  string(TOUPPER "${level}" _lv)
  file(APPEND "${_file}" "[${_ts}] [${_tag}] [${_lv}] ${text}\n")
  cxx_trim_log_file("${_file}")
endfunction()

# -----------------------------------------------------------------------------
# cxx_flog_separator(scenario) — 向对应日志追加分隔线
# -----------------------------------------------------------------------------
function(cxx_flog_separator scenario)
  if(scenario STREQUAL "cmake_configure")
    set(_file "${CXX_LOG_DIR}/cmake_configure.log")
  elseif(scenario STREQUAL "cmake_build")
    set(_file "${CXX_LOG_DIR}/cmake_build.log")
  elseif(scenario STREQUAL "exe")
    set(_file "${CXX_LOG_DIR}/exe.log")
  elseif(scenario STREQUAL "gtest")
    set(_file "${CXX_LOG_DIR}/gtest.log")
  elseif(scenario STREQUAL "lldb_debug")
    set(_file "${CXX_LOG_DIR}/lldb_debug.log")
  else()
    message(FATAL_ERROR "cxx_flog_separator: unknown scenario '${scenario}'")
  endif()
  file(APPEND "${_file}" "----- ----- ----- ----- ----- ----- ----- -----\n")
  cxx_trim_log_file("${_file}")
endfunction()
