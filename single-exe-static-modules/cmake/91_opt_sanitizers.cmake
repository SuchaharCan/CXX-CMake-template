# =============================================================================
# 91_opt_sanitizers.cmake — 可选 ASan / UBSan / TSan（模式互斥）
# 分层: opt（可选，手动开启）
# 独立: 是
# 下游: 10_core_setup.cmake（自注册 apply 门禁 cxx_apply_sanitizers）
# 启用: cmake .. -DCXX_ENABLE_SANITIZERS=ON [-DCXX_SANITIZER=address,undefined|thread]
# =============================================================================

include_guard(GLOBAL)

option(CXX_ENABLE_SANITIZERS
       "Enable sanitizer instrumentation (see CXX_SANITIZER)" OFF)
set(CXX_SANITIZER "" CACHE STRING
    "empty|address|undefined|thread|address,undefined — empty uses address,undefined when enabled")
set_property(CACHE CXX_SANITIZER PROPERTY STRINGS
  "" address undefined thread "address,undefined")

# -----------------------------------------------------------------------------
# cxx_apply_sanitizers(target) — 按 CXX_SANITIZER 注入 -fsanitize=*
# -----------------------------------------------------------------------------
function(cxx_apply_sanitizers target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "cxx_apply_sanitizers: target '${target}' does not exist")
  endif()
  if(NOT CXX_ENABLE_SANITIZERS)
    return()
  endif()
  if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    cxx_msg(yellow "Sanitizers requested but CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}; skipping for ${target}")
    return()
  endif()
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    message(WARNING "Sanitizers require Clang; skipping for ${target}")
    return()
  endif()

  set(_mode "${CXX_SANITIZER}")
  if(_mode STREQUAL "")
    set(_mode "address,undefined")
  endif()
  if(_mode STREQUAL "thread")
    if(CXX_ALLOCATOR AND NOT CXX_ALLOCATOR STREQUAL "system")
      cxx_msg(yellow "TSan: prefer CXX_ALLOCATOR=system")
    endif()
  endif()

  target_compile_options(${target} PRIVATE
      -fsanitize=${_mode}
      -fno-omit-frame-pointer
      -fno-sanitize-recover=all)
  target_link_options(${target} PRIVATE
      -fsanitize=${_mode})
  cxx_msg(gray "sanitizer=${_mode} → ${target}")
endfunction()

cxx_register_apply_gate(cxx_apply_sanitizers)
