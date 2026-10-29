# =============================================================================
# 97_opt_rapidcheck.cmake — 可选基于属性的测试（RapidCheck）
# 分层: opt（可选，默认 ON 但依赖缺失时自动跳过）
# 独立: 是
# 下游: 根 CMakeLists / 10_core_setup.cmake（CXX_RAPIDCHECK_FOUND）
# 启用: cmake .. -DCXX_ENABLE_RAPIDCHECK=ON|OFF
# =============================================================================

include_guard(GLOBAL)

option(CXX_ENABLE_RAPIDCHECK "Build RapidCheck property tests when available" ON)
set(CXX_RAPIDCHECK_FOUND OFF CACHE INTERNAL "" FORCE)

# -----------------------------------------------------------------------------
# cxx_find_rapidcheck() — 探测 RapidCheck，设置 CXX_RAPIDCHECK_FOUND
# -----------------------------------------------------------------------------
function(cxx_find_rapidcheck)
  if(NOT CXX_ENABLE_RAPIDCHECK)
    return()
  endif()
  find_package(rapidcheck CONFIG QUIET)
  if(rapidcheck_FOUND OR TARGET rapidcheck)
    set(CXX_RAPIDCHECK_FOUND ON CACHE INTERNAL "" FORCE)
    cxx_msg(grass "RapidCheck found")
  else()
    cxx_msg(yellow "RapidCheck not found — property tests skipped")
  endif()
endfunction()
