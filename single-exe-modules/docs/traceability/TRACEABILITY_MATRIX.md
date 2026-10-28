# Traceability matrix (teaching stub)

> DO-178C 风格的 **示意** 表。填写真实项目时再替换 ID / 证据链接。  
> **不构成** 任何 DAL 认证材料。

| Req ID | 需求摘要 | 设计/模块 | 源文件 | 测试 | 备注 |
|--------|----------|-----------|--------|------|------|
| REQ-PARSE-001 | 价格字符串解析为分；非法输入可区分错误 | `modern::parse_price_cents` | `src/modern_paradigm.cppm` | `modern_gtest` / `fuzz_parse_price` | `std::expected` |
| REQ-SUM-001 | 对价格序列求和且不取得所有权 | `modern::sum_cents` | 同上 | GTest + Benchmark | `std::span` |
| REQ-TICKET-001 | 机票业务演示可运行 | `airline_ticket` | `src/AirlineTicket.cppm` | `airline_gtest` | 模块卖点 |
| REQ-LOG-001 | 场景日志可写文件 | `ScenarioLog` | `src/ScenarioLog.*` | 手工 / exe 冒烟 | spdlog |

## 覆盖率钩子

```bash
cmake --preset coverage && cmake --build --preset coverage && ctest --test-dir build/coverage
# 再按 llvm-cov 文档生成报告（见 ENGINEERING.md）
```
