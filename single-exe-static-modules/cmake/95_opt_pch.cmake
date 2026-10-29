# =============================================================================
# 95_opt_pch.cmake — 可选预编译头（经典 TU；模块模板慎用）
# 分层: opt（可选，手动开启）
# 独立: 是
# 下游: 10_core_setup.cmake（自注册 apply 门禁 cxx_apply_pch）
# 启用: cmake .. -DCXX_ENABLE_PCH=ON
# =============================================================================

include_guard(GLOBAL)

option(CXX_ENABLE_PCH "Enable target_precompile_headers on executables/libs" OFF)

# -----------------------------------------------------------------------------
# cxx_apply_pch(target) — 预编译常用标准库头
# -----------------------------------------------------------------------------
function(cxx_apply_pch target)
  if(NOT CXX_ENABLE_PCH)
    return()
  endif()
  if(NOT TARGET ${target})
    message(FATAL_ERROR "cxx_apply_pch: missing ${target}")
  endif()
  target_precompile_headers(${target} PRIVATE
    <vector>
    <string>
    <memory>
    <span>
    <cstdint>
    <expected>
  )
  cxx_msg(gray "PCH → ${target}")
endfunction()

cxx_register_apply_gate(cxx_apply_pch)
