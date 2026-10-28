# Engineering toolkit (macOS · LLVM/Clang · CMake · modules)

> 与 `single-exe-classic` 对称的工程项；**模块差异**见下表。安全标准见 [`SAFETY_AWARENESS.md`](SAFETY_AWARENESS.md)（意识层，非认证）。

## 日常（强制 Ninja）

```bash
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
cd build
cmake -G Ninja ..   # 或 cmake --preset default
cmake --build .
ctest --output-on-failure
```

| Preset | 用途 |
|--------|------|
| `default` / `debug` / `tsan` / `release` / `fuzz` / `coverage` | 同 00_0 |
| `include-cleaner` | tidy include-cleaner（主要益处在 `.cxx/.hxx`） |
| `safety-lint` | JPL 启发 tidy |
| `docs` | Doxygen / Sphinx / MkDocs |

## 模块 vs 头文件卫生

| 工具 | Module 策略 |
|------|-------------|
| **IWYU** | 默认 OFF；对含 `.cppm` 的目标 **跳过**；勿当作模块图清理器 |
| **include-cleaner** | 可选；`.clangd` 对 modules **关闭** Unused/Missing；`ScenarioLog.hxx` 等传统 TU 可开 |
| **clang-tidy** | Preset 默认 OFF（噪声）；需要时 `-DCXX_ENABLE_CLANG_TIDY=ON` |

## 微基准 & Fuzz

```bash
cmake --build . --target run-bench
cmake --preset fuzz && cmake --build --preset fuzz
cmake --build build/fuzz --target run-fuzz
```

种子语料：`fuzz/corpus/fuzz_parse_price/`。Fuzz 标志经 `cxx_apply_libfuzzer_flags` 统一。

## API 文档

```bash
brew install doxygen && pip install -r docs/requirements-docs.txt
cmake .. -DCXX_ENABLE_DOCS=ON
cmake --build . --target docs | docs-sphinx | docs-mkdocs
```

Doxygen 会扫 `.cppm`；模块导出符号的文档成熟度因工具链而异。

## 安全意识

见 [`SAFETY_AWARENESS.md`](SAFETY_AWARENESS.md) · [`traceability/TRACEABILITY_MATRIX.md`](traceability/TRACEABILITY_MATRIX.md)。

**不做：** MISRA 认证、DO-178C 证据包、全面禁堆/禁异常（与 `import std` / 教学范式冲突）。

## 开关摘要

| 变量 | 说明 |
|------|------|
| `CXX_ENABLE_CCACHE` | 已恢复；BMI 缓存策略见 ccache 文档 |
| `CXX_ENABLE_PCH` | 仅可执行消费者 |
| `CXX_USE_LLD` / `CXX_ALLOCATOR` / sanitizers | 同 00_0 |
| `CXX_SAFETY_LINT` / `CXX_ENABLE_INCLUDE_CLEANER` / `CXX_ENABLE_DOCS` | 见上 |
