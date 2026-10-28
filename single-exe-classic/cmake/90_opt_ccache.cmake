# =============================================================================
# 90_opt_ccache.cmake — 可选 ccache 编译缓存（着色/日志见 00_core_buildlog.cmake）
# 分层: opt（可选，默认 ON 但缺失时自动跳过）
# 独立: 是（有顶层副作用：设置 CMAKE_CXX_COMPILER_LAUNCHER）
# 下游: 10_core_setup.cmake（bootstrap 显式 include，先于其他可选模块）
# 启用: cmake .. -DCXX_ENABLE_CCACHE=ON|OFF
# =============================================================================

include_guard(GLOBAL)

option(CXX_ENABLE_CCACHE "Use ccache as compiler launcher when available" ON)

set(CXX_CCACHE_ENABLED OFF CACHE BOOL "Whether ccache launcher is active" FORCE)

get_filename_component(_CXX_CCACHE_BASEDIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# -----------------------------------------------------------------------------
# cxx_register_ccache_targets() — 注册 ccache-stats / ccache-zero 手动目标
# -----------------------------------------------------------------------------
function(cxx_register_ccache_targets)
  if(NOT CXX_CCACHE_ENABLED OR NOT CCACHE_PROGRAM)
    return()
  endif()
  add_custom_target(ccache-stats
    COMMAND "${CCACHE_PROGRAM}" -s
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Show ccache hit/miss statistics"
    VERBATIM)
  add_custom_target(ccache-zero
    COMMAND "${CCACHE_PROGRAM}" -z
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Reset ccache statistics counters"
    VERBATIM)
endfunction()

if(NOT CXX_ENABLE_CCACHE)
  if(COMMAND cxx_msg)
    cxx_msg(yellow "ccache: disabled (CXX_ENABLE_CCACHE=OFF)")
  endif()
  return()
endif()

find_program(CCACHE_PROGRAM ccache)
if(NOT CCACHE_PROGRAM)
  if(COMMAND cxx_msg)
    cxx_msg(yellow "ccache: not found — brew install ccache")
  endif()
  return()
endif()

if(NOT DEFINED ENV{CCACHE_BASEDIR})
  set(ENV{CCACHE_BASEDIR} "${_CXX_CCACHE_BASEDIR}")
endif()
if(NOT DEFINED ENV{CCACHE_SLOPPINESS})
  set(ENV{CCACHE_SLOPPINESS} "time_macros,include_token_merging,pch_defines,include_guard")
endif()

set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "C++ compiler launcher" FORCE)
set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "C compiler launcher" FORCE)
set(CXX_CCACHE_ENABLED ON CACHE BOOL "Whether ccache launcher is active" FORCE)
