# single-exe-classic — 可复用单可执行程序 CMake 模板

经典 **`.hxx` / `.cxx`**（无 C++ 模块）。工具链路径动态发现；稳定逻辑在 `cmake/*.cmake`，根 `CMakeLists.txt` 只改项目元数据与源文件列表。

本文件是模板唯一的主文档：**架构设计说明 + 重大变更管理**。

> 模板优点：
>
> > 1.  clang-formate, clang-tidy 语法格式自动化 编码规范 - 提高代码可维护 和 质量

2. 注释文档生成自动化 clang-docs, Doxygen 两种 ，一个llvm内置，一个成熟可靠
3. justfile 自动编译构建，摆脱构建困难
4. 着色增强， 打印输出美化，亮色提醒 关键词
5.

## 1. 架构设计

### 1.1 目录与职责

| 路径                              | 职责                                                                              |
| --------------------------------- | --------------------------------------------------------------------------------- |
| `cmake/00_core_*.cmake`           | 必选核心模块（编译器 / 调色板 / 日志 / 依赖检测 / 构建着色 / 语法规范 / profile） |
| `cmake/10_core_setup.cmake`       | 必选编排器（bootstrap / phase / 一键门禁）                                        |
| `cmake/9*_opt_*.cmake`            | 可选功能模块，由 setup 自动发现（`file(GLOB CONFIGURE_DEPENDS)`）                 |
| `cmake/cxx.palette`               | 统一调色板（名称/语义/终端 ANSI/grc 配色）                                        |
| `cmake/cxx_highlight.rules`       | 构建输出高亮规则表（正则 / 颜色 / 次数，加关键字只改这里）                        |
| `cmake/cxx_build_colorize.inc.sh` | 构建输出管道（长命令换行 + 绝对路径缩短 + grcat 着色）                            |
| `justfile`                        | 构建自动化（just 必选依赖）                                                       |
| `src/` `bench/` `fuzz/` `docs/`   | 业务 / 基准 / fuzz / 文档素材                                                     |
| `build/` `log/`                   | 构建产物 / 日志（git 忽略）                                                       |

### 1.2 加载分层

```
A 00_core_compiler.cmake   # pre-project，独立
B project()
C 00_core_color / 00_core_log / 00_core_deps / 00_core_buildlog / 00_core_profile / 00_core_lint
D 10_core_setup.cmake      # bootstrap 时自动加载 cmake/9*_opt_*.cmake
E 业务源 + cxx_apply_template_defaults(target)
```

### 1.3 命名与依赖约定

- `00_core_*` / `10_core_*`：必选；`9*_opt_*`：可选（`-D` 手动开启或自动探测）。
- 数字前缀保证耦合文件排序相邻（如 `91_opt_sanitizers` 与 `91_opt_coverage`）。
- 每个文件头部声明：独立与否、上游文件、下游文件、功能作用。
- 叶子不 include 编排；`00_core_compiler` 不依赖任何模块。
- **可扩展点**：新增 `9*_opt_*.cmake` 并在末尾自注册 `cxx_register_apply_gate(fn)`（每 target 门禁）或 `cxx_register_finish_hook(fn)`（收尾目标），无需改编排器。

### 1.4 颜色语义（cmake/cxx.palette）

red=错误/失败 · orange=严重警告 · yellow=警告 · green=成功 · grass=通过/次级成功 · blue=依赖检测 · purple=语法检测 · cyan=路径/信息 · white=横幅/高亮 · gray=次要信息 · comment=注释说明（RGB 223,174,0，调色板语义色）。just 命令注释的展示色 = yellow（见 §2.1）。

配置期消息由 `00_core_color.cmake` 消费调色板；构建期关键字由 `00_core_buildlog.cmake` 读 `cxx_highlight.rules` 生成 `build/cxx_grc.conf`，交 grcat（`brew install grc`）着色。构建输出的绝对路径自动缩短为 `build` / `.`（仅终端显示，`log/cmake_build.log` 保留原文）。

### 1.5 日志分工

| 库     | 用途                     | 文件                                 |
| ------ | ------------------------ | ------------------------------------ |
| spdlog | 常规运行日志             | `log/exe.log` / `log/lldb_debug.log` |
| bqlog  | 程序运行期 I/O、闪退报错 | bqlog 自管二进制日志（三种接入见下） |

bqlog 接入：`-DCXX_BQLOG_MODE=header`（默认，头文件 `BQ_LATE_BINDING` 免链接）/ `library`（`-DCXX_BQLOG_LINK=static|shared`）/ `module`（`import bq.log;`）。

## 2. 快速开始

### just（推荐）

```bash
just            # 基础构建（cmake .. && cmake --build . -j9）
just list       # 全部命令（注释以 yellow 显示）
just env        # 必选系统环境变量
just fmt        # 源码自动格式化（clang-format -i，仅格式化 src/ 下精准检测到的源码）
just check      # 语法检测（tidy-check + syntax-check）
just test       # 单元测试（自动开启 GTest）
just docs       # clang-doc + Doxygen
just bench      # 微基准冒烟
just fuzz       # libFuzzer 冒烟
just coverage   # llvm-cov 覆盖率
just iwyu       # IWYU + include-cleaner
just verbose    # 详细构建（命令逐参数换行）
```

> **目录约定**：`just` 系列命令直接在模板根目录执行（justfile 会自动定位 `build/` 并完成 `cmake ..` + `cmake --build`）。手动执行 cmake 时，先 `cd build` 进入构建目录，再 `cmake .. && cmake --build . -j9`。`just build` 在默认开启 clang-format 时会先自动执行 `clang-format -i` 格式化源码（先精准检测 `src/` 下的 `.cxx/.hxx/.cpp/.hpp`，再做格式自动化），确保 PRE_BUILD 的 dry-run 门禁不会因格式问题失败。

### 2.1 just 注释配色（yellow）

just 原生可配置的颜色只有 `--command-color`（env `JUST_COMMAND_COLOR`，本模板已在 justfile 导出为 `yellow`，作用于配方命令行回显；TTY 下自动生效，管道场景需再加 `JUST_COLOR=always`）。`just --list` 的 `#` 注释蓝色是 just 硬编码的，无配置入口；本模板提供两种 yellow 方案：

- **推荐**：`just list` 配方已输出 yellow 注释（`just --list --color always` 后剥离内置蓝并统一上 `1;33`）。
- **原生 `just --list` 也想要 yellow**：在 `~/.zshrc` 加包装函数：

```zsh
just() {
  if [[ "${1:-}" == "--list" && -z "${2:-}" ]]; then
    command just --list --color always | perl -ne \
      'if (/(.*?)(#.*)/) { ($a,$c)=($1,$2); $c =~ s/\e\[[0-9;]*m//g; print "$a\e[1;33m$c\e[0m\n"; } else { print; }'
  else
    command just "$@"
  fi
}
```

### 原生 cmake

```bash
cd build
cmake .. && cmake --build . -j9       # 配置 + 构建
./step1 100
ctest --output-on-failure             # 需 -DCXX_ENABLE_GTEST=ON
```

| 你改了什么                                   | 需要                            |
| -------------------------------------------- | ------------------------------- |
| `src/` 业务源码                              | 仅 `cmake --build .`            |
| `CMakeLists.txt` / `cmake/*` / Preset / `-D` | `cmake ..` 后再 build           |
| 切换 `clang++` ↔ `g++`                       | 建议清空 `build/` 后 `cmake ..` |

## 3. 依赖

必选：`cmake`、`ninja`、`just`、`llvm`(clang++)、`spdlog`、`grc`；推荐 `bqlog`（缺失不阻断构建，只跳过接入）。

bqlog 官方安装方式：`brew install bqlog`（macOS 与 Linux Homebrew 均可；
Linuxbrew 前缀通常为 `/home/linuxbrew/.linuxbrew`）。非标准安装可用
`-DCXX_BQLOG_ROOT=/path/to/bqlog` 或环境变量 `BQLOG_ROOT` 指定。

标准库策略：macOS 使用 libc++；Linux 使用系统默认 libstdc++
（与 apt 的 spdlog/fmt/gtest/benchmark 一致）。需要 libc++ 时加
`-DCXX_FORCE_LIBCXX=ON`，并准备 libc++ 版本的第三方依赖。
可选：`googletest`（`-DCXX_ENABLE_GTEST=ON`）、`ccache`、`google-benchmark`、`mimalloc/jemalloc`、`doxygen`、`clang-doc`（随 llvm）。

## 4. 质量门禁

| 工具                | 查什么       | 怎么跑                                 |
| ------------------- | ------------ | -------------------------------------- |
| clang-tidy          | 静态代码问题 | `just check` / `--target tidy-check`   |
| clang-format        | 排版/风格    | `just check` / `--target syntax-check` |
| GTest               | 业务边界     | `just test` / `ctest`                  |
| clang-doc + Doxygen | 注释文档     | `just docs` / `--target docs`          |

## 5. 重大变更记录

| 日期       | 变更                                                                                                                                                                                                                                                               |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 2026-10-28 | .gitignore 三分区重写（必填文件清单 / log .gitkeep 占位 / 排除项分类）；新增 docs/TEMPLATE_ROADMAP.md 功能路线图 |
| 2026-10-28 | **模板更名**：`00_0_Single_Exe` → `single-exe-classic`（经典头文件单可执行；为后续更多模板预留命名空间）；manifest/README/feedback 同步更新                                                                                                                        |
| 2026-10-28 | **构建系统 v2 重构**：cmake/ 分层为 `00_core_*`/`10_core_*`/`9*_opt_*`；着色引擎切换为 grc（grcat）+ `cxx_highlight.rules` 规则表；分隔符支持注释（`cxx_sep`/`cxx_banner`）；bqlog 三模式接入；GTest 默认 OFF（手动 `-DCXX_ENABLE_GTEST=ON`）；文档合并为本 README |
| 2026-10-28 | 引入 `just` 为必选依赖并添加 justfile（构建/检测/测试/文档自动化）                                                                                                                                                                                                 |
| 2026-10-28 | just 注释展示色定为 yellow：justfile 设 `JUST_COMMAND_COLOR=yellow`，`just list` 配方与原生 `just --list` 的 zsh 包装方案均输出黄色注释（just 内置蓝无配置入口）；223,174,0 保留为调色板 `comment` 语义色                                                          |
| 2026-10-28 | `just build` 默认前置源码自动格式化（检测 `CXX_ENABLE_CLANG_FORMAT` 后 `clang-format -i`）；新增 `just fmt` 配方                                                                                                                                                   |
| 2026-10-28 | 文档明确执行目录约定：just 在模板根目录执行，手动 cmake 在 build 目录执行                                                                                                                                                                                          |
| 2026-10-28 | 清理无关文件：`vcpkg.json`、`CMakeUserPresets.json.example`、`.pre-commit-config.yaml`、源根 `compile_commands.json`（clangd 改用 `.clangd` 的 `CompilationDatabase: build/`）                                                                                     |
| 2026-10-28 | 构建输出：绝对路径缩短 + `cd … &&` 命令链逐参数换行格式化                                                                                                                                                                                                          |
| 2026-10-28 | 修复 brew 升级 llvm 后 `clang-scan-deps` 缓存失效（opt 软链自愈，见 `00_core_compiler.cmake`）                                                                                                                                                                     |

1. 着色增强 + 字符串匹配 维护方便cxx_highlight.rules 文件中维护

## 6. 相关文档

- [`cmake/README.md`](cmake/README.md) — CMake 模块地图与所有权
- [`docs/ENGINEERING.md`](docs/ENGINEERING.md) — 工程化工具链
- [`docs/SAFETY_AWARENESS.md`](docs/SAFETY_AWARENESS.md) — 安全意识边界
- [`docs/TEMPLATE_ROADMAP.md`](docs/TEMPLATE_ROADMAP.md) — 模板功能路线图（已有 / 建议 / 进阶）
