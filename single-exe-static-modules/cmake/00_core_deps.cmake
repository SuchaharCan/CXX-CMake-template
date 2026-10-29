# =============================================================================
# 00_core_deps.cmake — 依赖检测（工具链 + 第三方库，含最小版本门禁）
# 分层: core（必选）
# 独立: 否
# 上游: 00_core_compiler.cmake（CXX_LLVM_* / CXX_USING_* / CMAKE_PREFIX_PATH）
#       00_core_color.cmake（cxx_msg，依赖检测统一用 blue 色）
#       00_core_log.cmake（cxx_flog）
# 下游: 10_core_setup.cmake（调用 cxx_deps_detect / cxx_link_bqlog）
#       根 CMakeLists / 90_opt_*.cmake（消费 CXX_*_FOUND 等结果）
# 功能:
#   - 检测 cmake / ninja / llvm(clang) / bqlog，校验最小版本
#   - bqlog 三模式接入: header（默认，BQ_LATE_BINDING 免链接）/ library / module
#   - 本模板不依赖 spdlog；所有日志统一使用 bqlog 的 C++ 模块
#   - cxx_deps_report() 统一打印检测结果（蓝色=依赖检测）
# =============================================================================

include_guard(GLOBAL)

# ---- 最小版本（可用 -D 覆盖）----
set(CXX_MIN_CMAKE "4.4.4" CACHE STRING "Minimum CMake version")
set(CXX_MIN_NINJA "1.11" CACHE STRING "Minimum Ninja version")
set(CXX_MIN_LLVM "23.1" CACHE STRING "Minimum LLVM/clang version（import std 需 Clang ≥17，模板锁定 23.1）")
set(CXX_MIN_GCC "14" CACHE STRING "Minimum GCC major version")
set(CXX_MIN_BQLOG "2.7" CACHE STRING "Minimum bqlog version")
set(CXX_MIN_JUST "1.20" CACHE STRING "Minimum just (justfile) version")

# bqlog 接入模式: header | library | module（默认 header：头文件 + 免链接 late binding）
set(CXX_BQLOG_MODE "module" CACHE STRING "bqlog integration: header|library|module")
set_property(CACHE CXX_BQLOG_MODE PROPERTY STRINGS header library module)
if(NOT CXX_BQLOG_MODE STREQUAL "module")
  message(FATAL_ERROR "single-exe-static-modules requires CXX_BQLOG_MODE=module")
endif()
set(CXX_BQLOG_LINK "static" CACHE STRING "bqlog library link kind (library/module 模式)")
set_property(CACHE CXX_BQLOG_LINK PROPERTY STRINGS static shared)

set(CXX_BQLOG_FOUND OFF CACHE INTERNAL "bqlog discovered" FORCE)

# -----------------------------------------------------------------------------
# cxx_version_at_least(actual min result_var) — x.y.z 语义比较
# -----------------------------------------------------------------------------
function(cxx_version_at_least actual min result_var)
  set(_res TRUE)
  string(REPLACE "." ";" _a_parts "${actual}")
  string(REPLACE "." ";" _m_parts "${min}")
  list(LENGTH _a_parts _an)
  set(_idx 0)
  foreach(_m IN LISTS _m_parts)
    set(_a 0)
    if(_idx LESS _an)
      list(GET _a_parts ${_idx} _a)
    endif()
    if(NOT _a MATCHES "^[0-9]+$")
      set(_a 0)
    endif()
    if(_a LESS _m)
      set(_res FALSE)
      break()
    elseif(_a GREATER _m)
      break()
    endif()
    math(EXPR _idx "${_idx} + 1")
  endforeach()
  set(${result_var} "${_res}" PARENT_SCOPE)
endfunction()

# -----------------------------------------------------------------------------
# cxx_deps_detect() — 检测并校验所有必备依赖（不含可选的 gtest/benchmark 等）
# -----------------------------------------------------------------------------
function(cxx_deps_detect)
  cxx_sep("依赖检测")

  # cmake
  cxx_version_at_least("${CMAKE_VERSION}" "${CXX_MIN_CMAKE}" _ok)
  if(_ok)
    cxx_msg(grass "cmake ${CMAKE_VERSION} ≥ ${CXX_MIN_CMAKE}")
  else()
    cxx_msg(error "cmake ${CMAKE_VERSION} < ${CXX_MIN_CMAKE} — 升级 brew install cmake")
  endif()

  # just（必选：justfile 构建自动化）
  find_program(CXX_JUST_PROGRAM just)
  if(CXX_JUST_PROGRAM)
    execute_process(COMMAND "${CXX_JUST_PROGRAM}" --version
      OUTPUT_VARIABLE _just_ver OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    string(REGEX REPLACE "^just " "" _just_ver "${_just_ver}")
    cxx_version_at_least("${_just_ver}" "${CXX_MIN_JUST}" _ok)
    if(_ok)
      cxx_msg(grass "just ${_just_ver} ≥ ${CXX_MIN_JUST}")
    else()
      cxx_msg(error "just ${_just_ver} < ${CXX_MIN_JUST} — brew upgrade just")
    endif()
  else()
    cxx_msg(error "just not found — brew install just")
    message(FATAL_ERROR "just is required (brew install just)")
  endif()

  # ninja
  find_program(_ninja ninja)
  if(_ninja)
    execute_process(COMMAND "${_ninja}" --version
      OUTPUT_VARIABLE _ninja_ver OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    cxx_version_at_least("${_ninja_ver}" "${CXX_MIN_NINJA}" _ok)
    if(_ok)
      cxx_msg(grass "ninja ${_ninja_ver} ≥ ${CXX_MIN_NINJA}")
    else()
      cxx_msg(warning "ninja ${_ninja_ver} < ${CXX_MIN_NINJA} — brew upgrade ninja")
    endif()
  else()
    cxx_msg(error "ninja not found — brew install ninja")
  endif()

  # clang / llvm
  if(CXX_USING_CLANG)
    cxx_version_at_least("${CMAKE_CXX_COMPILER_VERSION}" "${CXX_MIN_LLVM}" _ok)
    if(_ok)
      cxx_msg(grass "clang ${CMAKE_CXX_COMPILER_VERSION} ≥ ${CXX_MIN_LLVM}")
    else()
      cxx_msg(error "clang ${CMAKE_CXX_COMPILER_VERSION} < ${CXX_MIN_LLVM} — brew upgrade llvm")
    endif()
  endif()

  # gcc（当前工具链为 gcc 时）
  if(CXX_USING_GCC)
    cxx_version_at_least("${CMAKE_CXX_COMPILER_VERSION}" "${CXX_MIN_GCC}" _ok)
    if(_ok)
      cxx_msg(grass "g++ ${CMAKE_CXX_COMPILER_VERSION} ≥ ${CXX_MIN_GCC}")
    else()
      cxx_msg(error "g++ ${CMAKE_CXX_COMPILER_VERSION} < ${CXX_MIN_GCC} — brew upgrade gcc")
    endif()
  endif()

  # bqlog（本模板唯一日志库，必需；必须使用 module 模式）
  cxx_find_bqlog()
  if(CXX_BQLOG_FOUND)
    cxx_msg(grass "bqlog ${CXX_BQLOG_VERSION} ≥ ${CXX_MIN_BQLOG} (mode=${CXX_BQLOG_MODE})")
  else()
    cxx_msg(error "bqlog not found — brew install bqlog")
    message(FATAL_ERROR "bqlog is required (brew install bqlog)")
  endif()
endfunction()

# -----------------------------------------------------------------------------
# cxx_find_bqlog() — 头文件 + 库定位（pkg-config 优先，find_path/find_library 兜底）
# -----------------------------------------------------------------------------
function(cxx_find_bqlog)
  if(CXX_BQLOG_FOUND AND (CXX_BQLOG_LIBRARY OR TARGET PkgConfig::PC_bqlog))
    return()
  endif()

  # bqlog 安装位置：macOS/Linux Homebrew（opt/bqlog）、Linuxbrew 备选、显式 ROOT
  set(_bq_roots "")
  foreach(_root IN ITEMS
      "${CXX_HOMEBREW_PREFIX}/opt/bqlog"
      "/home/linuxbrew/.linuxbrew/opt/bqlog"
      "$ENV{HOME}/.linuxbrew/opt/bqlog"
      "$ENV{CXX_BQLOG_ROOT}"
      "$ENV{BQLOG_ROOT}")
    if(NOT _root STREQUAL "" AND EXISTS "${_root}")
      list(APPEND _bq_roots "${_root}")
    endif()
  endforeach()
  foreach(_root IN LISTS _bq_roots)
    set(ENV{PKG_CONFIG_PATH}
      "${_root}/lib/pkgconfig:${_root}/lib64/pkgconfig:$ENV{PKG_CONFIG_PATH}")
  endforeach()

  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(PC_bqlog QUIET IMPORTED_TARGET bqlog)
  endif()

  set(_inc "")
  set(_lib "")
  set(_ver "")
  if(PC_bqlog_FOUND)
    set(_inc "${PC_bqlog_INCLUDE_DIRS}")
    set(_ver "${PC_bqlog_VERSION}")
    if(CXX_BQLOG_LINK STREQUAL "static")
      find_library(_lib NAMES BqLog
        PATHS "${PC_bqlog_LIBRARY_DIRS}" NO_DEFAULT_PATH)
    else()
      find_library(_lib NAMES BqLog
        PATHS "${PC_bqlog_LIBRARY_DIRS}" NO_DEFAULT_PATH)
    endif()
    if(NOT _lib)
      find_library(_lib NAMES BqLog
        HINTS ${_bq_roots} PATH_SUFFIXES lib lib64)
    endif()
    if(NOT _inc)
      find_path(_inc bq_log/bq_log.h HINTS ${_bq_roots} PATH_SUFFIXES include)
    endif()
  else()
    find_path(_inc bq_log/bq_log.h
      HINTS ${_bq_roots} PATH_SUFFIXES include)
    find_library(_lib NAMES BqLog
      HINTS ${_bq_roots} PATH_SUFFIXES lib lib64)
  endif()

  if(_inc AND EXISTS "${_inc}/bq_log/bq_log.h")
    set(CXX_BQLOG_INCLUDE_DIR "${_inc}" CACHE PATH "bqlog include dir" FORCE)
  else()
    set(CXX_BQLOG_INCLUDE_DIR "" CACHE PATH "bqlog include dir" FORCE)
  endif()
  set(CXX_BQLOG_LIBRARY "${_lib}" CACHE FILEPATH "bqlog library" FORCE)
  if(_ver STREQUAL "")
    set(_ver "${CXX_MIN_BQLOG}")
  endif()
  set(CXX_BQLOG_VERSION "${_ver}" CACHE STRING "bqlog version" FORCE)

  if(CXX_BQLOG_INCLUDE_DIR AND
     (CXX_BQLOG_LIBRARY OR CXX_BQLOG_MODE STREQUAL "header"
      OR TARGET PkgConfig::PC_bqlog))
    set(CXX_BQLOG_FOUND ON CACHE INTERNAL "bqlog discovered" FORCE)
  else()
    set(CXX_BQLOG_FOUND OFF CACHE INTERNAL "bqlog discovered" FORCE)
  endif()
endfunction()

# -----------------------------------------------------------------------------
# cxx_bqlog_dynamic_name() — 平台化动态库名（late binding 用）
# -----------------------------------------------------------------------------
function(cxx_bqlog_dynamic_name out_var)
  if(APPLE)
    set(_so "libBqLog.dylib")
  else()
    set(_so "libBqLog.so")
  endif()
  set(${out_var} "${_so}" PARENT_SCOPE)
endfunction()

# -----------------------------------------------------------------------------
# cxx_bqlog_module_target() — 惰性创建 bq.log.cppm 模块库（module 模式）
# -----------------------------------------------------------------------------
function(cxx_bqlog_module_target)
  get_property(_done GLOBAL PROPERTY CXX_BQLOG_MODULE_TARGET_CREATED)
  if(_done)
    return()
  endif()
  set(_cppm "${CXX_BQLOG_INCLUDE_DIR}/bq_log/bq.log.cppm")
  if(NOT EXISTS "${_cppm}")
    cxx_msg(warning "bqlog module mode: bq.log.cppm not found; fallback to header")
    set(CXX_BQLOG_MODE "header" CACHE STRING "bqlog integration" FORCE)
    return()
  endif()
  # file set 要求源文件位于 target 基目录内 → 拷贝进 build 目录
  set(_mod_dir "${CMAKE_BINARY_DIR}/.cxx_bqlog_module")
  file(COPY "${_cppm}" DESTINATION "${_mod_dir}")
  set(_mod_src "${_mod_dir}/bq.log.cppm")
  add_library(cxx_bqlog_module STATIC)
  target_sources(cxx_bqlog_module PUBLIC
    FILE_SET CXX_MODULES BASE_DIRS "${_mod_dir}" FILES "${_mod_src}")
  target_compile_features(cxx_bqlog_module PUBLIC cxx_std_20)
  target_include_directories(cxx_bqlog_module PUBLIC "${CXX_BQLOG_INCLUDE_DIR}")
  if(TARGET PkgConfig::PC_bqlog)
    target_link_libraries(cxx_bqlog_module PUBLIC PkgConfig::PC_bqlog)
  elseif(CXX_BQLOG_LIBRARY)
    target_link_libraries(cxx_bqlog_module PUBLIC "${CXX_BQLOG_LIBRARY}")
  endif()
  set_target_properties(cxx_bqlog_module PROPERTIES
    EXPORT_NAME cxx::bqlog_module)
  set_property(GLOBAL PROPERTY CXX_BQLOG_MODULE_TARGET_CREATED TRUE)
  cxx_msg(blue "bqlog module target: cxx_bqlog_module（源码 import bq.log;）")
endfunction()

# -----------------------------------------------------------------------------
# cxx_link_bqlog(target)
# 作用：按 CXX_BQLOG_MODE 接入 bqlog。
#   header  — 头文件 + BQ_LATE_BINDING 免链接（默认）
#   library — 链接 libBqLog（CXX_BQLOG_LINK=static|shared）
#   module  — 链接 cxx_bqlog_module，源码 import bq.log;
# -----------------------------------------------------------------------------
function(cxx_link_bqlog target)
  if(NOT CXX_BQLOG_FOUND)
    return()
  endif()
  if(NOT TARGET ${target})
    message(FATAL_ERROR "cxx_link_bqlog: target '${target}' does not exist")
  endif()

  target_include_directories(${target} PRIVATE "${CXX_BQLOG_INCLUDE_DIR}")

  if(CXX_BQLOG_MODE STREQUAL "module")
    cxx_bqlog_module_target()
    if(TARGET cxx_bqlog_module)
      target_link_libraries(${target} PUBLIC cxx_bqlog_module)
      cxx_msg(blue "bqlog(module) → ${target}（import bq.log;）")
      return()
    endif()
  endif()

  if(CXX_BQLOG_MODE STREQUAL "library")
    if(TARGET PkgConfig::PC_bqlog)
      target_link_libraries(${target} PRIVATE PkgConfig::PC_bqlog)
      cxx_msg(blue "bqlog(library/pkg-config) → ${target}")
      return()
    elseif(CXX_BQLOG_LIBRARY)
      target_link_libraries(${target} PRIVATE "${CXX_BQLOG_LIBRARY}")
      if(CXX_BQLOG_LINK STREQUAL "shared")
        get_filename_component(_libdir "${CXX_BQLOG_LIBRARY}" DIRECTORY)
        if(_libdir)
          target_link_directories(${target} PRIVATE "${_libdir}")
        endif()
      endif()
      cxx_msg(blue "bqlog(library/${CXX_BQLOG_LINK}) → ${target}")
      return()
    endif()
    cxx_msg(warning "bqlog library mode but libBqLog not found; fallback to header")
  endif()

  # header 模式（默认）: BQ_LATE_BINDING 免链接
  cxx_bqlog_dynamic_name(_dyn)
  get_filename_component(_libdir "${CXX_BQLOG_LIBRARY}" DIRECTORY)
  if(_libdir AND EXISTS "${_libdir}/${_dyn}")
    set(_dyn_path "${_libdir}/${_dyn}")
  else()
    set(_dyn_path "${_dyn}")
  endif()
  target_compile_definitions(${target} PRIVATE
    "BQ_LATE_BINDING=1"
    "BQ_API_DYNAMIC_LIBRARY_NAME=\"${_dyn_path}\""
    "BQ_API_DYNAMIC_LIBRARY_FALLBACK_NAME=\"${_dyn}\"")
  cxx_msg(blue "bqlog(header, late-binding) → ${target}")
endfunction()

# -----------------------------------------------------------------------------
# cxx_deps_report() — 打印检测结果（统一 blue=依赖检测色）
# -----------------------------------------------------------------------------
function(cxx_deps_report)
  execute_process(COMMAND "${CMAKE_CXX_COMPILER}" --version
    OUTPUT_VARIABLE _ver OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  string(REGEX REPLACE "\n.*" "" _ver_one "${_ver}")

  cxx_msg(blue "CXX_PREFERRED_COMPILER=${CXX_PREFERRED_COMPILER}")
  if(CXX_JUST_PROGRAM)
    cxx_msg(blue "just: ${CXX_JUST_PROGRAM}（justfile 自动化）")
  endif()
  cxx_msg(blue "Active CXX: ${_ver_one}")
  cxx_msg(cyan "CMAKE_CXX_COMPILER: ${CMAKE_CXX_COMPILER}")
  if(CXX_HOMEBREW_PREFIX)
    cxx_msg(blue "Homebrew prefix: ${CXX_HOMEBREW_PREFIX}")
  endif()
  if(CXX_LLVM_ROOT)
    cxx_msg(blue "LLVM (clang++): ${CXX_LLVM_ROOT}")
  endif()
  if(CXX_GXX_PROGRAM)
    cxx_msg(blue "GCC (g++): ${CXX_GXX_PROGRAM}")
  endif()
  if(CXX_BQLOG_FOUND)
    cxx_msg(blue "bqlog: ${CXX_BQLOG_VERSION} (${CXX_BQLOG_MODE})")
  else()
    cxx_msg(blue "bqlog: missing")
  endif()
endfunction()
