# =============================================================================
# 00_core_modules.cmake — C++23 模块 API（FILE_SET CXX_MODULES + 原生 import std）
# 分层: core（必选，模块模板专用）
# 独立: 否
# 上游: 00_core_compiler.cmake（编译器发现 / Ninja 强制 / scan-deps 软链自愈）
# 下游: 根 CMakeLists（cxx_add_module / cxx_add_module_consumer）
# 功能:
#   - cxx_add_module(name sources...): STATIC 模块库，PUBLIC FILE_SET CXX_MODULES
#     模块查找 = CMake 原生 clang-scan-deps 扫描 + FILE_SET（不再手工指定 .pcm/.o）
#   - cxx_add_module_consumer(name sources...): import 消费方可执行文件
#   - import std 由 CMake 4.4+ 原生处理（Clang ≥17 + libc++），不再手工编译 std.cppm
#     （更稳定：std 模块编译参数由 CMake 与工具链自动对齐）
# =============================================================================

include_guard(GLOBAL)

# -----------------------------------------------------------------------------
# cxx_enable_std_module()
# 作用：把 clang 自带的 std.cppm 编译成显式模块目标 cxx_std_module（惰性、幂等）。
#   背景：CMake 原生 import std 仅覆盖普通 TU；命名模块接口单元内 import std
#         需要显式 -fmodule-file=std=<pcm>。brew llvm 的 std.cppm 位于
#         <llvm>/share/libc++/v1/，其 .inc 依赖同目录 → 私有 include 目录。
#   更稳定：std 模块来自编译器自带资源，版本与工具链严格一致。
# -----------------------------------------------------------------------------
function(cxx_enable_std_module)
  get_property(_done GLOBAL PROPERTY CXX_STD_MODULE_TARGET_CREATED)
  if(_done)
    return()
  endif()

  set(_std_src "")
  set(_std_dir "")
  # 1) 通过 libc++.modules.json 反查 std.cppm（brew / apt LLVM 布局通用）
  execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=libc++.modules.json
    OUTPUT_VARIABLE _std_json OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(_std_json AND EXISTS "${_std_json}")
    file(READ "${_std_json}" _std_json_text)
    string(REGEX MATCH "\"source-path\"[ \t]*:[ \t]*\"([^\"]+)\"" _m "${_std_json_text}")
    if(CMAKE_MATCH_1)
      get_filename_component(_json_dir "${_std_json}" DIRECTORY)
      get_filename_component(_cand "${_json_dir}/${CMAKE_MATCH_1}" ABSOLUTE)
      if(EXISTS "${_cand}")
        set(_std_src "${_cand}")
        get_filename_component(_std_dir "${_cand}" DIRECTORY)
      endif()
    endif()
  endif()
  # 2) 常见布局兜底（brew opt / Toolchains / apt /usr/lib/llvm-*）
  if(NOT _std_src AND CXX_LLVM_ROOT)
    foreach(_root IN ITEMS "${CXX_LLVM_ROOT}" "/usr/lib/llvm-23" "/usr/local")
      if(NOT _std_src AND EXISTS "${_root}/share/libc++/v1/std.cppm")
        set(_std_src "${_root}/share/libc++/v1/std.cppm")
        set(_std_dir "${_root}/share/libc++/v1")
      endif()
    endforeach()
  endif()
  if(NOT _std_src AND CXX_LLVM_ROOT)
    file(GLOB _tc "${CXX_LLVM_ROOT}/Toolchains/*/usr/share/libc++/v1/std.cppm")
    if(_tc)
      list(GET _tc 0 _std_src)
      get_filename_component(_std_dir "${_std_src}" DIRECTORY)
    endif()
  endif()

  if(NOT _std_src)
    cxx_msg(warning "std.cppm not found — named modules must avoid import std（brew install llvm 后应存在）")
    set_property(GLOBAL PROPERTY CXX_STD_MODULE_TARGET_CREATED TRUE)
    return()
  endif()

  set(_mod_dir "${CMAKE_BINARY_DIR}/.cxx_std_module")
  file(COPY "${_std_src}" DESTINATION "${_mod_dir}")

  add_library(cxx_std_module STATIC)
  target_sources(cxx_std_module PUBLIC
    FILE_SET CXX_MODULES BASE_DIRS "${_mod_dir}" FILES "${_mod_dir}/std.cppm")
  target_compile_features(cxx_std_module PUBLIC cxx_std_23)
  target_include_directories(cxx_std_module PRIVATE "${_std_dir}")
  target_compile_options(cxx_std_module PRIVATE -w)  # 标准库模块内部告警一律静默
  # 与消费者保持 ABI 一致（00_core_profile 在 Debug 下启用 libc++ 硬化）
  target_compile_definitions(cxx_std_module PRIVATE
    $<$<CONFIG:Debug>:_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG>)
  set_target_properties(cxx_std_module PROPERTIES
    EXPORT_NAME cxx::std_module)
  set_property(GLOBAL PROPERTY CXX_STD_MODULE_TARGET_CREATED TRUE)
  cxx_msg(blue "std module target: cxx_std_module（${_std_src}）")
endfunction()

# -----------------------------------------------------------------------------
# cxx_add_module(name sources...)
# 作用：创建 STATIC 模块库；模块接口文件经 FILE_SET CXX_MODULES 扫描导出。
# -----------------------------------------------------------------------------
function(cxx_add_module name)
  cxx_enable_std_module()
  add_library(${name} STATIC)
  target_sources(${name} PUBLIC FILE_SET CXX_MODULES FILES ${ARGN})
  target_compile_features(${name} PUBLIC cxx_std_23)
  # 标记模块目标：00_core_lint 据此跳过编译期 clang-tidy 挂载
  #（clang-tidy 无法解析 CMake 的 @modmap 模块映射，模块 TU 走 tidy-check 或不做）
  set_target_properties(${name} PROPERTIES CXX_MODULE_TARGET TRUE)
  if(TARGET cxx_std_module)
    target_link_libraries(${name} PUBLIC cxx_std_module)
  endif()
endfunction()

# -----------------------------------------------------------------------------
# cxx_add_module_consumer(name sources...)
# 作用：创建 import 消费方可执行文件；链接模块库后 CMake 自动生成模块依赖。
# -----------------------------------------------------------------------------
function(cxx_add_module_consumer name)
  cxx_enable_std_module()
  add_executable(${name} ${ARGN})
  target_compile_features(${name} PRIVATE cxx_std_23)
  set_target_properties(${name} PROPERTIES CXX_MODULE_TARGET TRUE)
  if(TARGET cxx_std_module)
    target_link_libraries(${name} PRIVATE cxx_std_module)
  endif()
endfunction()
