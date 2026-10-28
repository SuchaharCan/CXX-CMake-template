# cmake/ — single-exe-modules 模板基建（脚本独立 / 功能不耦合）

根 `CMakeLists.txt` 只保留：**模块源列表 + add targets + 调用 phase API**。

## 模块地图（文件头注释里有完整依赖说明）

| 文件 | 分层 | 职责 | 上游 |
|------|------|------|------|
| `00_core_compiler.cmake` | core 必选 | 工具链发现（clang≥23.1）+ 强制 Ninja + scan-deps 自愈 | 无（独立） |
| `00_core_modules.cmake` | core 必选 | 模块 API：cxx_add_module / cxx_add_module_consumer | compiler |
| `00_core_color.cmake` | core 必选 | 调色板 + cxx_msg + cxx_sep/cxx_banner | 无（独立） |
| `00_core_log.cmake` | core 必选 | log/ 文件日志 | 无（独立） |
| `00_core_deps.cmake` | core 必选 | cmake≥4.4.4/ninja/clang≥23.1/just/spdlog/fmt/bqlog 检测 | compiler+color+log |
| `00_core_buildlog.cmake` | core 必选 | build tee + grc 着色 + 长命令换行 | color+log |
| `00_core_profile.cmake` | core 必选 | Debug/Release 编译画像 | 无（独立） |
| `00_core_lint.cmake` | core 必选 | 警告探测 + clang-tidy + clang-format（含 .cppm） | compiler+color+buildlog |
| `10_core_setup.cmake` | core 必选 | 编排器 + 可选模块自动发现 | 以上全部 core |
| `9*_opt_*.cmake` | opt 可选 | 各可选功能，自注册 gate/hook | 头注释标注 |
| `cxx.palette` / `cxx_highlight.rules` | 配置 | 调色板 / 高亮规则表 | 无 |
| `cxx_build_colorize.inc.sh` | 管道 | 换行格式化 + grcat 着色 | 由 tee 注入 |

## 可选模块

`90_opt_ccache` / `90_opt_gtest`(默认 OFF) / `91_opt_sanitizers` / `91_opt_coverage` /
`92_opt_docs` / `93_opt_benchmark` / `93_opt_fuzz` / `94_opt_iwyu` / `94_opt_include_cleaner` /
`95_opt_pch`(仅经典 TU) / `96_opt_linker` / `96_opt_allocator` / `97_opt_compile_commands` / `97_opt_rapidcheck`

## 与 single-exe-classic 的差异

- 新增 `00_core_modules.cmake`；移除 unity build（模块不适用）。
- `import std` 原生支持，无 stdlib/ 目录；`CMAKE_CXX_STANDARD=23`；强制 Ninja。

## 依赖规则

- 每个文件头部声明：独立与否、上游文件、下游文件、功能作用。
- 新增可选功能：新建 `9*_opt_*.cmake` + 自注册，无需改编排器。
