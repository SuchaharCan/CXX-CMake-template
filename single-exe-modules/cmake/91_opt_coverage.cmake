# =============================================================================
# 91_opt_coverage.cmake — 可选 llvm-cov 覆盖率插桩（Clang）
# 分层: opt（可选，手动开启）
# 独立: 是
# 下游: 10_core_setup.cmake（自注册 apply 门禁 cxx_apply_coverage）
# 启用: cmake .. -DCXX_ENABLE_COVERAGE=ON
# =============================================================================

include_guard(GLOBAL)

option(CXX_ENABLE_COVERAGE "Instrument with -fprofile-instr-generate -fcoverage-mapping" OFF)

# -----------------------------------------------------------------------------
# cxx_apply_coverage(target) — CXX_ENABLE_COVERAGE=ON 时插桩
# -----------------------------------------------------------------------------
function(cxx_apply_coverage target)
  if(NOT CXX_ENABLE_COVERAGE)
    return()
  endif()
  if(CXX_ENABLE_FUZZERS)
    cxx_msg(yellow "coverage skipped for ${target}（CXX_ENABLE_FUZZERS=ON，与 libFuzzer 覆盖率互斥）")
    return()
  endif()
  if(NOT TARGET ${target})
    message(FATAL_ERROR "cxx_apply_coverage: missing ${target}")
  endif()
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    cxx_msg(yellow "coverage requires Clang; skip ${target}")
    return()
  endif()
  target_compile_options(${target} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
  target_link_options(${target} PRIVATE -fprofile-instr-generate)
  cxx_msg(gray "coverage on ${target}")
endfunction()

cxx_register_apply_gate(cxx_apply_coverage)
