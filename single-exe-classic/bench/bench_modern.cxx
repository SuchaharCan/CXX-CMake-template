/**
 * @file bench_modern.cxx
 * @brief Google Benchmark — parse_price_cents / sum_cents 微基准。
 *
 * 构建：cmake --build . --target step1_bench
 * 运行：./step1_bench 或 cmake --build . --target run-bench（CI 冒烟 min_time=0.05s）
 */

#include "ModernParadigm.hxx"

#include <benchmark/benchmark.h>

#include <string>
#include <vector>

/// 固定字符串反复解析，测 parse_price_cents 吞吐
static void BM_ParsePrice(benchmark::State& state) {
  const std::string s = "123456";
  for (auto _ : state) {
    auto r = tutorial::parse_price_cents(s);
    benchmark::DoNotOptimize(r);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ParsePrice);

/// 向量长度由 Range 控制，测 sum_cents 随规模扩展
static void BM_SumCents(benchmark::State& state) {
  std::vector<tutorial::Price> v(static_cast<std::size_t>(state.range(0)), tutorial::Price{7});
  for (auto _ : state) {
    auto s = tutorial::sum_cents(v);
    benchmark::DoNotOptimize(s);
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
  state.SetBytesProcessed(
      static_cast<int64_t>(state.iterations()) * state.range(0) * static_cast<int64_t>(sizeof(tutorial::Price)));
}
BENCHMARK(BM_SumCents)->Range(8, 8 << 10);
