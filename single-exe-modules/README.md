# single-exe-modules — C++23 模块单可执行 CMake 模板

纯 **C++23 模块**（`export module` + `import std`）单可执行工程：`airline_ticket` + `modern_paradigm` 两个自研模块，GTest / Benchmark / libFuzzer 均为模块消费者。本文件是模板唯一主文档：**架构设计说明 + 重大变更管理**。

## 1. 架构设计

### 1.1 目录与职责

| 路径 | 职责 |
|------|------|
| `cmake/00_core_compiler.cmake` | 工具链发现（clang 23.1 门禁）+ 强制 Ninja + scan-deps 软链自愈 |
| `cmake/00_core_modules.cmake` | 模块 API：`cxx_add_module` / `cxx_add_module_consumer` |
| `cmake/00_core_color/log/deps/buildlog/profile/lint` | 与 single-exe-classic 同构（调色板 / 日志 / 依赖 / grc 着色 / 语法规范） |
| `cmake/10_core_setup.cmake` | 编排器；自动发现 `cmake/9*_opt_*.cmake` |
| `cmake/cxx.palette` / `cxx_highlight.rules` | 调色板与构建输出高亮规则表 |
| `justfile` | just 自动化（just 必选依赖） |
| `src/` | 模块源（`.cppm`）与消费者（`.cxx`） |
| `bench/` `fuzz/` `docs/` `build/` `log/` | 基准 / fuzz 语料 / 文档 / 构建产物 / 日志 |

### 1.2 模块构建与查找方式（更稳定）

- **std 模块**：普通 TU 的 `import std` 走 CMake 原生支持；命名模块接口单元内的 `import std` 由 `00_core_modules` 把 clang 自带 `std.cppm`（`<llvm>/share/libc++/v1/`）编译为显式目标 `cxx_std_module`（`-w` + 私有 include 目录），std 模块与工具链版本严格一致。
- **自研模块查找**：`cxx_add_module` 使用 `FILE_SET CXX_MODULES`，依赖扫描走 CMake 原生 `clang-scan-deps`（其路径由 `00_core_compiler` 的 opt 软链自愈保障），不再手工拼 `.pcm/.o`。
- **生成器**：模块模板强制 Ninja（`cmake -G Ninja ..`），just 已内置。

### 1.3 命名与依赖约定

- `00_core_*` / `10_core_*` 必选；`9*_opt_*` 可选（`-D` 手动开启或自动探测）。
- 每个文件头部声明独立与否、上游、下游、功能；叶子不 include 编排。
- **可扩展点**：新增 `9*_opt_*.cmake` 并自注册 `cxx_register_apply_gate` / `cxx_register_finish_hook`。
- 颜色语义与 just 注释配色（yellow）与 single-exe-classic 一致。

### 1.4 日志分工

| 库 | 用途 | 文件 |
|----|------|------|
| spdlog | 常规运行日志 | `log/exe.log` / `log/lldb_debug.log` |
| bqlog | 运行期 I/O、闪退报错 | bqlog 自管二进制日志（`-DCXX_BQLOG_MODE=header|library|module`） |

## 2. 快速开始

> **目录约定**：`just` 命令在模板根目录执行；手动 cmake 先 `cd build`。

```bash
just            # 基础构建（cmake -G Ninja .. && cmake --build . -j9）
just list       # 全部命令（注释 yellow）
just check      # clang-tidy + clang-format
just test       # 单元测试（自动开启 GTest）
just docs       # clang-doc + Doxygen
just bench      # 微基准冒烟
just fuzz       # libFuzzer 冒烟
just coverage   # llvm-cov
just iwyu       # IWYU + include-cleaner
```

原生 cmake：`cd build && cmake -G Ninja .. && cmake --build . && ./Ticket`

## 3. 依赖与版本门禁

必选：`cmake ≥4.4.4`、`ninja`、`clang++ 23.1`（`import std` 需 Clang ≥17，模板锁定 23.1）、`just`、`spdlog`、`grc`；推荐 `bqlog`。

bqlog 官方安装方式：`brew install bqlog`（macOS 与 Linux Homebrew 均可；
Linuxbrew 前缀通常为 `/home/linuxbrew/.linuxbrew`）。非标准安装可用
`-DCXX_BQLOG_ROOT=/path/to/bqlog` 或环境变量 `BQLOG_ROOT` 指定。
可选：`googletest`（`-DCXX_ENABLE_GTEST=ON`）、`ccache`、`google-benchmark`、`doxygen`、`clang-doc`。

## 4. 质量门禁

| 工具 | 查什么 | 怎么跑 |
|------|--------|--------|
| clang-tidy | 静态问题 | `just check` / `--target tidy-check` |
| clang-format | 排版（含 .cppm） | `just fmt` / `--target syntax-check` |
| GTest | 业务边界 | `just test` |
| clang-doc + Doxygen | 注释文档 | `just docs` |

## 5. 重大变更记录

| 日期 | 变更 |
|------|------|
| 2026-10-28 | **v2 重构（对齐 single-exe-classic）**：cmake/ 分层 `00_core/10_core/9*_opt`；`import std` 改 CMake 原生（移除 Generator/StdlibConfig/StdModule/Modules/stdlib 手工编译）；cmake ≥4.4.4、clang ≥23.1；grc 着色 + 规则表；bqlog 三模式；justfile 全命令；GTest 默认 OFF；文档合并为 README |
| 2026-10-28 | 清理无关文件（vcpkg.json、UserPresets.example、.pre-commit、源根 compile_commands.json、ARCHITECTURE.md）；`.clangd` 指向 `build/`；`.gitignore` 三分区 |

## 6. 相关文档

- [`cmake/README.md`](cmake/README.md) — 模块地图与所有权
- [`docs/ENGINEERING.md`](docs/ENGINEERING.md) — 工程化工具链
- [`docs/TEMPLATE_ROADMAP.md`](docs/TEMPLATE_ROADMAP.md) — 模板功能路线图
- [`docs/SAFETY_AWARENESS.md`](docs/SAFETY_AWARENESS.md) — 安全意识边界
