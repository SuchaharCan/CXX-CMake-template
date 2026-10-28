# =============================================================================
# 00_core_profile.cmake — Debug / Release 编译画像
# 分层: core（必选）
# 独立: 是 — 只依赖 CMake 内置命令与生成器表达式
# 上游: 无
# 下游: 10_core_setup.cmake（cxx_apply_template_defaults 逐个 target 应用）
# 功能:
#   - Debug: CXX_TUTORIAL_DEBUG_TRACE + libc++ hardening
#   - Release/RelWithDebInfo: -O3、visibility、function/data sections、dead_strip
# =============================================================================

include_guard(GLOBAL)

set(TARGET_ARCH "native" CACHE STRING "Reserved: -march for future non-default targets")

# -----------------------------------------------------------------------------
# cxx_apply_debug_profile(target) — Debug 配置画像
# -----------------------------------------------------------------------------
function(cxx_apply_debug_profile target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "cxx_apply_debug_profile: target '${target}' does not exist")
  endif()
  target_compile_definitions(${target} PRIVATE
      $<$<CONFIG:Debug>:CXX_TUTORIAL_DEBUG_TRACE=1>)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    target_compile_definitions(${target} PRIVATE
        $<$<CONFIG:Debug>:_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG>)
  endif()
  cxx_msg(gray "debug profile → ${target}")
endfunction()

# -----------------------------------------------------------------------------
# cxx_apply_release_optimizations(target) — Release/RelWithDebInfo 优化
# -----------------------------------------------------------------------------
function(cxx_apply_release_optimizations target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "cxx_apply_release_optimizations: target '${target}' does not exist")
  endif()
  target_compile_options(${target} PRIVATE
      $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-O3>
      $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-fvisibility=hidden>
      $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-fvisibility-inlines-hidden>
      $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-ffunction-sections>
      $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-fdata-sections>)
  cxx_msg(gray "release optimizations → ${target}")
endfunction()

# -----------------------------------------------------------------------------
# cxx_apply_release_executable_optimizations(target) — 链接期瘦身
# -----------------------------------------------------------------------------
function(cxx_apply_release_executable_optimizations target)
  if(NOT TARGET ${target})
    return()
  endif()
  if(APPLE)
    target_link_options(${target} PRIVATE
        $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-Wl,-dead_strip>)
  else()
    target_link_options(${target} PRIVATE
        $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-Wl,--gc-sections>)
  endif()
  cxx_msg(gray "release exe link opts → ${target}")
endfunction()
