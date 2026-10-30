# =============================================================================
# 10_core_setup.cmake — 必选编排器（根 CMakeLists 只写业务 + 调用本文件 API）
# 分层: core（必选，编号 10 保证排在 00_* 之后加载）
# 独立: 否
# 上游: 00_core_compiler / 00_core_color / 00_core_log / 00_core_deps
#       00_core_buildlog / 00_core_profile / 00_core_lint
#       可选模块: cmake/90_opt_*.cmake（本文件 bootstrap 时按需 include）
# 下游: 根 CMakeLists（cxx_setup_bootstrap / cxx_phase_* / cxx_apply_template_defaults）
# 功能:
#   - 可选模块注册机制: cxx_register_apply_gate / cxx_register_finish_hook
#     （新增 90_opt_*.cmake 即可自动接入，无需改本文件）
#   - cxx_setup_bootstrap: tee+grc 着色、ccache、可选模块加载、依赖检测、标准/构建类型
#   - cxx_phase_*: 配置输出阶段（依赖检测 / 变量分组 / 收尾）
#   - cxx_apply_template_defaults: 一键注入核心门禁 + 可选门禁
# =============================================================================

include_guard(GLOBAL)

set(CXX_CMAKE_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL
    "cmake/ module dir (macro 内不可用 CMAKE_CURRENT_LIST_DIR)")
set(CXX_APPLY_GATES "" CACHE INTERNAL "registered per-target gates (functions)" FORCE)
set(CXX_FINISH_HOOKS "" CACHE INTERNAL "registered finish hooks (functions)" FORCE)

option(CXX_SHOW_CMAKE_CONFIG
       "Print CMake configure and compiler-probe detail messages" OFF)
option(CXX_SHOW_CMAKE_VARIABLES
       "Print grouped CMake variable values" ON)

# -----------------------------------------------------------------------------
# cxx_register_apply_gate(fn) — 可选模块注册 per-target 门禁（可扩展点）
# -----------------------------------------------------------------------------
function(cxx_register_apply_gate fn)
  if(NOT fn IN_LIST CXX_APPLY_GATES)
    list(APPEND CXX_APPLY_GATES "${fn}")
    set(CXX_APPLY_GATES "${CXX_APPLY_GATES}" CACHE INTERNAL "gates" FORCE)
  endif()
endfunction()

# -----------------------------------------------------------------------------
# cxx_register_finish_hook(fn) — 可选模块注册配置收尾钩子（可扩展点）
# -----------------------------------------------------------------------------
function(cxx_register_finish_hook fn)
  if(NOT fn IN_LIST CXX_FINISH_HOOKS)
    list(APPEND CXX_FINISH_HOOKS "${fn}")
    set(CXX_FINISH_HOOKS "${CXX_FINISH_HOOKS}" CACHE INTERNAL "hooks" FORCE)
  endif()
endfunction()

# -----------------------------------------------------------------------------
# cxx_set_lint_sources(rel_path...)
# 作用：登记 format/tidy 源（相对源码根）。
# -----------------------------------------------------------------------------
function(cxx_set_lint_sources)
  set(_abs)
  foreach(_f IN LISTS ARGN)
    list(APPEND _abs "${CMAKE_SOURCE_DIR}/${_f}")
  endforeach()
  set(CXX_LINT_SOURCES "${_abs}" PARENT_SCOPE)
endfunction()

# -----------------------------------------------------------------------------
# cxx_setup_bootstrap()
# 作用：project() 后首次调用；装 tee/grc、加载可选模块、拉齐门禁、设 C++ 标准。
# -----------------------------------------------------------------------------
macro(cxx_setup_bootstrap)
  cxx_sanitize_linker_flags()
  cxx_install_build_tool_tee()

  # ccache 有顶层副作用（设置 launcher），先于其余可选模块加载
  include(${CMAKE_SOURCE_DIR}/cmake/90_opt_ccache.cmake)
  if(COMMAND cxx_register_ccache_targets)
    cxx_register_ccache_targets()
  endif()

  # 其余可选模块自动发现：新增 90_opt_*.cmake 即接入，无需改本文件
  file(GLOB _cxx_opt_mods CONFIGURE_DEPENDS "${CXX_CMAKE_MODULE_DIR}/9*_opt_*.cmake")
  list(SORT _cxx_opt_mods)
  foreach(_cxx_opt IN LISTS _cxx_opt_mods)
    get_filename_component(_cxx_opt_name "${_cxx_opt}" NAME)
    if(_cxx_opt_name STREQUAL "90_opt_ccache.cmake")
      continue()
    endif()
    include("${_cxx_opt}")
  endforeach()
  unset(_cxx_opt_mods)
  unset(_cxx_opt)

  set(CMAKE_CXX_STANDARD 26)  # 本模板要求 C++26
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS ON)

  if(CXX_USING_CLANG AND CXX_LLVM_LIB)
    set(CMAKE_INSTALL_RPATH "${CXX_LLVM_LIB}")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
  endif()

  cxx_resolve_clang_tidy_config()
  if(COMMAND cxx_symlink_compile_commands)
    cxx_symlink_compile_commands()
  endif()
  if(COMMAND cxx_find_benchmark)
    cxx_find_benchmark()
  endif()
  if(COMMAND cxx_find_rapidcheck)
    cxx_find_rapidcheck()
  endif()

  cxx_deps_detect()
  enable_testing()

  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo")
    message(STATUS "No Build type selected, default to RelWithDebInfo")
  endif()

  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=return-type")
  endif()
endmacro()

# -----------------------------------------------------------------------------
# cxx_phase_toolchain() — 阶段 1：依赖检测报告
# -----------------------------------------------------------------------------
function(cxx_phase_toolchain)
  cxx_configure_phase("=== 1. 依赖检测 ===")
  cxx_deps_report()
  cxx_msg(cyan "Configure log: ${CXX_LOG_DIR}/cmake_configure.log")
endfunction()

# -----------------------------------------------------------------------------
# cxx_phase_variables() — 阶段 2：分组打印变量
# -----------------------------------------------------------------------------
function(cxx_phase_variables)
  if(NOT CXX_SHOW_CMAKE_VARIABLES)
    return()
  endif()

  cxx_configure_phase("=== 2. CMake 变量 ===")

  cxx_var_group(green "工具链 Toolchain"
    "cmake/00_core_compiler.cmake 或 cmake .. -DCXX_PREFERRED_COMPILER=g++|clang++")
  cxx_var_line(green CXX_PREFERRED_COMPILER)
  cxx_var_line(green CXX_USING_CLANG)
  cxx_var_line(green CXX_USING_GCC)
  cxx_var_line(green CXX_USING_LIBCXX)
  cxx_var_line(green CXX_HOMEBREW_PREFIX)
  cxx_var_line(green CXX_LLVM_ROOT)
  cxx_var_line(green CXX_GCC_ROOT)
  cxx_var_line(green CXX_GXX_PROGRAM)
  cxx_var_line(green CMAKE_CXX_COMPILER)
  cxx_var_line(green CMAKE_C_COMPILER)
  cxx_var_line(green CMAKE_CXX_COMPILER_ID)
  cxx_var_line(green CMAKE_CXX_COMPILER_VERSION)
  cxx_var_line(green CMAKE_AR)
  cxx_var_line(green CMAKE_RANLIB)
  cxx_var_line(green CMAKE_OSX_SYSROOT)
  cxx_var_line(green CMAKE_MAKE_PROGRAM)
  cxx_var_line(green CXX_REAL_BUILD_TOOL)
  cxx_var_line(green CXX_GRCAT)
  cxx_var_line(green CXX_CCACHE_ENABLED)
  cxx_var_line(green CCACHE_PROGRAM)
  cxx_var_line(green CMAKE_CXX_COMPILER_LAUNCHER)

  cxx_var_group(blue "依赖检测 Dependencies"
    "cmake/00_core_deps.cmake（最小版本见 CXX_MIN_*）")
  cxx_var_line(blue CMAKE_VERSION)
  cxx_var_line(blue CXX_JUST_PROGRAM)
  cxx_var_line(blue CXX_FMT_FOUND)
  cxx_var_line(blue CXX_BQLOG_FOUND)
  cxx_var_line(blue CXX_BQLOG_VERSION)
  cxx_var_line(blue CXX_BQLOG_MODE)
  cxx_var_line(blue CXX_BQLOG_LINK)
  cxx_var_line(blue CXX_BQLOG_INCLUDE_DIR)
  cxx_var_line(blue CXX_BQLOG_LIBRARY)
  cxx_var_line(blue CXX_BQLOG_TARGET)

  cxx_var_group(cyan "项目基础 Project"
    "根 CMakeLists.txt 的 project()/STR_TEST/add_executable；cmake/single-exe-static-modules.h.in")
  cxx_var_line(cyan CMAKE_PROJECT_NAME)
  cxx_var_line(cyan PROJECT_NAME)
  cxx_var_line(cyan PROJECT_VERSION)
  cxx_var_line(cyan PROJECT_SOURCE_DIR)
  cxx_var_line(cyan PROJECT_BINARY_DIR)
  cxx_var_line(cyan CMAKE_BUILD_TYPE)
  cxx_var_line(cyan CMAKE_CXX_STANDARD)
  cxx_var_line(cyan CMAKE_CXX_EXTENSIONS)
  cxx_var_line(cyan CMAKE_GENERATOR)
  if(DEFINED STR_TEST)
    cxx_var_line(cyan STR_TEST)
  endif()
  cxx_var_line(cyan CXX_LOG_DIR)
  cxx_var_line(cyan CXX_LOG_MAX_LINES)
  cxx_var_line(cyan CXX_BUILD_LOG_MAX_LINES)
  cxx_var_line(cyan CXX_SHOW_CMAKE_CONFIG)
  cxx_var_line(cyan CXX_SHOW_CMAKE_VARIABLES)

  cxx_var_group(purple "语法检测 Lint"
    "cmake/00_core_lint.cmake；cmake .. -DCXX_ENABLE_CLANG_TIDY= / -DCXX_ENABLE_CLANG_FORMAT=")
  cxx_var_line(purple CXX_ENABLE_CLANG_TIDY)
  cxx_var_line(purple CXX_ENABLE_CLANG_FORMAT)
  cxx_var_line(purple CXX_SAFETY_LINT)
  cxx_var_line(purple CXX_CLANG_TIDY_CONFIG_FILE)
  cxx_var_line(purple CXX_ENABLE_SANITIZERS)
  cxx_var_line(purple CXX_SANITIZER)
  cxx_var_line(purple CXX_ENABLE_GTEST)
  cxx_var_line(purple CXX_ENABLE_CCACHE)
  cxx_var_line(purple CXX_ENABLE_PCH)
  cxx_var_line(purple CXX_ENABLE_IWYU)
  cxx_var_line(purple CXX_ENABLE_INCLUDE_CLEANER)
  cxx_var_line(purple CXX_ENABLE_DOCS)
  cxx_var_line(purple CXX_USE_LLD)
  cxx_var_line(purple CXX_ALLOCATOR)
  cxx_var_line(purple CXX_ENABLE_COVERAGE)
  cxx_var_line(purple CXX_ENABLE_FUZZERS)
  cxx_var_line(purple CXX_ENABLE_BENCHMARK)
  cxx_var_line(purple CXX_GTEST_FOUND)
  cxx_var_line(purple CXX_BENCHMARK_FOUND)
  cxx_var_line(purple CXX_RAPIDCHECK_FOUND)
endfunction()

# -----------------------------------------------------------------------------
# cxx_apply_template_defaults(target)
# 作用：核心门禁 + 可选模块自注册门禁（可扩展：新模块 cxx_register_apply_gate 即可）。
# -----------------------------------------------------------------------------
function(cxx_apply_static_module_defaults target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "cxx_apply_static_module_defaults: '${target}' missing")
  endif()
  # 核心（必选）门禁
  cxx_apply_warnings(${target})
  cxx_apply_debug_profile(${target})
  cxx_apply_release_optimizations(${target})
  cxx_apply_release_executable_optimizations(${target})
  cxx_apply_clang_tidy(${target})
  cxx_apply_clang_format_gate(${target})
  cxx_apply_llvm_runtime(${target})
  # 可选（自注册）门禁
  foreach(_gate IN LISTS CXX_APPLY_GATES)
    if(COMMAND ${_gate})
      cmake_language(CALL ${_gate} ${target})
    endif()
  endforeach()
endfunction()

# 兼容旧 API：默认只注入核心门禁（bqlog 由 interface_log 显式链接）
function(cxx_apply_template_defaults target)
  cxx_apply_static_module_defaults(${target})
endfunction()

# -----------------------------------------------------------------------------
# cxx_phase_finish() — 配置收尾 + 注册手动目标
# -----------------------------------------------------------------------------
function(cxx_phase_finish)
  cxx_register_lint_targets()
  foreach(_hook IN LISTS CXX_FINISH_HOOKS)
    if(COMMAND ${_hook})
      cmake_language(CALL ${_hook})
    endif()
  endforeach()
  cxx_banner("CMake 配置完成")
  cxx_msg(grass "请执行: cmake --build . -j9")
  cxx_msg(cyan "可选目标: tidy-check | syntax-check | run-bench | run-fuzz | docs")
  cxx_msg(cyan "Build log: ${CXX_LOG_DIR}/cmake_build.log")
  cxx_msg(gray "CXX_PREFERRED_COMPILER=${CXX_PREFERRED_COMPILER} tidy=${CXX_ENABLE_CLANG_TIDY} format=${CXX_ENABLE_CLANG_FORMAT}")
endfunction()
