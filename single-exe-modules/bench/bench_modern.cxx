/**
 * @file bench_modern.cxx
 * @brief Google Benchmark — modern_paradigm 模块消费者微基准。
 *
 * cmake --build . --target Ticket_bench
 * cmake --build . --target run-bench
 */

import std;
import modern_paradigm;

#include <benchmark/benchmark.h>

#include <vector>

/// 反复解析固定分字符串
static void BM_ParsePrice(benchmark::State& state) {
  for (auto _ : state) {
    auto r = modern::parse_price_cents("123456");
    benchmark::DoNotOptimize(r);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ParsePrice);

/// 向量长度由 Range 控制，测 sum_cents 扩展性
static void BM_SumCents(benchmark::State& state) {
  std::vector<modern::Price> v(static_cast<std::size_t>(state.range(0)), modern::Price{7});
  for (auto _ : state) {
    auto s = modern::sum_cents(v);
    benchmark::DoNotOptimize(s);
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
  state.SetBytesProcessed(
      static_cast<int64_t>(state.iterations()) * state.range(0) * static_cast<int64_t>(sizeof(modern::Price)));
}
BENCHMARK(BM_SumCents)->Range(8, 8 << 10);
