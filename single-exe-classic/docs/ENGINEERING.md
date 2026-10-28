# Engineering toolkit (macOS · LLVM/Clang · CMake)

> 覆盖 ✅/🟡/🟢/🔵 工程项；安全关键标准见 [`SAFETY_AWARENESS.md`](SAFETY_AWARENESS.md)（**意识层，非认证**）。

## 日常构建（固定 `build/`）

```bash
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
cd build
cmake -G Ninja ..          # 或 cmake --preset default
cmake --build .
ctest --output-on-failure
```

| Preset        | 用途                              |
| ------------- | --------------------------------- |
| `default`     | Ninja + RelWithDebInfo + ccache   |
| `debug`       | ASan + UBSan                      |
| `tsan`        | ThreadSanitizer（勿与 ASan 同开） |
| `release`     | Release + lld                     |
| `fuzz`        | libFuzzer                         |
| `coverage`    | llvm-cov 插桩                     |
| `iwyu`        | IWYU + include-cleaner 剖面       |
| `safety-lint` | JPL 启发 tidy（非 MISRA 证明）    |
| `docs`        | Doxygen / Sphinx / MkDocs 目标    |

## 头文件卫生

| 工具                | 开关                            | 说明                                                                                                                |
| ------------------- | ------------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| **IWYU**            | `CXX_ENABLE_IWYU=ON`            | `CXX_INCLUDE_WHAT_YOU_USE`；可选 `CMAKE_CXX_INCLUDE_WHAT_YOU_USE`（`CXX_IWYU_AS_CMAKE_VAR`）；映射 `cmake/iwyu.imp` |
| **include-cleaner** | `CXX_ENABLE_INCLUDE_CLEANER=ON` | tidy `misc-include-cleaner` + 目标 `include-cleaner-check`；`.clangd` 已开 Strict Unused/Missing                    |
| 手动                |                                 | `cmake --build . --target iwyu-check` / `include-cleaner-check`                                                     |

模块模板：IWYU / include-cleaner **不对 `import` 图做合并扫描**；仅传统 `.hxx/.cxx` 消费者有意义。

## 微基准 & Fuzz

```bash
brew install google-benchmark
cmake --build . --target run-bench          # 短时冒烟
cmake --preset fuzz && cmake --build --preset fuzz
cmake --build build/fuzz --target run-fuzz  # corpus + max_total_time
```

- Bench：`ItemsProcessed` / `BytesProcessed` 便于回归；CI 可加 `--benchmark_out=`
- Fuzz：种子在 `fuzz/corpus/fuzz_parse_price/`

## API 文档（Doxygen → Breathe → Sphinx / MkDocs）

```bash
brew install doxygen
pip install -r docs/requirements-docs.txt
cmake .. -DCXX_ENABLE_DOCS=ON
cmake --build . --target docs          # HTML + XML
cmake --build . --target docs-sphinx   # 需 breathe
cmake --build . --target docs-mkdocs   # 轻量站点
```

## 安全意识（非认证）

见 [`SAFETY_AWARENESS.md`](SAFETY_AWARENESS.md) 与追溯草稿 [`traceability/TRACEABILITY_MATRIX.md`](traceability/TRACEABILITY_MATRIX.md)。

```bash
cmake --preset safety-lint && cmake --build --preset safety-lint --target tidy-check
```

**不做：** 全面禁异常/禁堆、商业 MISRA 检查器、DO-178C 证据包（另立 safety 模板）。

## 其它开关

| Cache 变量               | 默认   | 说明                                          |
| ------------------------ | ------ | --------------------------------------------- |
| `CXX_ENABLE_CCACHE`      | ON     | 编译缓存                                      |
| `CXX_ENABLE_PCH`         | OFF    | 预编译头                                      |
| `CXX_ENABLE_UNITY_BUILD` | OFF    | Unity（仅本模板）                             |
| `CXX_USE_LLD`            | OFF    | `-fuse-ld=lld`                                |
| `CXX_ALLOCATOR`          | system | mimalloc/jemalloc；Sanitizer/Fuzz 强制 system |
| `CXX_SANITIZER`          | （空） | `address,undefined` / `thread`                |

## 依赖

- Homebrew：`spdlog` / `googletest` / 可选 `google-benchmark` / `mimalloc` / `include-what-you-use` / `doxygen`
- 可选：`vcpkg.json` + toolchain

## 质量门

- `.clangd` · `.pre-commit-config.yaml` · `compile_commands.json` 根软链
- `tidy-check` / `syntax-check` / `run-bench` / `run-fuzz` / `docs`
