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
bash ci/run-local-ci.sh
bash ci/run-template-matrix.sh # 全量本地 CI（等价 workflow，自动清理 build、隔离日志）
# 快速子集
CI_JUST_RECIPES="build test" bash ci/run-template-matrix.sh
# 在模板目录内
cd single-exe-classic
just local-ci
just build            # auto：macOS = clang++ + libc++

cd ../single-exe-modules
just local-ci
just build
just build-clang      # 显式 clang++ + libc++
```

| 平台 / 模板   | 默认             | 备选                                     | 说明                              |
| ------------- | ---------------- | ---------------------------------------- | --------------------------------- |
| macOS classic | clang++ + libc++ | g++ + libstdc++（需自建 libstdc++ 依赖） | Homebrew 依赖都是 libc++ ABI      |
| Linux classic | g++ + libstdc++  | clang++ + libc++（CI 构建配套依赖）      | 与 apt 依赖一致                   |
| Linux modules | clang++ + libc++ | 无（必须）                               | `import std` 使用 libc++ std.cppm |
