# =============================================================================
# 95_opt_unity.cmake — 可选 UNITY_BUILD（经典 TU；模块模板勿用）
# 分层: opt（可选，手动开启）
# 独立: 是
# 下游: 10_core_setup.cmake（自注册 apply 门禁 cxx_apply_unity_build）
# 启用: cmake .. -DCXX_ENABLE_UNITY_BUILD=ON
# =============================================================================

include_guard(GLOBAL)

option(CXX_ENABLE_UNITY_BUILD "Enable UNITY_BUILD on applicable targets" OFF)
set(CXX_UNITY_BUILD_BATCH_SIZE "8" CACHE STRING "UNITY_BUILD_BATCH_SIZE")

# -----------------------------------------------------------------------------
# cxx_apply_unity_build(target) — 合并编译单元
# -----------------------------------------------------------------------------
function(cxx_apply_unity_build target)
  if(NOT CXX_ENABLE_UNITY_BUILD)
    return()
  endif()
  if(NOT TARGET ${target})
    message(FATAL_ERROR "cxx_apply_unity_build: missing ${target}")
  endif()
  set_target_properties(${target} PROPERTIES
    UNITY_BUILD ON
    UNITY_BUILD_BATCH_SIZE "${CXX_UNITY_BUILD_BATCH_SIZE}")
  cxx_msg(gray "UNITY_BUILD → ${target}")
endfunction()

cxx_register_apply_gate(cxx_apply_unity_build)
