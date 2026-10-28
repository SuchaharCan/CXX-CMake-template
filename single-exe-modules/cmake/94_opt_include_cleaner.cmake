# =============================================================================
# 94_opt_include_cleaner.cmake — 可选 LLVM include-cleaner（clang 语义的 include 检查）
# 分层: opt（可选，手动开启）
# 独立: 是（仅被 00_core_lint 的 tidy 配置解析读取 CXX_ENABLE_INCLUDE_CLEANER）
# 下游: 10_core_setup.cmake（自注册 finish 钩子）
# 启用: cmake .. -DCXX_ENABLE_INCLUDE_CLEANER=ON
# =============================================================================

include_guard(GLOBAL)

option(CXX_ENABLE_INCLUDE_CLEANER
       "Use include-cleaner clang-tidy profile + include-cleaner-check target" OFF)

# -----------------------------------------------------------------------------
# cxx_register_include_cleaner_targets() — include-cleaner-check 手动扫描
# -----------------------------------------------------------------------------
function(cxx_register_include_cleaner_targets)
  if(NOT CXX_ENABLE_INCLUDE_CLEANER)
    return()
  endif()

  set(_paths)
  if(CXX_LLVM_BIN)
    list(APPEND _paths "${CXX_LLVM_BIN}")
  endif()
  find_program(CXX_INCLUDE_CLEANER NAMES clang-include-cleaner include-cleaner
    PATHS ${_paths} NO_DEFAULT_PATH)
  if(NOT CXX_INCLUDE_CLEANER)
    find_program(CXX_INCLUDE_CLEANER NAMES clang-include-cleaner include-cleaner)
  endif()

  if(DEFINED CXX_TIDY_PLAIN_SOURCES AND CXX_TIDY_PLAIN_SOURCES)
    set(_srcs ${CXX_TIDY_PLAIN_SOURCES})
  else()
    set(_srcs
      "${CMAKE_SOURCE_DIR}/src/ScenarioLog.cxx"
      "${CMAKE_SOURCE_DIR}/src/ScenarioLog.hxx")
  endif()

  if(CXX_INCLUDE_CLEANER AND EXISTS "${CMAKE_BINARY_DIR}/compile_commands.json")
    add_custom_target(include-cleaner-check
      COMMAND "${CXX_INCLUDE_CLEANER}" -p "${CMAKE_BINARY_DIR}" ${_srcs}
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      COMMENT "clang-include-cleaner scan"
      VERBATIM)
    cxx_msg(grass "include-cleaner-check registered (${CXX_INCLUDE_CLEANER})")
  else()
    cxx_msg(yellow "include-cleaner-check skipped (need clang-include-cleaner + compile_commands.json)")
  endif()
endfunction()

cxx_register_finish_hook(cxx_register_include_cleaner_targets)
