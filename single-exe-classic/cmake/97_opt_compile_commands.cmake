# =============================================================================
# 97_opt_compile_commands.cmake — 可选 compile_commands.json 源码根符号链接
# 分层: opt（可选，默认 ON，供 clangd / clang-tidy / IWYU 使用）
# 独立: 是
# 下游: 10_core_setup.cmake（bootstrap 中调用 cxx_symlink_compile_commands）
# 启用: cmake .. -DCXX_SYMLINK_COMPILE_COMMANDS=ON|OFF
# =============================================================================

include_guard(GLOBAL)

option(CXX_SYMLINK_COMPILE_COMMANDS "Symlink build/compile_commands.json to source root" OFF)

# -----------------------------------------------------------------------------
# cxx_symlink_compile_commands() — 源码根建符号链接（Unix）
# -----------------------------------------------------------------------------
function(cxx_symlink_compile_commands)
  if(NOT CXX_SYMLINK_COMPILE_COMMANDS)
    return()
  endif()
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "" FORCE)
  set(_src "${CMAKE_BINARY_DIR}/compile_commands.json")
  set(_dst "${CMAKE_SOURCE_DIR}/compile_commands.json")
  if(CMAKE_HOST_UNIX)
    add_custom_target(compile-commands-link ALL
      COMMAND ${CMAKE_COMMAND} -E rm -f "${_dst}"
      COMMAND ${CMAKE_COMMAND} -E create_symlink "${_src}" "${_dst}"
      COMMENT "symlink compile_commands.json → source root"
      VERBATIM)
  endif()
endfunction()
