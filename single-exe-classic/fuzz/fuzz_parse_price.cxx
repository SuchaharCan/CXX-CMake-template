/**
 * @file fuzz_parse_price.cxx
 * @brief libFuzzer  harness：对 parse_price_cents 做鲁棒性模糊测试。
 *
 * cmake --preset fuzz && cmake --build --preset fuzz
 * ./fuzz_parse_price fuzz/corpus/fuzz_parse_price -max_total_time=30
 * cmake --build . --target run-fuzz
 */

#include "ModernParadigm.hxx"

#include <cstddef>
#include <cstdint>
#include <string_view>

/// libFuzzer 入口：限制输入长度，仅调用解析器不断言
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size == 0 || size > 32) {
    return 0;
  }
  const std::string_view sv{reinterpret_cast<const char*>(data), size};
  static_cast<void>(tutorial::parse_price_cents(sv));
  return 0;
}
