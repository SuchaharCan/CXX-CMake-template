/**
 * @file fuzz_parse_price.cxx
 * @brief libFuzzer harness（模块消费者）：模糊测试 modern::parse_price_cents。
 *
 * cmake --preset fuzz && cmake --build --preset fuzz
 * cmake --build . --target run-fuzz
 */

import std;
import modern_paradigm;

#include <cstddef>
#include <cstdint>

/// libFuzzer 入口：限制输入长度，仅调用解析器
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size == 0 || size > 32) {
    return 0;
  }
  const std::string_view sv{reinterpret_cast<const char*>(data), size};
  static_cast<void>(modern::parse_price_cents(sv));
  return 0;
}
