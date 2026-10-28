#pragma once

/**
 * @file ModernParadigm.hxx
 * @brief 现代 C++ 范式演示：强类型、std::expected、span、concept。
 *
 * 供主程序、GTest、Benchmark 与 libFuzzer 共用；实现见 ModernParadigm.cxx。
 */

#include <concepts>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

namespace tutorial {

/// 价格强类型（整型分），避免与 Quantity 静默混用
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

/**
 * @brief 类型安全的解析：用 std::expected 表达错误（非 magic -1）。
 * @param s 仅含 ASCII 数字的分字符串
 * @return 成功为 Price，失败为 ParseErr
 */
[[nodiscard]] std::expected<Price, ParseErr> parse_price_cents(std::string_view s);

/// 对价格数组求和（std::span 零拷贝 API）
[[nodiscard]] std::int64_t sum_cents(std::span<const Price> prices) noexcept;

/// Concept 演示：具名数值类型须暴露 .value 且可转为 int32
template <class T>
concept NamedValue = requires(T t) {
  { t.value } -> std::convertible_to<std::int32_t>;
};

/// 提取 NamedValue 的原始数量值
template <NamedValue T>
[[nodiscard]] constexpr std::int32_t raw_qty(T t) noexcept {
  return t.value;
}

}  // namespace tutorial
