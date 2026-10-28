/**
 * @file modern_paradigm.cppm
 * @module modern_paradigm
 * @brief 现代 C++ 范式演示（模块版）：expected + 强类型 + span。
 *
 * 供 GTest、Google Benchmark 与 libFuzzer harness 通过 import 使用。
 */

export module modern_paradigm;

import std;

export namespace modern {

/// 价格强类型（整型分）
struct Price {
  std::int64_t cents{};
  friend constexpr bool operator==(Price, Price) = default;
};

/// 数量强类型（整型单位）
struct Quantity {
  std::int32_t value{};
  friend constexpr bool operator==(Quantity, Quantity) = default;
};

/// 价格字符串解析的可恢复错误码
enum class ParseErr : int { empty = 1, invalid = 2, overflow = 3 };

/// 解析 ASCII 数字分字符串；失败返回 ParseErr
[[nodiscard]] std::expected<Price, ParseErr> parse_price_cents(std::string_view s) {
  if (s.empty()) {
    return std::unexpected(ParseErr::empty);
  }
  std::int64_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') {
      return std::unexpected(ParseErr::invalid);
    }
    int const d = c - '0';
    if (v > (std::numeric_limits<std::int64_t>::max() - d) / 10) {
      return std::unexpected(ParseErr::overflow);
    }
    v = v * 10 + d;
  }
  return Price{v};
}

/// 对价格 span 求和（零拷贝）
[[nodiscard]] std::int64_t sum_cents(std::span<Price const> prices) noexcept {
  std::int64_t s = 0;
  for (Price p : prices) {
    s += p.cents;
  }
  return s;
}

}  // namespace modern
