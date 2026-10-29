# =============================================================================
# single-exe-static-modules — 多独立静态模块 + 单 main 的 macOS 模板
#
# 结构：
#   InterfaceLog  STATIC 模块库  （bqlog 模块日志 + 1000 行环形接口记录）
#   ProjectSync   STATIC 模块库  （项目/Stack 同步）
#   TaskManager   STATIC 模块库  （任务创建 / ID 生成）
#   Stacks_Tools  单 main，只做单层 import 与 CLI 路由
#
# 约束：
#   Ninja + CMake ≥4.4.4 + Clang++ ≥23.1 + C++26 + libc++
#   std 模块由本机 LLVM 的 std.cppm 自编译
# =============================================================================

cmake_minimum_required(VERSION 4.4.4)

include(cmake/00_core_compiler.cmake)

project(
  StaticModulesExe
  VERSION 0.1.0
  DESCRIPTION "C++26 多独立静态模块 + 单 main（Ninja / Clang / libc++）"
  LANGUAGES CXX
)

include(cmake/00_core_color.cmake)
include(cmake/00_core_log.cmake)
include(cmake/00_core_deps.cmake)
include(cmake/00_core_buildlog.cmake)
include(cmake/00_core_profile.cmake)
include(cmake/00_core_modules.cmake)
include(cmake/00_core_lint.cmake)
include(cmake/10_core_setup.cmake)

cxx_setup_bootstrap()
cxx_phase_toolchain()

set(STR_TEST "single-exe-static-modules")
cxx_set_lint_sources(
  modules/interface_log/include/InterfaceLog.cppm
  modules/project_sync/include/ProjectSync.cppm
  modules/task_manager/include/TaskManager.cppm
  src/main.cpp
  tests/static_modules_tests.cxx
)
# 本模板是纯模块 TU；clang-tidy 不支持 CMake @modmap，tidy-check 不扫描模块
set(CXX_TIDY_PLAIN_SOURCES "" CACHE STRING "Plain TU sources for tidy-check" FORCE)
cxx_phase_variables()
cxx_sep("构建独立静态模块")

# ---- 独立静态模块 ----
cxx_add_module(interface_log modules/interface_log/include/InterfaceLog.cppm)
target_include_directories(interface_log PUBLIC
  "${PROJECT_SOURCE_DIR}/modules/interface_log/include")
cxx_apply_static_module_defaults(interface_log)
cxx_link_bqlog(interface_log)   # module 模式：import bq.log
cxx_msg(grass "STATIC module: interface_log（bqlog module + 1000 行环形记录）")

cxx_add_module(project_sync modules/project_sync/include/ProjectSync.cppm)
target_include_directories(project_sync PUBLIC
  "${PROJECT_SOURCE_DIR}/modules/project_sync/include")
target_include_directories(project_sync PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(project_sync PUBLIC interface_log)
cxx_apply_static_module_defaults(project_sync)
cxx_msg(grass "STATIC module: project_sync")

cxx_add_module(task_manager modules/task_manager/include/TaskManager.cppm)
target_include_directories(task_manager PUBLIC
  "${PROJECT_SOURCE_DIR}/modules/task_manager/include")
target_include_directories(task_manager PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(task_manager PUBLIC interface_log)
cxx_apply_static_module_defaults(task_manager)
cxx_msg(grass "STATIC module: task_manager")

# ---- 单 main：只做单层 import / CLI 路由 ----
cxx_add_module_consumer(Stacks_Tools src/main.cpp)
target_include_directories(Stacks_Tools PRIVATE
  "${PROJECT_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}/modules/interface_log/include"
  "${PROJECT_SOURCE_DIR}/modules/project_sync/include"
  "${PROJECT_SOURCE_DIR}/modules/task_manager/include")
target_link_libraries(Stacks_Tools PRIVATE
  interface_log project_sync task_manager)
cxx_apply_static_module_defaults(Stacks_Tools)
cxx_msg(grass "single main: Stacks_Tools")

# ---- 单元测试（GTest：边界 + 并行；变异测试见 just mutation-test）----
if(CXX_GTEST_FOUND)
  cxx_add_module_consumer(static_modules_tests tests/static_modules_tests.cxx)
  target_include_directories(static_modules_tests PRIVATE
    "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/modules/interface_log/include"
    "${PROJECT_SOURCE_DIR}/modules/project_sync/include"
    "${PROJECT_SOURCE_DIR}/modules/task_manager/include")
  target_link_libraries(static_modules_tests PRIVATE
    interface_log project_sync task_manager)
  cxx_apply_static_module_defaults(static_modules_tests)
  cxx_add_gtest(static_modules_tests)
  cxx_msg(grass "GTest: static_modules_tests")
endif()

cxx_phase_finish()
