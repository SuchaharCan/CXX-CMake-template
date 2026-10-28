# =============================================================================
# 93_opt_fuzz.cmake — 可选 libFuzzer harness（Clang：-fsanitize=fuzzer,address）
# 分层: opt（可选，手动开启）
# 独立: 是
# 下游: 10_core_setup.cmake（自注册 finish 钩子 cxx_register_fuzz_run_targets）
#       根 CMakeLists（cxx_add_fuzzer）
# 启用: cmake .. -DCXX_ENABLE_FUZZERS=ON
# 语料: fuzz/corpus/<name>/（可选）；运行: cmake --build . --target run-fuzz
# =============================================================================

include_guard(GLOBAL)

option(CXX_ENABLE_FUZZERS "Build libFuzzer targets" OFF)
set(CXX_FUZZ_TARGETS "" CACHE INTERNAL "registered fuzzer targets" FORCE)
set(CXX_FUZZ_MAX_TOTAL_TIME "5" CACHE STRING "Default seconds for run-fuzz smoke")

# -----------------------------------------------------------------------------
# cxx_apply_libfuzzer_flags(target) — 注入 libFuzzer + ASan（仅 Clang）
# -----------------------------------------------------------------------------
function(cxx_apply_libfuzzer_flags target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "cxx_apply_libfuzzer_flags: missing ${target}")
  endif()
  target_compile_options(${target} PRIVATE
    -fsanitize=fuzzer,address -fno-omit-frame-pointer -fno-sanitize-recover=all)
  target_link_options(${target} PRIVATE -fsanitize=fuzzer,address)
endfunction()

# -----------------------------------------------------------------------------
# cxx_add_fuzzer(name [SOURCES ...] [LIBS ...] [CORPUS path])
# -----------------------------------------------------------------------------
function(cxx_add_fuzzer name)
  if(NOT CXX_ENABLE_FUZZERS)
    return()
  endif()
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    cxx_msg(yellow "fuzzers require Clang; skip ${name}")
    return()
  endif()
  cmake_parse_arguments(_fz "" "CORPUS" "SOURCES;LIBS" ${ARGN})
  if(NOT _fz_SOURCES)
    message(FATAL_ERROR "cxx_add_fuzzer(${name}): SOURCES required")
  endif()
  add_executable(${name} ${_fz_SOURCES})
  target_compile_features(${name} PRIVATE cxx_std_23)
  cxx_apply_libfuzzer_flags(${name})
  if(_fz_LIBS)
    target_link_libraries(${name} PRIVATE ${_fz_LIBS})
  endif()
  target_include_directories(${name} PRIVATE
    "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src")
  # 不套完整模板默认（会重复注入冲突 sanitizer）；只挂核心两项
  cxx_apply_warnings(${name})
  cxx_apply_llvm_runtime(${name})

  if(NOT _fz_CORPUS)
    set(_fz_CORPUS "${CMAKE_SOURCE_DIR}/fuzz/corpus/${name}")
  endif()
  set_property(TARGET ${name} PROPERTY CXX_FUZZ_CORPUS "${_fz_CORPUS}")

  set(_list "${CXX_FUZZ_TARGETS}")
  list(APPEND _list "${name}")
  set(CXX_FUZZ_TARGETS "${_list}" CACHE INTERNAL "registered fuzzer targets" FORCE)
  cxx_msg(grass "fuzzer ${name}")
endfunction()

# -----------------------------------------------------------------------------
# cxx_note_fuzzer(name) — 手动创建的 fuzz target 登记
# -----------------------------------------------------------------------------
function(cxx_note_fuzzer name)
  if(NOT TARGET ${name})
    return()
  endif()
  set(_list "${CXX_FUZZ_TARGETS}")
  list(APPEND _list "${name}")
  list(REMOVE_DUPLICATES _list)
  set(CXX_FUZZ_TARGETS "${_list}" CACHE INTERNAL "registered fuzzer targets" FORCE)
  get_property(_existing TARGET ${name} PROPERTY CXX_FUZZ_CORPUS)
  if(NOT _existing)
    set_property(TARGET ${name} PROPERTY CXX_FUZZ_CORPUS
      "${CMAKE_SOURCE_DIR}/fuzz/corpus/${name}")
  endif()
endfunction()

# -----------------------------------------------------------------------------
# cxx_register_fuzz_run_targets() — run-fuzz 短时冒烟
# -----------------------------------------------------------------------------
function(cxx_register_fuzz_run_targets)
  if(NOT CXX_ENABLE_FUZZERS OR NOT CXX_FUZZ_TARGETS)
    return()
  endif()
  set(_cmds)
  foreach(_t IN LISTS CXX_FUZZ_TARGETS)
    if(NOT TARGET ${_t})
      continue()
    endif()
    get_property(_corpus TARGET ${_t} PROPERTY CXX_FUZZ_CORPUS)
    if(_corpus AND EXISTS "${_corpus}")
      list(APPEND _cmds COMMAND "$<TARGET_FILE:${_t}>"
        "${_corpus}"
        "-max_total_time=${CXX_FUZZ_MAX_TOTAL_TIME}"
        -print_final_stats=1)
    else()
      list(APPEND _cmds COMMAND "$<TARGET_FILE:${_t}>"
        "-max_total_time=${CXX_FUZZ_MAX_TOTAL_TIME}"
        -print_final_stats=1)
    endif()
  endforeach()
  if(NOT _cmds)
    return()
  endif()
  add_custom_target(run-fuzz
    ${_cmds}
    COMMENT "libFuzzer smoke (${CXX_FUZZ_MAX_TOTAL_TIME}s each)"
    VERBATIM)
  cxx_msg(grass "run-fuzz target → ${CXX_FUZZ_TARGETS}")
endfunction()

cxx_register_finish_hook(cxx_register_fuzz_run_targets)
