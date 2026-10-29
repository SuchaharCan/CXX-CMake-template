# single-exe-static-modules — 多独立静态模块 + 单 main

Stacks_Tools 重构模板：**每个功能模块都是独立 STATIC 库**，main 只做单层 `import` 和 CLI 路由；模块之间不互相 import，只通过明确的公共模块（`InterfaceLog`）协作。

> 完整设计文档（设计原则 → API → 错误表 → UB 规避 → 测试策略）：[docs/DESIGN_DOC.md](docs/DESIGN_DOC.md)。
>
> > `tldr` 直接在 该文档同级目录下 输入 `just docs`运行 点击打印输出Doxygen 指向的文件路径中 的`index.html`中直接查看文档
> > **该架构的测试函数 和 模块是 作为我个人为Stacks2 编写的Alfred插件的核心功能**

## 架构

```
InterfaceLog (STATIC)   bqlog C++ 模块日志 + 最多 1000 条环形接口记录
ProjectSync  (STATIC)   项目/Stack 扫描与索引
TaskManager  (STATIC)   任务创建 / 20 字符 ID 生成
Stacks_Tools (exe)      单 main：import 三个模块 + CLI 路由
```

- `ProjectSync` / `TaskManager` 都只依赖 `InterfaceLog`，彼此没有依赖。
- `InterfaceLog` 是唯一直接使用 bqlog 的模块；业务模块通过接口日志记录输入/输出。
- 所有模块库均为 `add_library(... STATIC)`，使用 `FILE_SET CXX_MODULES`。
- std 模块使用本机 LLVM 自带的 `std.cppm` 自编译（`cxx_std_module`）。

## 平台约束

- macOS only：**Ninja + CMake ≥4.4.4 + LLVM/Clang++ ≥23.1 + C++26 + libc++**。
- bqlog 必须使用 C++ 模块模式：`import bq.log;`。
- spdlog 未提供 C++ 模块接口，本模板**不使用 spdlog**；日志全部走 bqlog 模块。
- bqlog 接口日志保留在 `log/interface.log`，同时内存环形缓冲最多保留 1000 条，超出淘汰最旧记录。

## 使用

```bash
cd single-exe-static-modules
just build                 # auto = clang++ + libc++
just test                  # 边界 + 并行 + 日志容量测试
just parallel-test         # CTest -j8 + 线程并行测试
just mutation-test         # 两个真实变异体必须被测试杀死
just docs                  # 注释文档生成（clang-doc + Doxygen 双后端）
just check                 # clang-format（模块 TU 不做 clang-tidy）
just local-ci              # 本机复现 GitHub Actions macOS 矩阵
```

所有会编译的 recipe（含测试、文档和变异测试）都启用 ccache，并在末尾打印
独立缓存命中率；也可运行 `just ccache-stats`。`just docs` 会以粗体黄色输出
Doxygen 与 clang-doc 的可点击 `index.html` 链接。

配置日志由两个 CMake BOOL 变量控制，模板头文件输入为
`cmake/single-exe-static-modules.h.in`：

| 变量 | 默认值 | 控制内容 |
| --- | --- | --- |
| `CXX_SHOW_CMAKE_CONFIG` | `OFF` | `CMake 配置` 分段、`configure_file` 消息、`Performing Test ...` warning 探测 |
| `CXX_SHOW_CMAKE_VARIABLES` | `ON` | `=== 2. CMake 变量 ===` 分组变量清单 |

```bash
just configure -DCXX_SHOW_CMAKE_CONFIG=ON
just configure -DCXX_SHOW_CMAKE_VARIABLES=OFF
```

配置后生成 `build/CMakeTutorialConfig.hxx`；开关只控制日志，不跳过文件生成。

CLI：

```bash
./build/Stacks_Tools sync /path/to/StacksLibrary
./build/Stacks_Tools add <project_or_stack_id> "title|description"
./build/Stacks_Tools list /path/to/demo.project
```

## 测试覆盖

- 边界：空输入、缺失/非法 JSON、缺失搜索路径、未知目标、Project/Stack 头插、
  兜底插入、任务文件元数据、`for_each_*` 函数对象遍历、mtime 格式、错误枚举全覆盖。
- 并行：8 线程生成唯一 ID、8 线程并发写接口日志且容量仍为 1000。
- 日志：1200 次记录后只保留最后 1000 条，bqlog 文件存在且有内容。
- 变异：`kTaskIdLength` 20→19、backlog 头插→尾插，两个 mutant 都必须被测试杀死。

## 设计原则

接口遵循博客参考的现代化 API 设计（强类型、`std::expected` 错误分类、函数对象遍历、
无 UB、C++26 初始化纪律），逐条映射见 [docs/DESIGN_DOC.md](docs/DESIGN_DOC.md)。

## CI

GitHub Actions 暂时只保留 macOS Job；Linux-Ubuntu Job 已在
`.github/workflows/template-matrix.yml` 中用 `if: false` 暂时禁用。
普通 push/PR 只构建变更模板，手动触发才执行完整 recipe 矩阵。

本地复现：

```bash
bash ci/run-local-ci.sh --changed
bash ci/run-local-ci.sh --all
```

## 本轮变更记录（2026-10-29）

- **增量 CI**：push/PR 只构建差异涉及的模板，手动触发仍运行完整矩阵；共享 CI 变更回退全量。
- **配置输出**：头文件输入改为 `cmake/single-exe-static-modules.h.in`，补齐 `configure_file`；新增 `CXX_SHOW_CMAKE_CONFIG=OFF` 与 `CXX_SHOW_CMAKE_VARIABLES=ON` 两个日志开关。
- **文档与缓存**：`just docs` 输出两条粗体黄色可点击 HTML 链接；测试、文档和变异测试均启用 ccache 并打印统计；Actions Job 结束前清理工作树生成物。
