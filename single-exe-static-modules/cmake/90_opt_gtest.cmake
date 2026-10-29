# =============================================================================
# 90_opt_gtest.cmake — 可选 GoogleTest（发现 + 注册）
# 分层: opt（可选，需手动开启）
# 独立: 是（仅依赖 color/log 提供的 cxx_msg / cxx_flog）
# 下游: 10_core_setup.cmake（GLOB 加载）；根 CMakeLists（CXX_GTEST_FOUND 守卫）
# 启用: cmake .. -DCXX_ENABLE_GTEST=ON（默认 OFF）
# =============================================================================

include_guard(GLOBAL)

option(CXX_ENABLE_GTEST "Build GoogleTest samples when GTest is available" OFF)

set(CXX_GTEST_FOUND OFF CACHE INTERNAL "GTest discovered" FORCE)

if(NOT CXX_ENABLE_GTEST)
  cxx_msg(yellow "GTest: disabled (手动开启: -DCXX_ENABLE_GTEST=ON)")
else()
  find_package(GTest CONFIG QUIET)
  if(NOT GTest_FOUND)
    find_package(GTest QUIET)
  endif()
  if(GTest_FOUND)
    set(CXX_GTEST_FOUND ON CACHE INTERNAL "GTest discovered" FORCE)
    cxx_msg(grass "GTest found (gtest_discover_tests 可用)")
  else()
    cxx_msg(yellow "GTest: not found — brew install googletest")
  endif()
endif()

# -----------------------------------------------------------------------------
# cxx_add_gtest(test_target)
# 作用：链接 GTest::gtest_main，并用 gtest_discover_tests 注册到 CTest。
# -----------------------------------------------------------------------------
function(cxx_add_gtest test_target)
  if(NOT CXX_GTEST_FOUND)
    return()
  endif()
  if(NOT TARGET ${test_target})
    message(FATAL_ERROR "cxx_add_gtest: target '${test_target}' does not exist")
  endif()
  target_link_libraries(${test_target} PRIVATE GTest::gtest_main)
  include(GoogleTest)
  # 排除故意失败的演示用例，保证默认 ctest 通过；编译不受影响
  gtest_discover_tests(${test_target}
    TEST_FILTER "-*AssertionFailDemo*"
  )
  cxx_flog(gtest INFO "registered CTest discovery for ${test_target}")
  cxx_flog(gtest OK "target ${test_target} from tutorial_gtest.cxx")
endfunction()
