# =============================================================================
# 93_opt_benchmark.cmake — 可选 Google Benchmark + run-bench 目标
# 分层: opt（可选，默认 ON 但依赖缺失时自动跳过）
# 独立: 是
# 下游: 10_core_setup.cmake（自注册 finish 钩子 cxx_register_benchmark_run_targets）
#       根 CMakeLists（cxx_add_benchmark）
# 启用: cmake .. -DCXX_ENABLE_BENCHMARK=ON|OFF
# =============================================================================

include_guard(GLOBAL)

option(CXX_ENABLE_BENCHMARK "Discover Google Benchmark and allow bench targets" ON)
set(CXX_BENCHMARK_FOUND OFF CACHE INTERNAL "benchmark found" FORCE)
set(CXX_BENCHMARK_TARGETS "" CACHE INTERNAL "registered bench targets" FORCE)

# -----------------------------------------------------------------------------
# cxx_find_benchmark() — find_package(benchmark)，设置 CXX_BENCHMARK_FOUND
# -----------------------------------------------------------------------------
function(cxx_find_benchmark)
  if(NOT CXX_ENABLE_BENCHMARK)
    return()
  endif()
  find_package(benchmark CONFIG QUIET)
  if(benchmark_FOUND)
    set(CXX_BENCHMARK_FOUND ON CACHE INTERNAL "benchmark found" FORCE)
    cxx_msg(grass "Google Benchmark found")
  else()
    set(CXX_BENCHMARK_FOUND OFF CACHE INTERNAL "benchmark found" FORCE)
    cxx_msg(yellow "Google Benchmark not found (brew install google-benchmark) — benches skipped")
  endif()
endfunction()

# -----------------------------------------------------------------------------
# cxx_add_benchmark(name sources...) — 创建并链接 benchmark 微基准目标
# -----------------------------------------------------------------------------
function(cxx_add_benchmark name)
  if(NOT CXX_BENCHMARK_FOUND)
    return()
  endif()
  add_executable(${name} ${ARGN})
  target_compile_features(${name} PRIVATE cxx_std_23)
  target_link_libraries(${name} PRIVATE benchmark::benchmark benchmark::benchmark_main)
  set(_list "${CXX_BENCHMARK_TARGETS}")
  list(APPEND _list "${name}")
  set(CXX_BENCHMARK_TARGETS "${_list}" CACHE INTERNAL "registered bench targets" FORCE)
  cxx_msg(grass "benchmark target ${name}")
endfunction()

# -----------------------------------------------------------------------------
# cxx_note_benchmark(name) — 手动创建的 bench target 登记
# -----------------------------------------------------------------------------
function(cxx_note_benchmark name)
  if(NOT TARGET ${name})
    return()
  endif()
  set(_list "${CXX_BENCHMARK_TARGETS}")
  list(APPEND _list "${name}")
  list(REMOVE_DUPLICATES _list)
  set(CXX_BENCHMARK_TARGETS "${_list}" CACHE INTERNAL "registered bench targets" FORCE)
endfunction()

# -----------------------------------------------------------------------------
# cxx_register_benchmark_run_targets() — run-bench 冒烟目标
# -----------------------------------------------------------------------------
function(cxx_register_benchmark_run_targets)
  if(NOT CXX_BENCHMARK_TARGETS)
    return()
  endif()
  set(_cmds)
  foreach(_t IN LISTS CXX_BENCHMARK_TARGETS)
    if(TARGET ${_t})
      list(APPEND _cmds COMMAND $<TARGET_FILE:${_t}>
        --benchmark_min_time=0.05s
        --benchmark_report_aggregates_only=true)
    endif()
  endforeach()
  if(NOT _cmds)
    return()
  endif()
  add_custom_target(run-bench
    ${_cmds}
    COMMENT "Google Benchmark smoke (min_time=0.05s) — tighten for regression CI"
    VERBATIM)
  cxx_msg(grass "run-bench target → ${CXX_BENCHMARK_TARGETS}")
endfunction()

cxx_register_finish_hook(cxx_register_benchmark_run_targets)
