# =============================================================================
# single-exe-modules — C++23 模块单可执行模板入口（import std + 自研模块）
#
# 分层加载顺序：
#   A  00_core_compiler.cmake（pre-project；强制 Ninja）
#   B  project()
#   C  00_core_* / 10_core_setup（必选 core；9*_opt_* 由 setup 自动发现）
#   D  业务：模块源 / add targets / 阶段 API
#
# 日常增量：cd build && cmake --build .
# 改 CMake / -D / 换编译器：cmake -G Ninja .. && cmake --build .
# =============================================================================

cmake_minimum_required(VERSION 4.4.4)

# ---- A. pre-project（无日志模块；模块模板强制 Ninja）----
include(cmake/00_core_compiler.cmake)

project(
  AirlineTicket
  VERSION 1.0.12
  DESCRIPTION "纯 C++23 模块单可执行：airline_ticket + modern_paradigm + import std"
  HOMEPAGE_URL "https://github.com/Rocky-houjinsong/CMakeTutorialCXX20"
  LANGUAGES CXX
)

# ---- B. 核心模块（必选）----
include(cmake/00_core_color.cmake)
include(cmake/00_core_log.cmake)
include(cmake/00_core_deps.cmake)
include(cmake/00_core_buildlog.cmake)
include(cmake/00_core_profile.cmake)
include(cmake/00_core_modules.cmake)
include(cmake/00_core_lint.cmake)
include(cmake/10_core_setup.cmake)

# ---- C. 编排（可选模块 9*_opt_* 由 bootstrap 自动加载）----
cxx_setup_bootstrap()
cxx_phase_toolchain()

set(STR_TEST "AirlineTicket C++23 modules")

cxx_phase_variables()

if(CXX_SHOW_CMAKE_CONFIG)
  cxx_sep("CMake 配置")
endif()

configure_file(
  cmake/single-exe-modules.h.in
  CMakeTutorialConfig.hxx
  @ONLY
)
if(CXX_SHOW_CMAKE_CONFIG)
  cxx_msg(grass "configure_file → CMakeTutorialConfig.hxx")
endif()

# ---- 业务源（模板使用者主要改这里）----
set(CXX_SHARED_MODULES
  src/AirlineTicket.cppm
  src/modern_paradigm.cppm
)
set(CXX_SCENARIO_LOG
  src/ScenarioLog.cxx
)
set(CXX_APP_CONSUMER
  src/AirlineTicketMain.cxx
  ${CXX_SCENARIO_LOG}
)
set(CXX_TEST_CONSUMER
  src/airline_gtest.cxx
  src/modern_gtest.cxx
  ${CXX_SCENARIO_LOG}
)

cxx_set_lint_sources(
  src/AirlineTicket.cppm
  src/modern_paradigm.cppm
  src/AirlineTicketMain.cxx
  src/airline_gtest.cxx
  src/modern_gtest.cxx
  src/ScenarioLog.cxx
  src/ScenarioLog.hxx
)

cxx_add_module(ticket_mods ${CXX_SHARED_MODULES})
target_include_directories(ticket_mods PUBLIC
  "${PROJECT_BINARY_DIR}"   # CMakeTutorialConfig.hxx
  "${PROJECT_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}/src"
)
cxx_apply_template_defaults(ticket_mods)
cxx_msg(grass "module library ticket_mods (airline_ticket + modern_paradigm) configured")

cxx_add_module_consumer(Ticket ${CXX_APP_CONSUMER})
target_include_directories(Ticket PRIVATE
  "${PROJECT_BINARY_DIR}"
  "${PROJECT_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(Ticket PRIVATE ticket_mods)
cxx_apply_template_defaults(Ticket)
cxx_msg(grass "module-consumer exe Ticket configured")

if(CXX_GTEST_FOUND)
  cxx_add_module_consumer(Ticket_tests ${CXX_TEST_CONSUMER})
  target_include_directories(Ticket_tests PRIVATE
    "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/src"
  )
  target_link_libraries(Ticket_tests PRIVATE ticket_mods)
  cxx_apply_template_defaults(Ticket_tests)
  cxx_add_gtest(Ticket_tests)
  cxx_msg(grass "GTest module-consumer: Ticket_tests")
  cxx_msg(cyan "CTest: ${CMAKE_BINARY_DIR}/Ticket_tests")
endif()

if(CXX_BENCHMARK_FOUND)
  cxx_add_module_consumer(Ticket_bench bench/bench_modern.cxx)
  target_link_libraries(Ticket_bench PRIVATE
    ticket_mods benchmark::benchmark benchmark::benchmark_main)
  target_include_directories(Ticket_bench PRIVATE
    "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src")
  cxx_apply_lld(Ticket_bench)
  cxx_link_allocator(Ticket_bench)
  cxx_apply_coverage(Ticket_bench)
  cxx_note_benchmark(Ticket_bench)
  cxx_msg(grass "benchmark: Ticket_bench")
endif()

cxx_add_fuzzer(fuzz_parse_price
  SOURCES fuzz/fuzz_parse_price.cxx
  CORPUS "${CMAKE_SOURCE_DIR}/fuzz/corpus/fuzz_parse_price"
)
if(TARGET fuzz_parse_price)
  target_link_libraries(fuzz_parse_price PRIVATE ticket_mods)
  target_include_directories(fuzz_parse_price PRIVATE
    "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src")
endif()

cxx_phase_finish()
