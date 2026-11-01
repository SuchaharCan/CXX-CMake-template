# =============================================================================
# single-exe-static-modules — Omni-Stacks 实际应用（多独立静态模块 + 单 main）
#
# 结构：
#   InterfaceLog  STATIC 模块库  （bqlog 模块日志 + 1000 行环形接口记录）
#   ProjectSync   STATIC 模块库  （项目/Stack 同步）
#   TaskManager   STATIC 模块库  （任务 CRUD / 日期 / 完成 / 计时）
#   ReminderSync  STATIC 模块库  （Reminders / OmniFocus 同步）
#   WorkflowUI    STATIC 模块库  （Alfred Script Filter / Action 适配）
#   Stacks_Tools  单 main，只做单层 import 与 CLI 路由
#
# 约束：
#   Ninja + CMake ≥4.4.4 + Clang++ ≥23.1 + C++26 + libc++
#   std 模块由本机 LLVM 的 std.cppm 自编译
# =============================================================================

cmake_minimum_required(VERSION 4.4.4)

include(cmake/00_core_compiler.cmake)

project(
  OmniStacksStaticModules
  VERSION 2.0.0
  DESCRIPTION "Omni-Stacks：Stacks 2 Alfred task core（C++26 static modules / Ninja / Clang / libc++）"
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
  modules/reminder_sync/include/ReminderSync.cppm
  modules/workflow_ui/include/WorkflowUI.cppm
  src/main.cpp
  tests/static_modules_tests.cxx
)
# 本模板是纯模块 TU；clang-tidy 不支持 CMake @modmap，tidy-check 不扫描模块。
# 格式与语法检查仍通过预构建 gate 和 clang 编译器诊断完成。
set(CXX_TIDY_PLAIN_SOURCES "" CACHE STRING "Plain TU sources for tidy-check" FORCE)
cxx_phase_variables()

if(CXX_SHOW_CMAKE_CONFIG)
  cxx_sep("CMake 配置")
endif()
configure_file(
  cmake/single-exe-static-modules.h.in
  CMakeTutorialConfig.hxx
  @ONLY
)
if(CXX_SHOW_CMAKE_CONFIG)
  cxx_msg(grass "configure_file → CMakeTutorialConfig.hxx")
endif()

cxx_sep("构建独立静态模块")

# ---- 独立静态模块：公共基础层 -------------------------------------------
# InterfaceLog 是唯一依赖 bqlog 的模块，其他业务模块只 import 其导出 API。
cxx_add_module(interface_log modules/interface_log/include/InterfaceLog.cppm)
target_include_directories(interface_log PUBLIC
  "${PROJECT_BINARY_DIR}"
  "${PROJECT_SOURCE_DIR}/modules/interface_log/include")
cxx_apply_static_module_defaults(interface_log)
cxx_link_bqlog(interface_log)   # module 模式：import bq.log
cxx_msg(grass "STATIC module: interface_log（bqlog module + 1000 行环形记录）")

# ---- 独立静态模块：同步与任务领域 ---------------------------------------
# ProjectSync/TaskManager 只依赖 InterfaceLog，彼此不直接 import；
# WorkflowUI 作为聚合层链接两者，负责把领域错误映射成 Alfred 呈现。
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

# ReminderSync 只把已校验任务快照映射到 AppleScript，不读取 Stacks 文件。
cxx_add_module(reminder_sync modules/reminder_sync/include/ReminderSync.cppm)
target_include_directories(reminder_sync PUBLIC
  "${PROJECT_SOURCE_DIR}/modules/reminder_sync/include")
target_link_libraries(reminder_sync PUBLIC interface_log)
cxx_apply_static_module_defaults(reminder_sync)
cxx_msg(grass "STATIC module: reminder_sync")

# WorkflowUI 是唯一的 UI 适配层；可依赖所有领域模块，领域模块不可反向依赖 UI。
cxx_add_module(workflow_ui modules/workflow_ui/include/WorkflowUI.cppm)
target_include_directories(workflow_ui PUBLIC
  "${PROJECT_SOURCE_DIR}/modules/workflow_ui/include")
target_include_directories(workflow_ui PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(workflow_ui PUBLIC
  interface_log project_sync task_manager reminder_sync)
cxx_apply_static_module_defaults(workflow_ui)
cxx_msg(grass "STATIC module: workflow_ui")

# ---- 单 main：只做单层 import / CLI 路由 -------------------------------
# 可执行目标名保持 `Stacks_Tools`，这是 Alfred 脚本和安装布局的稳定运行契约。
cxx_add_module_consumer(Stacks_Tools src/main.cpp)
target_include_directories(Stacks_Tools PRIVATE
  "${PROJECT_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}/modules/interface_log/include"
  "${PROJECT_SOURCE_DIR}/modules/project_sync/include"
  "${PROJECT_SOURCE_DIR}/modules/task_manager/include"
  "${PROJECT_SOURCE_DIR}/modules/reminder_sync/include"
  "${PROJECT_SOURCE_DIR}/modules/workflow_ui/include")
target_link_libraries(Stacks_Tools PRIVATE
  interface_log project_sync task_manager reminder_sync workflow_ui)
cxx_apply_static_module_defaults(Stacks_Tools)
cxx_msg(grass "single main: Stacks_Tools")

# ---- 单元测试（GTest：边界 + 并行；变异测试见 just mutation-test）----
# 测试目标链接全部模块，但仍使用隔离运行时目录，不允许写真实 Stacks 数据。
if(CXX_GTEST_FOUND)
  cxx_add_module_consumer(static_modules_tests tests/static_modules_tests.cxx)
  target_include_directories(static_modules_tests PRIVATE
    "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/modules/interface_log/include"
    "${PROJECT_SOURCE_DIR}/modules/project_sync/include"
    "${PROJECT_SOURCE_DIR}/modules/task_manager/include"
    "${PROJECT_SOURCE_DIR}/modules/reminder_sync/include"
    "${PROJECT_SOURCE_DIR}/modules/workflow_ui/include")
  target_link_libraries(static_modules_tests PRIVATE
    interface_log project_sync task_manager reminder_sync workflow_ui)
  cxx_apply_static_module_defaults(static_modules_tests)
  # 固定空白测试目录/日志目录，确保 IDE、CTest 和 just 调用行为一致。
  target_compile_definitions(static_modules_tests PRIVATE
    STACKS_TEST_RUNTIME_DIR="${PROJECT_SOURCE_DIR}/tests/.runtime"
    STACKS_TEST_LOG_DIR="${PROJECT_SOURCE_DIR}/tests/logs")
  cxx_add_gtest(static_modules_tests)
  cxx_msg(grass "GTest: static_modules_tests")
endif()

# clang-doc 消费含 @.modmap 参数的 compile_commands.json；先构建所有会进入
# 文档库的模块目标，确保 std/bqlog/业务模块的模块映射在文档阶段已存在。
set(CXX_DOC_CLANGDOC_BUILD_TARGETS Stacks_Tools)
set(CXX_DOC_CLANGDOC_SOURCES
  "${PROJECT_SOURCE_DIR}/modules/interface_log/include/InterfaceLog.cppm"
  "${PROJECT_SOURCE_DIR}/modules/project_sync/include/ProjectSync.cppm"
  "${PROJECT_SOURCE_DIR}/modules/task_manager/include/TaskManager.cppm"
  "${PROJECT_SOURCE_DIR}/modules/reminder_sync/include/ReminderSync.cppm"
  "${PROJECT_SOURCE_DIR}/modules/workflow_ui/include/WorkflowUI.cppm"
  "${PROJECT_SOURCE_DIR}/src/main.cpp")
if(CXX_GTEST_FOUND)
  list(APPEND CXX_DOC_CLANGDOC_BUILD_TARGETS static_modules_tests)
  list(APPEND CXX_DOC_CLANGDOC_SOURCES "${PROJECT_SOURCE_DIR}/tests/static_modules_tests.cxx")
endif()

cxx_phase_finish()
