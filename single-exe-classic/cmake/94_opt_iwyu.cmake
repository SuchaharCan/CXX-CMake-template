# =============================================================================
# 94_opt_iwyu.cmake — 可选 Include-What-You-Use（头文件模板主战场）
# 分层: opt（可选，手动开启）
# 独立: 是
# 下游: 10_core_setup.cmake（自注册 apply 门禁 + finish 钩子）
# 启用: cmake .. -DCXX_ENABLE_IWYU=ON
#       cmake --build . --target iwyu-check（独立扫描）
# =============================================================================

include_guard(GLOBAL)

option(CXX_ENABLE_IWYU
       "Attach include-what-you-use via CXX_INCLUDE_WHAT_YOU_USE on targets" OFF)
option(CXX_IWYU_AS_CMAKE_VAR
       "Also set CMAKE_CXX_INCLUDE_WHAT_YOU_USE (all CXX targets)" OFF)
option(CXX_IWYU_ERROR "Pass -Xiwyu --error (treat suggestions as errors)" OFF)

set(CXX_IWYU_PROGRAM "" CACHE FILEPATH "include-what-you-use binary")

# -----------------------------------------------------------------------------
# cxx_find_iwyu() — 定位 include-what-you-use，写入 CXX_IWYU_PROGRAM
# -----------------------------------------------------------------------------
function(cxx_find_iwyu)
  if(CXX_IWYU_PROGRAM AND EXISTS "${CXX_IWYU_PROGRAM}")
    return()
  endif()
  find_program(_iwyu NAMES include-what-you-use iwyu)
  if(_iwyu)
    set(CXX_IWYU_PROGRAM "${_iwyu}" CACHE FILEPATH "include-what-you-use binary" FORCE)
  endif()
endfunction()

# -----------------------------------------------------------------------------
# cxx_iwyu_command_line(out_var) — 组装 IWYU 命令行
# -----------------------------------------------------------------------------
function(cxx_iwyu_command_line out_var)
  cxx_find_iwyu()
  if(NOT CXX_IWYU_PROGRAM)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  set(_cmd "${CXX_IWYU_PROGRAM}")
  set(_map "${CMAKE_SOURCE_DIR}/cmake/iwyu.imp")
  if(EXISTS "${_map}")
    list(APPEND _cmd "-Xiwyu" "--mapping_file=${_map}")
  endif()
  if(CXX_IWYU_ERROR)
    list(APPEND _cmd "-Xiwyu" "--error")
  endif()
  # Keep transitive stdlib quieter for teaching templates
  list(APPEND _cmd "-Xiwyu" "--no_fwd_decls")
  set(${out_var} "${_cmd}" PARENT_SCOPE)
endfunction()

# -----------------------------------------------------------------------------
# cxx_apply_iwyu(target) — CXX_ENABLE_IWYU=ON 时设置 CXX_INCLUDE_WHAT_YOU_USE
# -----------------------------------------------------------------------------
function(cxx_apply_iwyu target)
  if(NOT CXX_ENABLE_IWYU)
    return()
  endif()
  if(NOT TARGET ${target})
    message(FATAL_ERROR "cxx_apply_iwyu: missing ${target}")
  endif()
  cxx_iwyu_command_line(_iwyu_cmd)
  if(NOT _iwyu_cmd)
    cxx_msg(yellow "IWYU ON but binary missing (brew install include-what-you-use)")
    return()
  endif()
  set_property(TARGET ${target} PROPERTY CXX_INCLUDE_WHAT_YOU_USE "${_iwyu_cmd}")
  cxx_msg(gray "IWYU → ${target}")
endfunction()

# -----------------------------------------------------------------------------
# cxx_register_iwyu_targets() — 全局开关 + iwyu-check 目标
# -----------------------------------------------------------------------------
function(cxx_register_iwyu_targets)
  if(NOT CXX_ENABLE_IWYU AND NOT CXX_IWYU_AS_CMAKE_VAR)
    return()
  endif()
  cxx_find_iwyu()
  if(NOT CXX_IWYU_PROGRAM)
    return()
  endif()

  if(CXX_IWYU_AS_CMAKE_VAR)
    cxx_iwyu_command_line(_global)
    if(_global)
      set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE "${_global}" CACHE STRING
          "Global IWYU launcher" FORCE)
      cxx_msg(grass "CMAKE_CXX_INCLUDE_WHAT_YOU_USE set")
    endif()
  endif()

  if(NOT (DEFINED CXX_LINT_SOURCES AND CXX_LINT_SOURCES))
    return()
  endif()
  set(_srcs ${CXX_LINT_SOURCES})

  find_program(CXX_IWYU_TOOL NAMES iwyu_tool.py iwyu-tool)
  if(CXX_IWYU_TOOL AND EXISTS "${CMAKE_BINARY_DIR}/compile_commands.json")
    add_custom_target(iwyu-check
      COMMAND "${CXX_IWYU_TOOL}" -p "${CMAKE_BINARY_DIR}" ${_srcs}
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      COMMENT "IWYU scan (iwyu_tool.py -p build)"
      VERBATIM)
    cxx_msg(grass "iwyu-check target registered")
  else()
    cxx_msg(gray "iwyu-check skipped (need iwyu_tool.py + compile_commands.json)")
  endif()
endfunction()

cxx_register_apply_gate(cxx_apply_iwyu)
cxx_register_finish_hook(cxx_register_iwyu_targets)
