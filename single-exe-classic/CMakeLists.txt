# =============================================================================
# single-exe-classic — 头文件机制可执行程序模板入口
#
# 分层加载顺序：
#   A  00_core_compiler.cmake   （pre-project，无日志模块）
#   B  project()
#   C  00_core_* / 10_core_setup （必选 core；90_opt_* 由 setup 自动发现）
#   D  业务：源列表 / add_executable / 阶段 API
#
# 日常增量：cd build && cmake --build .
# 改 CMake / -D / 换编译器：cmake .. && cmake --build .
# =============================================================================

cmake_minimum_required(VERSION 4.1...4.4)

# ---- A. pre-project（无日志模块）----
include(cmake/00_core_compiler.cmake)

project(
  CMakeTutorialStep1
  VERSION 2.3.12
  DESCRIPTION "创建可执行程序， 使用项目配置"
  HOMEPAGE_URL "https://www.bing.com"
  LANGUAGES CXX
)

# ---- B. 核心模块（必选）----
include(cmake/00_core_color.cmake)
include(cmake/00_core_log.cmake)
include(cmake/00_core_deps.cmake)
include(cmake/00_core_buildlog.cmake)
include(cmake/00_core_profile.cmake)
include(cmake/00_core_lint.cmake)
include(cmake/10_core_setup.cmake)

# ---- C. 编排（可选模块 90_opt_* 由 bootstrap 自动加载）----
cxx_setup_bootstrap()
cxx_phase_toolchain()

# 业务侧 CMake 变量（模板示例）
set(STR_TEST "Hello This is a variable from CMakeLists.txt")

cxx_phase_variables()

cxx_sep("CMake 配置")

configure_file(cmake/TutorialConfig.h.in CMakeTutorialConfig.hxx)
cxx_msg(grass "configure_file → CMakeTutorialConfig.hxx")

# ---- 业务源（模板使用者主要改这里）----
set(CXX_STEP1_SOURCES
  src/tutorial.cxx
  src/TutorialMath.cxx
  src/ModernParadigm.cxx
  src/ScenarioLog.cxx
)
set(CXX_TEST_SOURCES
  src/tutorial_gtest.cxx
  src/modern_gtest.cxx
  src/TutorialMath.cxx
  src/ModernParadigm.cxx
  src/ScenarioLog.cxx
)
cxx_set_lint_sources(
  src/tutorial.cxx
  src/TutorialMath.cxx
  src/TutorialMath.hxx
  src/ModernParadigm.cxx
  src/ModernParadigm.hxx
  src/tutorial_gtest.cxx
  src/modern_gtest.cxx
  src/ScenarioLog.cxx
  src/ScenarioLog.hxx
)

add_executable(step1 ${CXX_STEP1_SOURCES})
target_include_directories(step1 PUBLIC
  "${PROJECT_BINARY_DIR}"   # CMakeTutorialConfig.hxx
  "${PROJECT_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}/src"
)
cxx_apply_template_defaults(step1)  # 核心门禁 + 可选自注册门禁
cxx_msg(grass "target step1 configured")

if(CXX_GTEST_FOUND)
  add_executable(step1_tests ${CXX_TEST_SOURCES})
  target_include_directories(step1_tests PRIVATE
    "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/src"
  )
  cxx_apply_template_defaults(step1_tests)
  cxx_add_gtest(step1_tests)
  cxx_msg(grass "GTest target: step1_tests")
  cxx_msg(cyan "CTest: ${CMAKE_BINARY_DIR}/step1_tests")
endif()

if(CXX_BENCHMARK_FOUND)
  cxx_add_benchmark(step1_bench bench/bench_modern.cxx src/ModernParadigm.cxx)
  target_include_directories(step1_bench PRIVATE
    "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src")
  cxx_apply_lld(step1_bench)
  cxx_link_allocator(step1_bench)
  cxx_msg(grass "benchmark target: step1_bench")
endif()

cxx_add_fuzzer(fuzz_parse_price
  SOURCES fuzz/fuzz_parse_price.cxx src/ModernParadigm.cxx
  CORPUS "${CMAKE_SOURCE_DIR}/fuzz/corpus/fuzz_parse_price"
)

cxx_phase_finish()
