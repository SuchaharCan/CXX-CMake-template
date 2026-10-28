# 模板功能路线图 — single-exe-classic

> 约束：**单可执行文件、无自编译库**（不含库导出/安装、ABI 稳定、动态链接 API 等）。
> 参考：LLVM/CMake、Google（abseil/grpc）、Chromium、Rust 生态等成熟工程实践。
> 标注：✅ 已有 · 🔜 建议优先补 · ⭐ 进阶可选项。

## 1. 构建与依赖

| 功能 | 状态 | 说明 / 工具 |
|------|------|-------------|
| 多方案配置（Presets） | ✅ | `CMakePresets.json`（dev/debug/release/fuzz/docs/gcc） |
| 编译器发现 + 最小版本门禁 | ✅ | `00_core_compiler` / `00_core_deps`（brew 升级自愈） |
| 编译缓存 | ✅ | ccache（可选，默认开启） |
| 依赖检测集中管理 | ✅ | `00_core_deps.cmake`（cmake/ninja/llvm/gcc/spdlog/fmt/bqlog/just） |
| 构建任务自动化 | ✅ | `justfile`（build/test/docs/check/coverage/fuzz…） |
| 依赖包管理（只消费不编译） | 🔜 | vcpkg / Conan / FetchContent 三选一统一入口 |
| 可复现构建 + SBOM | 🔜 | 锁定依赖版本；SPDX/CycloneDX 产物 |
| 发布优化流水线 | ⭐ | LTO / PGO / BOLT |

## 2. 代码质量与静态分析

| 功能 | 状态 | 说明 / 工具 |
|------|------|-------------|
| 格式门禁 | ✅ | clang-format（PRE_BUILD dry-run + `just fmt` 自动化） |
| 静态分析 | ✅ | clang-tidy（编译期挂载 + tidy-check） |
| Include 卫生 | ✅ | IWYU / clang include-cleaner（可选） |
| 补充独立检查器 | 🔜 | cppcheck（跨编译器语义检查） |
| CI 级安全扫描 | ⭐ | CodeQL / Semgrep / SonarQube |
| 复杂度阈值 | ⭐ | 圈复杂度 / 认知复杂度（lizard 等） |
| 提交规范 | 🔜 | Conventional Commits + commitlint |
| 本地钩子 | 🔜 | pre-commit（历史曾内置，按需恢复） |
| 版权/许可证头 | 🔜 | REUSE / SkyWalking-eyes |

## 3. 测试体系

| 功能 | 状态 | 说明 / 工具 |
|------|------|-------------|
| 单元测试 | ✅ | GTest + CTest + gtest_discover_tests（`just test`） |
| 属性测试 | ✅ | RapidCheck（可选） |
| 模糊测试 | ✅ | libFuzzer + 语料库 + `just fuzz` |
| 内存/并发检查 | ✅ | ASan/UBSan/TSan（Preset 化） |
| 覆盖率 | ✅ | llvm-cov 插桩 + `just coverage` |
| 覆盖率门禁 | 🔜 | 覆盖率报告 + 最低阈值拦截（CI 中） |
| 变异测试 | ⭐ | mull / carp |
| 基准即回归 | 🔜 | `run-bench` 接入 CI 对比基线 |

## 4. CI/CD 与工程流

| 功能 | 状态 | 说明 / 工具 |
|------|------|-------------|
| 多平台矩阵 | 🔜 | GitHub Actions / GitLab CI（macOS/Ubuntu/Windows） |
| PR 一键全检 | 🔜 | fmt + tidy + test + coverage + sanitizer |
| 自动版本与变更日志 | 🔜 | release-please / semantic-release |
| 产物签名发布 | ⭐ | 二进制 + 校验和 + cosign |
| 夜间/长期回归 | ⭐ | 定时流水线 |

## 5. 运行时工程与可观测性

| 功能 | 状态 | 说明 / 工具 |
|------|------|-------------|
| 结构化日志 | ✅ | spdlog（常规运行日志） |
| 高吞吐 I/O/崩溃日志 | ✅ | bqlog（三模式接入） |
| CLI 规范 | 🔜 | CLI11：--help/--version、退出码约定 |
| 配置分层 | 🔜 | 默认值 < 配置文件 < 环境变量（dotenv） |
| 崩溃报告 | ⭐ | crashpad / sentry-native + core dump 管理 |
| 指标与追踪 | ⭐ | Prometheus exposition / Tracy |
| 优雅退出 | 🔜 | 信号处理、资源清理、SIGINT/SIGTERM |

## 6. 性能工程

| 功能 | 状态 | 说明 / 工具 |
|------|------|-------------|
| 微基准 | ✅ | google-benchmark + `just bench` |
| 剖析目标 | 🔜 | perf / Instruments 火焰图流程化 |
| 发布优化 | ⭐ | PGO/LTO/BOLT（同 §1） |

## 7. 安全

| 功能 | 状态 | 说明 / 工具 |
|------|------|-------------|
| 内存安全 | ✅ | Sanitizer + Fuzzing 常态化 |
| 依赖漏洞扫描 | 🔜 | OSV-Scanner / Dependabot |
| 供应链完整性 | ⭐ | SBOM + SLSA provenance |
| 加固编译选项 | 🔜 | PIE / 栈保护 / FORTIFY |
| 平台签名公证 | ⭐ | macOS hardened runtime + notarization / Windows 签名 |

## 8. 文档与开发者体验

| 功能 | 状态 | 说明 / 工具 |
|------|------|-------------|
| API 文档双后端 | ✅ | clang-doc + Doxygen（`just docs`） |
| 架构与变更管理 | ✅ | README（架构说明 + 重大变更记录） |
| CLI 帮助页 | 🔜 | --help / man page（随 §5 CLI11） |
| 决策记录 | 🔜 | ADR（docs/adr/） |
| 贡献指南/行为准则 | 🔜 | CONTRIBUTING / CODE_OF_CONDUCT |
| 容器化开发环境 | ⭐ | devcontainer / Dockerfile |
| 换行与编码锁定 | 🔜 | .gitattributes（LF）、EditorConfig |
| Issue/PR 模板 | 🔜 | .github/ISSUE_TEMPLATE、徽章 |

## 9. 发布与分发

| 功能 | 状态 | 说明 / 工具 |
|------|------|-------------|
| 安装规则 | 🔜 | `install(TARGETS ... RUNTIME DESTINATION bin)`（单 exe 也建议） |
| 版本注入 | 🔜 | project() 版本 → 生成 version.h（单一事实来源） |
| 多渠道分发 | ⭐ | Homebrew formula / 二进制 Release / 包管理器 |

## 10. 刻意排除（与模板约束冲突）

- 自编译库、库导出（install(EXPORT)/Config 包）、ABI 稳定性策略
- 动态链接 API 设计、插件接口
- 多可执行/多组件编排（属于其他模板）
