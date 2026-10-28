# Safety awareness (NOT certification)

> 本模板是 **学习 / 工程脚手架**，不是航电或汽车功能安全认证包。
> `CXX_SAFETY_LINT=ON` 只启用 **启发自** JPL / CERT 的 clang-tidy 剖面，**不构成** MISRA 或 DO-178C 合规证明。

## 为何不把 MISRA / DO-178C 焊进 Single_Exe

| 标准 | 工业含义 | 本模板策略 |
|------|----------|------------|
| **MISRA C++:2023** | 合并 AUTOSAR C++14 思路，面向 C++17 子集，强约束 UB / 继承 / 转换 | 另立 `safety` 品牌模板才合理；此处仅文档 + 可选 lint 子集 |
| **DO-178C (DAL A/B)** | 需求↔架构↔源码↔目标码 **双向追溯** + 覆盖率证据 | 提供 `docs/traceability/` **教学矩阵草稿**，不生成认证证据 |
| **NASA JPL Power of Ten** | 简单控制流、禁递归、查返回值、限指针层级等 | `.clang-tidy.safety` 打开 `misc-no-recursion`、CERT、避免 goto 等；**不全量禁用堆/异常**（与教学 `expected`/容器冲突） |

## Power of Ten → 模板映射（意识层）

1. **简单控制流 / 禁递归** → `misc-no-recursion`（safety 剖面）  
2. **最小作用域** → 代码评审 + readability 规则，非自动证明  
3. **检查返回值** → `cert-err*` / `[[nodiscard]]` 示例（`parse_price_cents`）  
4. **限制指针层级** → Core Guidelines / CERT；模块与 `span` 示例优先值语义  

## 推荐用法

```bash
# 意识门禁（仍可编译现代 C++23 示例）
cmake --preset default -DCXX_SAFETY_LINT=ON -DCXX_ENABLE_CLANG_TIDY=ON
cmake --build . --target tidy-check
```

若要做 **真·安全关键交付**（无堆、禁异常、MC/DC、形式化、商业 SAST）：请开独立模板品牌，勿污染本学习 exe。

## 追溯性草稿

见 [`traceability/TRACEABILITY_MATRIX.md`](traceability/TRACEABILITY_MATRIX.md) — 演示 DO-178C「需求→实现→测试」表格形态，供课程/设计讨论，**不可**当作 DAL 证据包。


## 模块模板补充

`import std` / 堆分配与「全面禁堆 / 禁异常」子集冲突。本模板的 `CXX_SAFETY_LINT` 只加强静态分析启发项，**不**切换到 freestanding / MISRA 子集方言。
