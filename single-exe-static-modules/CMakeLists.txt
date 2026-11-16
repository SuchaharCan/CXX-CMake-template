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
#   Ninja + CMake ≥4.5.0 + Clang++ ≥23.1 + C++26 + libc++
#   std 模块由 CMake 4.5 原生 import std 按本机架构构建
# =============================================================================

cmake_minimum_required(VERSION 4.5.0)

include(cmake/00_core_compiler.cmake)

project(
  OmniStacksStaticModules
  VERSION 2.1.0
  DESCRIPTION "Omni-Stacks：Stacks 2 Alfred task core（C++26 static modules / Ninja / Clang / libc++）"
  LANGUAGES CXX
)

include(cmake/00_core_color.cmake)
include(cmake/00_core_log.cmake)
include(cmake/00_core_deps.cmake)
include(cmake/00_core_conan.cmake)
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
  modules/algorithms_study/include/JsonAlgorithms.cppm
  modules/algorithms_study/include/EigenAlgorithms.cppm
  modules/algorithms_study/include/AsioAlgorithms.cppm
  modules/algorithms_study/include/SqliteAlgorithms.cppm
  modules/algorithms_study/include/AbseilAlgorithms.cppm
  modules/algorithms_study/include/BoostAlgorithms.cppm
  modules/algorithms_study/include/OpenCvAlgorithms.cppm
  modules/algorithms_study/include/AlgorithmsStudy.cppm
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

# ---- Conan 锁定的 JSON 依赖（替代旧 json.hpp 单头文件）------------------
# 业务模块在全局模块片段里 #include <nlohmann/json.hpp>，include 路径由
# nlohmann_json::nlohmann_json imported target 提供。
if(TARGET nlohmann_json::nlohmann_json)
  target_link_libraries(project_sync PUBLIC nlohmann_json::nlohmann_json)
  target_link_libraries(task_manager PUBLIC nlohmann_json::nlohmann_json)
  target_link_libraries(workflow_ui PUBLIC nlohmann_json::nlohmann_json)
endif()

# ---- 独立学习模块 Algorithms_Study（仅 Debug；按库独立 gating 的模块化封装）----
# 每个 wrapper 只在对应 Conan 库存在时创建；AlgorithmsStudy 聚合当前可用的
# wrapper，因此缺失重库（如 OpenCV）时其余模块与构建仍可正常通过。
set(CXX_ALGORITHMS_STUDY_AVAILABLE FALSE)
set(_study_modules "")
set(_study_defines "")

if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND TARGET nlohmann_json::nlohmann_json)
  cxx_add_module(json_algorithms modules/algorithms_study/include/JsonAlgorithms.cppm)
  target_include_directories(json_algorithms PUBLIC "${PROJECT_SOURCE_DIR}/modules/algorithms_study/include")
  target_link_libraries(json_algorithms PUBLIC nlohmann_json::nlohmann_json)
  cxx_apply_static_module_defaults(json_algorithms)
  list(APPEND _study_modules json_algorithms)
  list(APPEND _study_defines CXX_HAS_JSON_ALGORITHMS)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND TARGET Eigen3::Eigen)
  cxx_add_module(eigen_algorithms modules/algorithms_study/include/EigenAlgorithms.cppm)
  target_include_directories(eigen_algorithms PUBLIC "${PROJECT_SOURCE_DIR}/modules/algorithms_study/include")
  target_link_libraries(eigen_algorithms PUBLIC Eigen3::Eigen)
  cxx_apply_static_module_defaults(eigen_algorithms)
  list(APPEND _study_modules eigen_algorithms)
  list(APPEND _study_defines CXX_HAS_EIGEN_ALGORITHMS)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND TARGET asio::asio)
  cxx_add_module(asio_algorithms modules/algorithms_study/include/AsioAlgorithms.cppm)
  target_include_directories(asio_algorithms PUBLIC "${PROJECT_SOURCE_DIR}/modules/algorithms_study/include")
  target_link_libraries(asio_algorithms PUBLIC asio::asio)
  cxx_apply_static_module_defaults(asio_algorithms)
  list(APPEND _study_modules asio_algorithms)
  list(APPEND _study_defines CXX_HAS_ASIO_ALGORITHMS)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND TARGET sqlite_orm::sqlite_orm)
  cxx_add_module(sqlite_algorithms modules/algorithms_study/include/SqliteAlgorithms.cppm)
  target_include_directories(sqlite_algorithms PUBLIC "${PROJECT_SOURCE_DIR}/modules/algorithms_study/include")
  target_link_libraries(sqlite_algorithms PUBLIC sqlite_orm::sqlite_orm)
  cxx_apply_static_module_defaults(sqlite_algorithms)
  list(APPEND _study_modules sqlite_algorithms)
  list(APPEND _study_defines CXX_HAS_SQLITE_ALGORITHMS)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND TARGET absl::strings)
  cxx_add_module(abseil_algorithms modules/algorithms_study/include/AbseilAlgorithms.cppm)
  target_include_directories(abseil_algorithms PUBLIC "${PROJECT_SOURCE_DIR}/modules/algorithms_study/include")
  target_link_libraries(abseil_algorithms PUBLIC absl::strings)
  cxx_apply_static_module_defaults(abseil_algorithms)
  list(APPEND _study_modules abseil_algorithms)
  list(APPEND _study_defines CXX_HAS_ABSEIL_ALGORITHMS)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND TARGET Boost::headers)
  cxx_add_module(boost_algorithms modules/algorithms_study/include/BoostAlgorithms.cppm)
  target_include_directories(boost_algorithms PUBLIC "${PROJECT_SOURCE_DIR}/modules/algorithms_study/include")
  target_link_libraries(boost_algorithms PUBLIC Boost::headers)
  cxx_apply_static_module_defaults(boost_algorithms)
  list(APPEND _study_modules boost_algorithms)
  list(APPEND _study_defines CXX_HAS_BOOST_ALGORITHMS)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND TARGET opencv::opencv_core)
  cxx_add_module(opencv_algorithms modules/algorithms_study/include/OpenCvAlgorithms.cppm)
  target_include_directories(opencv_algorithms PUBLIC "${PROJECT_SOURCE_DIR}/modules/algorithms_study/include")
  target_link_libraries(opencv_algorithms PUBLIC opencv::opencv_core)
  cxx_apply_static_module_defaults(opencv_algorithms)
  list(APPEND _study_modules opencv_algorithms)
  list(APPEND _study_defines CXX_HAS_OPENCV_ALGORITHMS)
endif()

if(_study_modules)
  cxx_add_module(algorithms_study modules/algorithms_study/include/AlgorithmsStudy.cppm)
  target_include_directories(algorithms_study PUBLIC "${PROJECT_SOURCE_DIR}/modules/algorithms_study/include")
  target_link_libraries(algorithms_study PUBLIC ${_study_modules})
  foreach(_d IN LISTS _study_defines)
    target_compile_definitions(algorithms_study PRIVATE "${_d}=1")
  endforeach()
  cxx_apply_static_module_defaults(algorithms_study)
  set(CXX_ALGORITHMS_STUDY_AVAILABLE TRUE)
  cxx_msg(grass "STATIC module: algorithms_study（Debug-only, ${_study_modules}）")
else()
  cxx_msg(yellow "AlgorithmsStudy skipped（未发现 Conan 学习依赖，先 just conan）")
endif()

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
if(CXX_ALGORITHMS_STUDY_AVAILABLE)
  target_link_libraries(Stacks_Tools PRIVATE algorithms_study)
  target_compile_definitions(Stacks_Tools PRIVATE CXX_ALGORITHMS_STUDY=1)
endif()

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
  if(CXX_ALGORITHMS_STUDY_AVAILABLE)
    target_link_libraries(static_modules_tests PRIVATE algorithms_study)
    target_compile_definitions(static_modules_tests PRIVATE CXX_ALGORITHMS_STUDY=1)
  endif()
  cxx_add_gtest(static_modules_tests)
  cxx_msg(grass "GTest: static_modules_tests")
endif()

# clang-doc 消费含 @.modmap 参数的 compile_commands.json；先构建所有会进入
# 文档库的模块目标，确保 std/bqlog/业务模块的模块映射在文档阶段已存在。
set(CXX_DOC_CLANGDOC_BUILD_TARGETS
  interface_log
  project_sync
  task_manager
  reminder_sync
  workflow_ui
  Stacks_Tools)
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
