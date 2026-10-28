# 00CMake-Template

多模板 CMake 工程集合。每个模板目录自成一体，包含 `justfile`，可用 just 直接构建。

| 模板                 | 说明                                                               |
| -------------------- | ------------------------------------------------------------------ |
| `single-exe-classic` | 经典 `.hxx/.cxx` 单可执行（cmake/ 分层 + grc 着色 + bqlog）        |
| `single-exe-modules` | C++23 模块单可执行（`import std` + 自研模块 + CMake 原生模块扫描） |

## CI（GitHub Actions）

- `.github/workflows/template-matrix.yml` — GitHub Actions：`ubuntu-24.04` + `macos-26` 双环境矩阵
- `ci/run-template-matrix.sh` — 自动发现所有含 `justfile` 的模板，逐个执行 just 命令并汇总结果
- `ci/install-deps-ubuntu.sh` / `ci/install-deps-macos.sh` — 两平台依赖安装
- 启用步骤、macOS 27 自建 runner 与本地验证见 [`ci/README.md`](ci/README.md)

本地验证：

```bash
bash ci/run-template-matrix.sh
CI_JUST_RECIPES="build test" bash ci/run-template-matrix.sh
```
