/**
 * @file ModernParadigm.cxx
 * @brief ModernParadigm.hxx 的实现：分字符串解析与 span 求和。
 */

#include "ModernParadigm.hxx"

#include <cctype>
#include <limits>

namespace tutorial {

/// 逐字符解析十进制整数分；空串、非数字或溢出返回对应 ParseErr
std::expected<Price, ParseErr> parse_price_cents(std::string_view s) {
  if (s.empty()) {
    return std::unexpected(ParseErr::empty);
  }
  std::int64_t v = 0;
  for (char c : s) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
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

/// 累加各 Price::cents； noexcept 便于 fuzz/bench 热路径
std::int64_t sum_cents(std::span<Price const> prices) noexcept {
  std::int64_t s = 0;
  for (Price p : prices) {
    s += p.cents;
  }
  return s;
}

}  // namespace tutorial
