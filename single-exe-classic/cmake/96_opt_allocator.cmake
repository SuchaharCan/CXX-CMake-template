# =============================================================================
# 96_opt_allocator.cmake — 可选 mimalloc / jemalloc（sanitizer/fuzz 下强制 system）
# 分层: opt（可选，手动切换）
# 独立: 是
# 下游: 10_core_setup.cmake（自注册 apply 门禁 cxx_link_allocator）
# 启用: cmake .. -DCXX_ALLOCATOR=mimalloc|jemalloc
# =============================================================================

include_guard(GLOBAL)

set(CXX_ALLOCATOR "system" CACHE STRING "system | mimalloc | jemalloc")
set_property(CACHE CXX_ALLOCATOR PROPERTY STRINGS system mimalloc jemalloc)

# -----------------------------------------------------------------------------
# cxx_link_allocator(target) — 按 CXX_ALLOCATOR 链接
# -----------------------------------------------------------------------------
function(cxx_link_allocator target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "cxx_link_allocator: missing ${target}")
  endif()
  if(CXX_ALLOCATOR STREQUAL "system")
    return()
  endif()
  if(CXX_ENABLE_SANITIZERS OR CXX_ENABLE_FUZZERS)
    cxx_msg(yellow "allocator=${CXX_ALLOCATOR} skipped (sanitizer/fuzzer → system)")
    return()
  endif()
  find_package(${CXX_ALLOCATOR} CONFIG QUIET)
  if(TARGET ${CXX_ALLOCATOR}::${CXX_ALLOCATOR})
    target_link_libraries(${target} PRIVATE ${CXX_ALLOCATOR}::${CXX_ALLOCATOR})
    cxx_msg(gray "allocator ${CXX_ALLOCATOR} → ${target}")
    return()
  endif()
  find_library(_alloc_lib NAMES ${CXX_ALLOCATOR} mimalloc jemalloc)
  if(_alloc_lib)
    target_link_libraries(${target} PRIVATE ${_alloc_lib})
    cxx_msg(gray "allocator lib ${_alloc_lib} → ${target}")
  else()
    cxx_msg(yellow "allocator ${CXX_ALLOCATOR} not found; system malloc")
  endif()
endfunction()

cxx_register_apply_gate(cxx_link_allocator)
