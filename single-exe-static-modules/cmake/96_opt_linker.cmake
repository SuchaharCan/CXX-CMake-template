# =============================================================================
# 96_opt_linker.cmake — 可选 LLVM lld 链接器（-fuse-ld=lld）
# 分层: opt（可选，手动开启）
# 独立: 否
# 上游: 00_core_compiler.cmake（CXX_LLVM_ROOT）；仅 Clang 有效
# 下游: 10_core_setup.cmake（自注册 apply 门禁 cxx_apply_lld）
# 启用: cmake .. -DCXX_USE_LLD=ON
# =============================================================================

include_guard(GLOBAL)

option(CXX_USE_LLD "Link with LLVM lld when available (Clang)" OFF)

# -----------------------------------------------------------------------------
# cxx_apply_lld(target) — Clang 时注入 -fuse-ld=lld
# -----------------------------------------------------------------------------
function(cxx_apply_lld target)
  if(NOT CXX_USE_LLD)
    return()
  endif()
  if(NOT TARGET ${target})
    message(FATAL_ERROR "cxx_apply_lld: missing ${target}")
  endif()
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    cxx_msg(yellow "lld: requires Clang; skip ${target}")
    return()
  endif()
  find_program(_CXX_LLD NAMES ld.lld lld
    PATHS "${CXX_LLVM_ROOT}/bin" "/opt/homebrew/opt/llvm/bin" NO_DEFAULT_PATH)
  if(NOT _CXX_LLD)
    find_program(_CXX_LLD NAMES ld.lld lld)
  endif()
  if(NOT _CXX_LLD)
    cxx_msg(yellow "lld: not found (brew install llvm); skip")
    return()
  endif()
  target_link_options(${target} PRIVATE -fuse-ld=lld)
  cxx_msg(gray "lld (-fuse-ld=lld) → ${target}")
endfunction()

cxx_register_apply_gate(cxx_apply_lld)
