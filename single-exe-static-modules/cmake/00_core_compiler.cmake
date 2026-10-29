# =============================================================================
# 00_core_compiler.cmake — 编译器 / 工具链发现（clang++ / g++；模块模板强制 Ninja）
# 分层: core（必选，必须在 project() 之前 include）
# 独立: 是 — 不依赖任何其他模板模块，仅用 CMake 内置命令
# 下游: 00_core_deps.cmake（消费 CXX_LLVM_* / CXX_USING_*）
#       00_core_modules.cmake（模块 API 依赖编译器；import std 由 CMake 4.4+ 原生支持）
#       00_core_lint.cmake（消费 CXX_LLVM_BIN 定位 clang-tidy/format）
#       10_core_setup.cmake（编排 cxx_sanitize_linker_flags / cxx_apply_llvm_runtime）
# 功能:
#   - 发现 Homebrew / LLVM / GCC，写入 CMAKE_CXX_COMPILER / CMAKE_C_COMPILER
#   - brew 升级后 Cellar 旧路径失效 → opt 软链自愈（clang-scan-deps/llvm-ar/ranlib）
#   - Clang→libc++ / GCC→libstdc++ 的初始 flags 与 rpath
#   - Apple SDK sysroot、AR/RANLIB、链接 flags 去重
# 用法: cmake .. -DCXX_PREFERRED_COMPILER=g++
# =============================================================================

include_guard(GLOBAL)

# ---- 模块模板强制 Ninja（import std / CXX_MODULES 依赖 Ninja 生成器）----
if(NOT CMAKE_GENERATOR MATCHES "Ninja")
  message(FATAL_ERROR
    "single-exe-modules requires the Ninja generator: cmake -G Ninja ..")
endif()

# ---- 用户可选：auto | clang++ | g++（模块模板 auto=clang++）----
set(CXX_PREFERRED_COMPILER "auto" CACHE STRING
    "C++ compiler: auto | clang++ | g++")
set_property(CACHE CXX_PREFERRED_COMPILER PROPERTY STRINGS auto clang++ g++)
string(TOLOWER "${CXX_PREFERRED_COMPILER}" _cxx_pref)
if(_cxx_pref STREQUAL "gcc" OR _cxx_pref STREQUAL "g++")
  set(CXX_PREFERRED_COMPILER "g++" CACHE STRING "" FORCE)
elseif(_cxx_pref STREQUAL "clang" OR _cxx_pref STREQUAL "clang++")
  set(CXX_PREFERRED_COMPILER "clang++" CACHE STRING "" FORCE)
elseif(_cxx_pref STREQUAL "auto")
  set(CXX_PREFERRED_COMPILER "auto" CACHE STRING "" FORCE)
else()
  message(FATAL_ERROR
    "CXX_PREFERRED_COMPILER must be auto, clang++ or g++ (got '${CXX_PREFERRED_COMPILER}')")
endif()

# -----------------------------------------------------------------------------
# _cxx_detect_homebrew_prefix(out_var)
# 作用：探测 Homebrew 前缀（ENV / brew --prefix / 常见路径）。
# -----------------------------------------------------------------------------
function(_cxx_detect_homebrew_prefix out_var)
  if(DEFINED ENV{HOMEBREW_PREFIX} AND EXISTS "$ENV{HOMEBREW_PREFIX}")
    set(${out_var} "$ENV{HOMEBREW_PREFIX}" PARENT_SCOPE)
    return()
  endif()
  find_program(_brew brew)
  if(_brew)
    execute_process(COMMAND "${_brew}" --prefix
      OUTPUT_VARIABLE _prefix OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_prefix AND EXISTS "${_prefix}")
      set(${out_var} "${_prefix}" PARENT_SCOPE)
      return()
    endif()
  endif()
  foreach(_cand IN ITEMS "/opt/homebrew" "/usr/local" "/home/linuxbrew/.linuxbrew")
    if(EXISTS "${_cand}")
      set(${out_var} "${_cand}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  if(DEFINED ENV{HOME} AND EXISTS "$ENV{HOME}/.linuxbrew")
    set(${out_var} "$ENV{HOME}/.linuxbrew" PARENT_SCOPE)
    return()
  endif()
  set(${out_var} "" PARENT_SCOPE)
endfunction()

# -----------------------------------------------------------------------------
# _cxx_detect_llvm_root(out_var)
# 作用：探测 Homebrew llvm 前缀（需含 bin/clang++）。
# -----------------------------------------------------------------------------
function(_cxx_detect_llvm_root out_var)
  if(CXX_HOMEBREW_PREFIX)
    find_program(_brew brew PATHS "${CXX_HOMEBREW_PREFIX}/bin" NO_DEFAULT_PATH)
  else()
    find_program(_brew brew)
  endif()
  if(_brew)
    execute_process(COMMAND "${_brew}" --prefix llvm
      OUTPUT_VARIABLE _llvm OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_llvm AND EXISTS "${_llvm}/bin/clang++")
      set(${out_var} "${_llvm}" PARENT_SCOPE)
      return()
    endif()
  endif()
  if(CXX_HOMEBREW_PREFIX AND EXISTS "${CXX_HOMEBREW_PREFIX}/opt/llvm/bin/clang++")
    set(${out_var} "${CXX_HOMEBREW_PREFIX}/opt/llvm" PARENT_SCOPE)
    return()
  endif()
  find_program(_cxx_any clang++)
  if(_cxx_any)
    set(_cxx_any_path "${_cxx_any}")
    # Linux：symlink 可能在 /usr/local/bin，解析到 /usr/lib/llvm-23 以获得正确 -L 路径；
    # macOS 保持 Homebrew opt 软链（解析到 Cellar 会在 brew 升级后失效）
    get_filename_component(_cxx_any_real "${_cxx_any}" REALPATH)
    if(NOT _cxx_any_real MATCHES "/Cellar/")
      set(_cxx_any_path "${_cxx_any_real}")
    endif()
    get_filename_component(_bin "${_cxx_any_path}" DIRECTORY)
    get_filename_component(_root "${_bin}" DIRECTORY)
    if(EXISTS "${_root}/lib")
      set(${out_var} "${_root}" PARENT_SCOPE)
      return()
    endif()
  endif()
  set(${out_var} "" PARENT_SCOPE)
endfunction()

# -----------------------------------------------------------------------------
# _cxx_detect_gcc_root(out_var out_gxx)
# 作用：探测 Homebrew gcc 根目录与具体 g++-N 可执行文件。
# -----------------------------------------------------------------------------
function(_cxx_detect_gcc_root out_var out_gxx)
  set(_root "")
  set(_gxx "")
  if(CXX_HOMEBREW_PREFIX)
    find_program(_brew brew PATHS "${CXX_HOMEBREW_PREFIX}/bin" NO_DEFAULT_PATH)
  else()
    find_program(_brew brew)
  endif()
  if(_brew)
    execute_process(COMMAND "${_brew}" --prefix gcc
      OUTPUT_VARIABLE _gcc OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_gcc AND EXISTS "${_gcc}/bin")
      set(_root "${_gcc}")
    endif()
  endif()
  if(NOT _root AND CXX_HOMEBREW_PREFIX AND EXISTS "${CXX_HOMEBREW_PREFIX}/opt/gcc/bin")
    set(_root "${CXX_HOMEBREW_PREFIX}/opt/gcc")
  endif()
  if(_root)
    file(GLOB _gxx_cands "${_root}/bin/g++-*")
    list(SORT _gxx_cands ORDER DESCENDING)
    foreach(_c IN LISTS _gxx_cands)
      if(_c MATCHES "g\\+\\+-[0-9]+$")
        set(_gxx "${_c}")
        break()
      endif()
    endforeach()
    if(NOT _gxx AND EXISTS "${_root}/bin/g++")
      set(_gxx "${_root}/bin/g++")
    endif()
  endif()
  if(NOT _gxx)
    find_program(_gxx_path NAMES g++-16 g++-15 g++-14 g++-13 g++)
    if(_gxx_path)
      set(_gxx "${_gxx_path}")
      get_filename_component(_bin "${_gxx}" DIRECTORY)
      get_filename_component(_root "${_bin}" DIRECTORY)
    endif()
  endif()
  set(${out_var} "${_root}" PARENT_SCOPE)
  set(${out_gxx} "${_gxx}" PARENT_SCOPE)
endfunction()

_cxx_detect_homebrew_prefix(CXX_HOMEBREW_PREFIX)
set(CXX_HOMEBREW_PREFIX "${CXX_HOMEBREW_PREFIX}" CACHE PATH "Detected Homebrew prefix" FORCE)

_cxx_detect_llvm_root(CXX_LLVM_ROOT)
_cxx_detect_gcc_root(CXX_GCC_ROOT CXX_GXX_PROGRAM)
set(CXX_LLVM_ROOT "${CXX_LLVM_ROOT}" CACHE PATH "Detected LLVM root" FORCE)
set(CXX_GCC_ROOT "${CXX_GCC_ROOT}" CACHE PATH "Detected Homebrew GCC root" FORCE)
set(CXX_GXX_PROGRAM "${CXX_GXX_PROGRAM}" CACHE FILEPATH "Detected g++ program" FORCE)

if(CXX_LLVM_ROOT)
  set(_cxx_clangxx "${CXX_LLVM_ROOT}/bin/clang++")
  set(_cxx_clang   "${CXX_LLVM_ROOT}/bin/clang")
  set(CXX_LLVM_BIN "${CXX_LLVM_ROOT}/bin" CACHE PATH "" FORCE)
  set(CXX_LLVM_LIB "${CXX_LLVM_ROOT}/lib" CACHE PATH "" FORCE)
  if(EXISTS "${CXX_LLVM_ROOT}/lib/c++")
    set(CXX_LIBCXX_LIBDIR "${CXX_LLVM_ROOT}/lib/c++" CACHE PATH "" FORCE)
  else()
    set(CXX_LIBCXX_LIBDIR "${CXX_LLVM_ROOT}/lib" CACHE PATH "" FORCE)
  endif()
  if(EXISTS "${CXX_LLVM_ROOT}/lib/unwind")
    set(CXX_UNWIND_LIBDIR "${CXX_LLVM_ROOT}/lib/unwind" CACHE PATH "" FORCE)
  else()
    set(CXX_UNWIND_LIBDIR "${CXX_LLVM_ROOT}/lib" CACHE PATH "" FORCE)
  endif()
else()
  set(_cxx_clangxx "clang++")
  set(_cxx_clang "clang")
  set(CXX_LLVM_BIN "" CACHE PATH "" FORCE)
  set(CXX_LLVM_LIB "" CACHE PATH "" FORCE)
  set(CXX_LIBCXX_LIBDIR "" CACHE PATH "" FORCE)
  set(CXX_UNWIND_LIBDIR "" CACHE PATH "" FORCE)
endif()

set(LLVM_ROOT "${CXX_LLVM_ROOT}" CACHE PATH "Alias of CXX_LLVM_ROOT" FORCE)

# ---- LLVM 工具路径自愈（brew upgrade llvm 后旧 Cellar 路径失效）----
# CMake 在首次编译器探测时会把 clang-scan-deps / llvm-ar / llvm-ranlib
# 缓存为 Cellar 真实路径（如 /opt/homebrew/Cellar/llvm/23.1.3_1/...）。
# brew 升级删除旧版本目录后，这些缓存值导致 code=127（No such file）。
# 此处统一改指 opt 软链：升级后软链自动指向新版本，无需手动清缓存。
if(CXX_LLVM_BIN)
  foreach(_tool_name IN ITEMS clang-scan-deps llvm-ar llvm-ranlib)
    if(_tool_name STREQUAL "clang-scan-deps")
      set(_tool_var CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS)
    elseif(_tool_name STREQUAL "llvm-ar")
      set(_tool_var CMAKE_CXX_COMPILER_AR)
    else()
      set(_tool_var CMAKE_CXX_COMPILER_RANLIB)
    endif()
    set(_tool_target "${CXX_LLVM_BIN}/${_tool_name}")
    if(EXISTS "${_tool_target}")
      set(_tool_stale FALSE)
      if(NOT "${${_tool_var}}" STREQUAL "" AND NOT EXISTS "${${_tool_var}}")
        set(_tool_stale TRUE)
      endif()
      get_property(_tool_type CACHE ${_tool_var} PROPERTY TYPE)
      if("${${_tool_var}}" STREQUAL "" OR _tool_stale
          OR _tool_type STREQUAL "UNINITIALIZED")
        set(${_tool_var} "${_tool_target}"
            CACHE FILEPATH "${_tool_name} (Homebrew opt symlink)" FORCE)
      endif()
    endif()
  endforeach()
endif()

# ---- 平台默认 + 回退策略（模块模板固定 clang++ + libc++）----
# 模块的 import std 使用 libc++ 的 std.cppm；Ubuntu GCC 13 无 std 模块。
set(CXX_REQUIRE_STD_MODULE ON CACHE INTERNAL "modules require libc++ std module")
if(CXX_PREFERRED_COMPILER STREQUAL "auto")
  set(CXX_PREFERRED_COMPILER "clang++" CACHE STRING "" FORCE)
endif()
find_program(_CXX_CLANG_PROGRAM NAMES clang++)
set(_cxx_clang_available FALSE)
if(_CXX_CLANG_PROGRAM OR EXISTS "${_cxx_clangxx}")
  set(_cxx_clang_available TRUE)
endif()
if(NOT _cxx_clang_available)
  message(FATAL_ERROR
    "single-exe-modules requires clang++ (import std via libc++ std.cppm). "
    "Install llvm or set -DCXX_PREFERRED_COMPILER=clang++.")
endif()
if(CXX_PREFERRED_COMPILER STREQUAL "g++")
  message(FATAL_ERROR
    "single-exe-modules requires clang++ + libc++; GCC does not provide "
    "the libc++ std module used by this template.")
endif()

# 模块模板固定 libc++
set(CXX_STDLIB "libc++" CACHE STRING "C++ standard library (modules fixed to libc++)")
set_property(CACHE CXX_STDLIB PROPERTY STRINGS libc++)
if(CXX_STDLIB STREQUAL "auto" OR CXX_STDLIB STREQUAL "")
  set(CXX_STDLIB "libc++" CACHE STRING "" FORCE)
elseif(NOT CXX_STDLIB STREQUAL "libc++")
  message(FATAL_ERROR "single-exe-modules requires CXX_STDLIB=libc++")
endif()

# ---- 按 CXX_PREFERRED_COMPILER 注入编译器（可被 -DCMAKE_CXX_COMPILER= 覆盖）----
# 仅在与当前 CACHE 不一致时 FORCE，避免每次 cmake .. 都改写编译器导致无谓重检
set(CXX_USING_CLANG OFF)
set(CXX_USING_GCC OFF)

if(CMAKE_CXX_COMPILER AND NOT CMAKE_CXX_COMPILER STREQUAL "clang++"
   AND NOT CMAKE_CXX_COMPILER STREQUAL "g++"
   AND NOT CMAKE_CXX_COMPILER MATCHES ".*/clang\\+\\+$"
   AND NOT CMAKE_CXX_COMPILER MATCHES ".*/g\\+\\+(-[0-9]+)?$")
  # 用户已给绝对路径，尊重之
  if(CMAKE_CXX_COMPILER MATCHES "g\\+\\+")
    set(CXX_USING_GCC ON)
  else()
    set(CXX_USING_CLANG ON)
  endif()
elseif(CXX_PREFERRED_COMPILER STREQUAL "g++")
  if(NOT CXX_GXX_PROGRAM)
    message(FATAL_ERROR "CXX_PREFERRED_COMPILER=g++ but Homebrew g++ not found (brew install gcc)")
  endif()
  if(NOT CMAKE_CXX_COMPILER STREQUAL "${CXX_GXX_PROGRAM}")
    set(CMAKE_CXX_COMPILER "${CXX_GXX_PROGRAM}" CACHE FILEPATH "C++ compiler" FORCE)
    string(REPLACE "g++" "gcc" _cxx_gcc_c "${CXX_GXX_PROGRAM}")
    if(EXISTS "${_cxx_gcc_c}")
      set(CMAKE_C_COMPILER "${_cxx_gcc_c}" CACHE FILEPATH "C compiler" FORCE)
    endif()
  endif()
  set(CXX_USING_GCC ON)
else()
  set(_want_cxx "${_cxx_clangxx}")
  if(NOT EXISTS "${_want_cxx}")
    set(_want_cxx "clang++")
  endif()
  if(NOT CMAKE_CXX_COMPILER STREQUAL "${_want_cxx}")
    if(EXISTS "${_cxx_clangxx}")
      set(CMAKE_CXX_COMPILER "${_cxx_clangxx}" CACHE FILEPATH "C++ compiler" FORCE)
    else()
      set(CMAKE_CXX_COMPILER "clang++" CACHE STRING "C++ compiler" FORCE)
    endif()
    if(EXISTS "${_cxx_clang}")
      set(CMAKE_C_COMPILER "${_cxx_clang}" CACHE FILEPATH "C compiler" FORCE)
    else()
      set(CMAKE_C_COMPILER "clang" CACHE STRING "C compiler" FORCE)
    endif()
  endif()
  set(CXX_USING_CLANG ON)
endif()

set(CXX_USING_CLANG "${CXX_USING_CLANG}" CACHE BOOL "Active toolchain is Clang" FORCE)
set(CXX_USING_GCC "${CXX_USING_GCC}" CACHE BOOL "Active toolchain is GCC" FORCE)

# 模块模板固定 libc++（import std 使用 libc++ 的 std.cppm）
set(CXX_USING_LIBCXX ON CACHE BOOL "Using libc++" FORCE)
if(CXX_PREFERRED_COMPILER STREQUAL "clang++" AND CXX_STDLIB STREQUAL "libc++")
  set(CMAKE_CXX_FLAGS_INIT "-stdlib=libc++")
  if(CXX_LIBCXX_LIBDIR AND CXX_UNWIND_LIBDIR)
    set(CMAKE_EXE_LINKER_FLAGS_INIT
        "-stdlib=libc++ -L${CXX_LIBCXX_LIBDIR} -L${CXX_UNWIND_LIBDIR}")
  else()
    set(CMAKE_EXE_LINKER_FLAGS_INIT "-stdlib=libc++")
  endif()
else()
  set(CMAKE_CXX_FLAGS_INIT "")
  set(CMAKE_EXE_LINKER_FLAGS_INIT "")
endif()
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${CMAKE_EXE_LINKER_FLAGS_INIT}")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE AND CXX_USING_CLANG)
  add_compile_options("-DINFINITY=__builtin_huge_valf()" "-DNAN=__builtin_nanf(\"\")")
endif()

# -----------------------------------------------------------------------------
# cxx_sanitize_linker_flags()
# 作用：整理 Clang 链接 flags（unwind）并在 Apple 上去重警告。
# -----------------------------------------------------------------------------
function(cxx_sanitize_linker_flags)
  if(NOT CXX_USING_CLANG)
    return()
  endif()
  foreach(_var IN ITEMS CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS)
    if(NOT DEFINED ${_var})
      continue()
    endif()
    set(_flags "${${_var}}")
    string(REPLACE " -lunwind" "" _flags "${_flags}")
    string(REGEX REPLACE "^-lunwind " "" _flags "${_flags}")
    string(STRIP "${_flags}" _flags)
    if(APPLE AND (_flags MATCHES "lib/unwind" OR _flags MATCHES "libc\\+\\+"))
      set(_flags "${_flags} -lunwind")
    endif()
    set(${_var} "${_flags}" CACHE STRING "" FORCE)
  endforeach()
  get_property(_link_guard GLOBAL PROPERTY CXX_APPLE_NO_WARN_DUP_LIBS)
  if(APPLE AND NOT _link_guard)
    add_link_options("LINKER:-no_warn_duplicate_libraries")
    set_property(GLOBAL PROPERTY CXX_APPLE_NO_WARN_DUP_LIBS TRUE)
  endif()
endfunction()

# 供 find_package 检索 Homebrew keg（googletest/bqlog 等）
if(CXX_HOMEBREW_PREFIX)
  list(APPEND CMAKE_PREFIX_PATH
    "${CXX_HOMEBREW_PREFIX}"
    "${CXX_HOMEBREW_PREFIX}/opt/googletest"
    "${CXX_HOMEBREW_PREFIX}/opt/bqlog")
endif()
# Linuxbrew 备选路径（brew 不在 PATH 时也能发现 bqlog/googletest）
foreach(_cxx_brew_alt IN ITEMS "/home/linuxbrew/.linuxbrew")
  if(EXISTS "${_cxx_brew_alt}")
    list(APPEND CMAKE_PREFIX_PATH
      "${_cxx_brew_alt}"
      "${_cxx_brew_alt}/opt/googletest"
      "${_cxx_brew_alt}/opt/bqlog")
  endif()
endforeach()
if(DEFINED ENV{HOME} AND EXISTS "$ENV{HOME}/.linuxbrew")
  list(APPEND CMAKE_PREFIX_PATH
    "$ENV{HOME}/.linuxbrew"
    "$ENV{HOME}/.linuxbrew/opt/bqlog")
endif()
# CI 用 libc++ 构建的第三方依赖前缀（Linux modules 专用）
if(DEFINED ENV{CXX_LIBCXX_DEPS_PREFIX} AND EXISTS "$ENV{CXX_LIBCXX_DEPS_PREFIX}")
  list(PREPEND CMAKE_PREFIX_PATH "$ENV{CXX_LIBCXX_DEPS_PREFIX}")
endif()

if(APPLE AND CXX_USING_CLANG AND CXX_LLVM_BIN)
  find_program(_CXX_LLVM_AR llvm-ar PATHS "${CXX_LLVM_BIN}" NO_DEFAULT_PATH)
  if(_CXX_LLVM_AR)
    set(CMAKE_AR "${_CXX_LLVM_AR}" CACHE FILEPATH "Archive tool (llvm-ar)" FORCE)
    find_program(_CXX_LLVM_RANLIB llvm-ranlib PATHS "${CXX_LLVM_BIN}" NO_DEFAULT_PATH)
    if(_CXX_LLVM_RANLIB)
      set(CMAKE_RANLIB "${_CXX_LLVM_RANLIB}" CACHE FILEPATH "Ranlib tool (llvm-ranlib)" FORCE)
    endif()
  endif()
endif()

if(APPLE)
  # 避免链接器误搜 Linux 风格隐式路径（如 /lib），减少 Apple Silicon 上无意义告警
  set(CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "")

  execute_process(COMMAND xcrun --sdk macosx --show-sdk-path
    OUTPUT_VARIABLE _cxx_macos_sdk OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(_cxx_macos_sdk)
    set(CMAKE_OSX_SYSROOT "${_cxx_macos_sdk}" CACHE STRING "macOS SDK" FORCE)
  endif()
endif()

# -----------------------------------------------------------------------------
# cxx_apply_llvm_runtime(target)
# 作用：按 CXX_USING_CLANG/GCC 为 target 注入标准库与链接搜索路径。
#       历史命名保留；Clang→libc++，GCC→libstdc++ 相关 -L。
# -----------------------------------------------------------------------------
function(cxx_apply_llvm_runtime target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "cxx_apply_llvm_runtime: target '${target}' does not exist")
  endif()
  if(CXX_USING_LIBCXX)
    target_compile_options(${target} PRIVATE -stdlib=libc++)
    target_link_options(${target} PRIVATE -stdlib=libc++)
    if(CXX_LIBCXX_LIBDIR)
      target_link_directories(${target} PRIVATE "${CXX_LIBCXX_LIBDIR}")
    endif()
    if(CXX_UNWIND_LIBDIR)
      target_link_directories(${target} PRIVATE "${CXX_UNWIND_LIBDIR}")
    endif()
    if(CXX_LLVM_LIB AND NOT CMAKE_BUILD_WITH_INSTALL_RPATH)
      target_link_options(${target} PRIVATE "LINKER:-rpath,${CXX_LLVM_LIB}")
    endif()
  elseif(CXX_USING_GCC AND CXX_GCC_ROOT)
    if(EXISTS "${CXX_GCC_ROOT}/lib/gcc")
      target_link_directories(${target} PRIVATE "${CXX_GCC_ROOT}/lib")
    endif()
  endif()
endfunction()
