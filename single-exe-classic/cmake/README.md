# cmake/ — single-exe-classic 模板基建（脚本独立 / 功能不耦合）

根 `CMakeLists.txt` 只保留：**业务源列表 + add_executable + 调用 phase API**。

## 模块地图（文件头注释里有完整依赖说明）

| 文件 | 分层 | 职责 | 上游 |
|------|------|------|------|
| `00_core_compiler.cmake` | core 必选 | 编译器/工具链发现（clang/gcc）、opt 软链自愈、libc++ runtime | 无（独立） |
| `00_core_color.cmake` | core 必选 | 调色板解析 + cxx_msg + cxx_sep/cxx_banner 分隔符 | 无（独立，读 cxx.palette） |
| `00_core_log.cmake` | core 必选 | log/ 文件日志（CXX_LOG_DIR / cxx_flog / 裁剪） | 无（独立） |
| `00_core_deps.cmake` | core 必选 | cmake/ninja/llvm/gcc/spdlog/fmt/bqlog 检测与最小版本；bqlog 三模式接入 | compiler + color + log |
| `00_core_buildlog.cmake` | core 必选 | build tee + grc 着色管道 + 长命令换行 | color + log |
| `00_core_profile.cmake` | core 必选 | Debug/Release 编译画像 | 无（独立） |
| `00_core_lint.cmake` | core 必选 | 警告探测 + clang-tidy + clang-format + tidy 配置解析 | compiler + color + buildlog |
| `10_core_setup.cmake` | core 必选 | 编排器：bootstrap / phase / 一键门禁 / 可选模块自动发现 | 以上全部 core |
| `9*_opt_*.cmake` | opt 可选 | 各可选功能，文件末尾自注册 gate/hook | 各自头注释标注 |
| `cxx.palette` | 配置 | 调色板（名称/语义/ANSI/grc 配色） | 无 |
| `cxx_build_colorize.inc.sh` | 管道 | 构建输出换行格式化 + grcat 着色 | 由 tee 注入 CXX_GRCAT/CXX_GRC_CONF |

## 可选模块

`90_opt_ccache` / `90_opt_gtest`(默认 OFF，手动 `-DCXX_ENABLE_GTEST=ON`) /
`91_opt_sanitizers` / `91_opt_coverage` / `92_opt_docs`(clang-doc + Doxygen 双后端) /
`93_opt_benchmark` / `93_opt_fuzz` / `94_opt_iwyu` / `94_opt_include_cleaner` /
`95_opt_pch` / `95_opt_unity` / `96_opt_linker`(lld) / `96_opt_allocator` /
`97_opt_compile_commands` / `97_opt_rapidcheck`

## 所有权（改哪里）

| 关心点 | 文件 |
|--------|------|
| 编译器选择、LLVM/GCC 路径 | `00_core_compiler.cmake` |
| 颜色 / 分隔符 / 横幅 | `00_core_color.cmake` + `cxx.palette` |
| 构建输出着色规则 | `00_core_buildlog.cmake` + `cxx.palette`（生成 build/cxx_grc.conf） |
| 依赖检测、bqlog/spdlog 接入 | `00_core_deps.cmake` |
| 警告 / tidy / format 门禁 | `00_core_lint.cmake` |
| 阶段编排、一键门禁 | `10_core_setup.cmake` |

## 依赖规则

- 每个文件头部声明：独立与否、上游文件、下游文件、功能作用。
- 叶子不 include 编排；`00_core_compiler` 不依赖任何模块。
- **新增可选功能**：新建 `9*_opt_*.cmake` + 自注册 gate/hook，无需改编排器。
