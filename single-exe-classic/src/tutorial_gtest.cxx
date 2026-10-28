/**
 * @file tutorial_gtest.cxx
 * @brief TutorialMath 单元测试；全局 Environment 经 ScenarioLog 写 gtest.log。
 *
 * 日志封装见 ScenarioLog（【不可删除】）；故意失败用例默认被 CTest 过滤。
 */

#include <gtest/gtest.h>

#include <string_view>

#include "ScenarioLog.hxx"
#include "TutorialMath.hxx"

using scenario_log::gtest_case;
using scenario_log::gtest_info;

namespace {

constexpr std::string_view kSep = "----- ----- ----- ----- ----- ----- ----- -----";

/// 套件起止时在 gtest.log 打横幅
class SpdlogEnvironment : public ::testing::Environment {
 public:
  void SetUp() override {
    gtest_info(kSep);
    gtest_info("GoogleTest suite starting");
  }
  void TearDown() override {
    gtest_info("GoogleTest suite finished");
    gtest_info(kSep);
  }
};

/// 静态注册，无需改 main
struct RegisterSpdlogEnv {
  RegisterSpdlogEnv() { ::testing::AddGlobalTestEnvironment(new SpdlogEnvironment); }
};

const RegisterSpdlogEnv kRegisterSpdlogEnv;

}  // namespace

TEST(TutorialMath, ComputeSqrtPositive) {
  gtest_info("TEST ComputeSqrtPositive");

  {
    constexpr double in = 9.0;
    constexpr double expected = 3.0;
    const double actual = tutorial::compute_sqrt(in);
    gtest_case("compute_sqrt", in, expected, actual);
    EXPECT_DOUBLE_EQ(actual, expected);
  }
  {
    constexpr double in = 0.0;
    constexpr double expected = 0.0;
    const double actual = tutorial::compute_sqrt(in);
    gtest_case("compute_sqrt", in, expected, actual);
    EXPECT_DOUBLE_EQ(actual, expected);
  }
  {
    constexpr double in = 2.0;
    constexpr double expected = 1.41421356237;
    const double actual = tutorial::compute_sqrt(in);
    gtest_case("compute_sqrt", in, expected, actual);
    EXPECT_NEAR(actual, expected, 1e-9);
  }
}

TEST(TutorialMath, ComputeSqrtNegativeRejected) {
  gtest_info("TEST ComputeSqrtNegativeRejected");

  constexpr double in = -4.0;
  constexpr double expected = -1.0;
  const double actual = tutorial::compute_sqrt(in);
  gtest_case("compute_sqrt", in, expected, actual);
  EXPECT_DOUBLE_EQ(actual, expected);
}

TEST(TutorialMath, ComputeSqrtOverMaxRejected) {
  gtest_info("TEST ComputeSqrtOverMaxRejected");

  const double in = tutorial::kComputeSqrtMaxInput + 1.0;
  constexpr double expected = -1.0;
  const double actual = tutorial::compute_sqrt(in);
  gtest_case("compute_sqrt", in, expected, actual);
  EXPECT_DOUBLE_EQ(actual, expected);
}

// 故意写错预期，演示断言失败时的终端/gtest.log 输出。
// 默认 ctest 已排除本用例（见 GoogleTest.cmake TEST_FILTER）；编译不受影响。
// 手动查看失败：./step1_tests --gtest_filter=*AssertionFailDemo*
TEST(TutorialMath, ComputeSqrtIllegalAssertionFailDemo) {
  gtest_info("TEST ComputeSqrtIllegalAssertionFailDemo（故意断言失败演示）");

  {
    constexpr double in = -4.0;
    constexpr double wrong_expected = 0.0;  // 正确应为 -1.0
    const double actual = tutorial::compute_sqrt(in);
    gtest_case("compute_sqrt", in, wrong_expected, actual);
    EXPECT_DOUBLE_EQ(actual, wrong_expected);
  }
  {
    const double in = tutorial::kComputeSqrtMaxInput + 1.0;
    constexpr double wrong_expected = 0.0;  // 正确应为 -1.0
    const double actual = tutorial::compute_sqrt(in);
    gtest_case("compute_sqrt", in, wrong_expected, actual);
    EXPECT_DOUBLE_EQ(actual, wrong_expected);
  }
}

TEST(TutorialMath, HasNumericArgument) {
  gtest_info("TEST HasNumericArgument");

  {
    constexpr int in = 1;
    constexpr bool expected = false;
    const bool actual = tutorial::has_numeric_argument(in);
    gtest_case("has_numeric_argument", in, expected, actual);
    EXPECT_FALSE(actual);
  }
  {
    constexpr int in = 2;
    constexpr bool expected = true;
    const bool actual = tutorial::has_numeric_argument(in);
    gtest_case("has_numeric_argument", in, expected, actual);
    EXPECT_TRUE(actual);
  }
  {
    constexpr int in = 3;
    constexpr bool expected = true;
    const bool actual = tutorial::has_numeric_argument(in);
    gtest_case("has_numeric_argument", in, expected, actual);
    EXPECT_TRUE(actual);
  }
}
