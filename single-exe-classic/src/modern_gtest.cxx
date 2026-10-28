/**
 * @file modern_gtest.cxx
 * @brief ModernParadigm 单元测试：expected 解析、span 求和、concept 演示。
 */

#include <gtest/gtest.h>

#include <vector>

#include "ModernParadigm.hxx"
#include "TutorialMath.hxx"

TEST(ModernParadigm, ParsePriceOk) {
  auto r = tutorial::parse_price_cents("42");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->cents, 42);
}

TEST(ModernParadigm, ParsePriceErrors) {
  EXPECT_FALSE(tutorial::parse_price_cents("").has_value());
  EXPECT_FALSE(tutorial::parse_price_cents("12x").has_value());
}

TEST(ModernParadigm, SpanSum) {
  std::vector<tutorial::Price> v{{1}, {2}, {3}};
  EXPECT_EQ(tutorial::sum_cents(v), 6);
}

TEST(ModernParadigm, StrongQtyConcept) {
  EXPECT_EQ(tutorial::raw_qty(tutorial::Quantity{9}), 9);
}
