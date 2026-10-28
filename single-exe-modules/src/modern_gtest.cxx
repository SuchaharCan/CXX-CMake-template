/**
 * @file modern_gtest.cxx
 * @brief modern_paradigm 模块单元测试：解析、求和与错误路径。
 */

import std;
import modern_paradigm;

#include <gtest/gtest.h>

TEST(ModernParadigm, ParseAndSum) {
  auto r = modern::parse_price_cents("100");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->cents, 100);
  std::vector<modern::Price> v{*r, modern::Price{50}};
  EXPECT_EQ(modern::sum_cents(v), 150);
  EXPECT_FALSE(modern::parse_price_cents("x").has_value());
}
