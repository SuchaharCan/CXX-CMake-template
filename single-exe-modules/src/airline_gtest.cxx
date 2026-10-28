/**
 * @file airline_gtest.cxx
 * @brief airline_ticket 模块单元测试；全局 Environment 写 gtest.log。
 */

#include <gtest/gtest.h>

#include <string_view>

#include "ScenarioLog.hxx"

import std;
import airline_ticket;

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

struct RegisterSpdlogEnv {
  RegisterSpdlogEnv() { ::testing::AddGlobalTestEnvironment(new SpdlogEnvironment); }
};

const RegisterSpdlogEnv kRegisterSpdlogEnv;

}  // namespace

TEST(AirlineTicket, PriceFromMiles) {
  gtest_info("TEST PriceFromMiles");
  AirlineTicket t;
  t.setNumberOfMiles(700);
  constexpr double expected = 70.0;
  const double actual = t.calculatePriceInDollars();
  gtest_case("calculatePriceInDollars", 700, expected, actual);
  EXPECT_DOUBLE_EQ(actual, expected);
}

TEST(AirlineTicket, EliteFliesFree) {
  gtest_info("TEST EliteFliesFree");
  AirlineTicket t;
  t.setNumberOfMiles(700);
  t.setHasEliteSuperRewardsStatus(true);
  constexpr double expected = 0.0;
  const double actual = t.calculatePriceInDollars();
  gtest_case("calculatePriceInDollars(elite)", 700, expected, actual);
  EXPECT_DOUBLE_EQ(actual, expected);
}

TEST(AirlineTicket, PassengerName) {
  gtest_info("TEST PassengerName");
  AirlineTicket t;
  const std::string name = "Sherman T. Socketwrench";
  t.setPassengerName(name);
  gtest_case("getPassengerName", name, name, t.getPassengerName());
  EXPECT_EQ(t.getPassengerName(), name);
}
