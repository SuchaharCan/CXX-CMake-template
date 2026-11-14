# =============================================================================
# 00_core_modules.cmake — C++26 静态模块 API（CMake 4.5 原生 import std）
# 分层: core（必选，模块模板专用）
# 独立: 否
# 上游: 00_core_compiler.cmake（编译器发现 / Ninja 强制 / scan-deps 软链自愈）
# 下游: 根 CMakeLists（cxx_add_module / cxx_add_module_consumer）
# 功能:
#   - cxx_add_module(name sources...): STATIC 模块库，PUBLIC FILE_SET CXX_MODULES
#   - cxx_add_module_consumer(name sources...): import 消费方可执行文件
#   - import std：CMake ≥4.5 原生支持（CMP0155 NEW → C++20+ 目标自动启用
#     CXX_SCAN_FOR_MODULES；Ninja + Clang + libc++）。std BMI 由 CMake 按当前
#     机器架构构建，不再手工定位/复制 std.cppm，也不再维护 cxx_std_module 目标。
# =============================================================================

include_guard(GLOBAL)

# -----------------------------------------------------------------------------
# cxx_add_module(name sources...)
# 作用：创建 STATIC 模块库；模块接口文件经 FILE_SET CXX_MODULES 扫描导出。
# -----------------------------------------------------------------------------
function(cxx_add_module name)
  add_library(${name} STATIC)
  target_sources(${name} PUBLIC FILE_SET CXX_MODULES FILES ${ARGN})
  # CMP0155 NEW 下，声明 C++20+ 编译特性即自动对该目标启用模块扫描；
  # 实际语言标准仍由 10_core_setup 的 CMAKE_CXX_STANDARD 26 控制。
  target_compile_features(${name} PUBLIC cxx_std_23)
  # 标记模块目标：00_core_lint 据此跳过编译期 clang-tidy 挂载。
  set_target_properties(${name} PROPERTIES CXX_MODULE_TARGET TRUE)
endfunction()

# -----------------------------------------------------------------------------
# cxx_add_module_consumer(name sources...)
# 作用：创建 import 消费方可执行文件；链接模块库后 CMake 自动生成模块依赖。
# -----------------------------------------------------------------------------
function(cxx_add_module_consumer name)
  add_executable(${name} ${ARGN})
  target_compile_features(${name} PRIVATE cxx_std_23)
  set_target_properties(${name} PROPERTIES CXX_MODULE_TARGET TRUE)
endfunction()
