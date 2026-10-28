# CI — 模板矩阵流水线（GitHub Actions）

## 设计

- 自动发现仓库内所有**直接包含 `justfile`** 的模板目录（新增模板自动纳入）。
- 每个模板执行：`env list build check test docs bench fuzz coverage iwyu verbose`。
- `clean` 不自动执行（会删除构建目录）；可用 `CI_JUST_RECIPES` 覆盖命令集合。
- 任一命令失败会继续执行其余命令，结尾汇总失败项并返回非零退出码。
- 日志会作为 Actions artifact 上传（7 天保留）。
- 每条命令输出保存到 `.ci-logs/<模板>-<命令>.log`；失败汇总会打印命令与末尾 30 行错误输出。

## 工作流文件

- `.github/workflows/template-matrix.yml`
  - `linux`：`ubuntu-24.04`（apt/LLVM 23.1/clang 23 + libc++）
  - `macos`：`macos-26`（GitHub 最新托管 macOS，arm64，Homebrew）
- `ci/install-deps-ubuntu.sh` — Ubuntu 依赖安装（兼容 GitHub runner 的 sudo）
- `ci/install-deps-macos.sh` — macOS 依赖安装/升级（cmake≥4.4.4、clang≥23.1 等）
- `ci/install-libcxx-deps.sh` — Linux modules 专用的 libc++ 第三方依赖（spdlog/fmt/gtest/benchmark）
- `ci/run-template-matrix.sh` — 模板矩阵执行器

> `justfile` 使用 `bash`（Ubuntu/macOS 默认可用），`just fmt` 通过
> `CLANG_FORMAT` 环境变量（默认 `clang-format`）定位格式化工具；
> 两个 CI Job 都显式设置了绝对路径。

## 本地验证

```bash
CI_DEPS_CHECK_ONLY=1 bash ci/install-deps-macos.sh   # macOS 只检查
bash ci/run-local-ci.sh                              # 本机复现 GitHub Actions 全量矩阵
just local-ci                                        # 同上（模板目录内）
bash ci/run-template-matrix.sh                       # 全量矩阵
CI_JUST_RECIPES="build test" bash ci/run-template-matrix.sh  # 快速子集
```

## GitHub 上如何开启

1. **提交并推送**（workflow 必须在默认分支上）：

```bash
git add .
git commit -m "ci: add GitHub Actions template matrix"
git push -u origin master
```

2. 打开仓库 **Settings → Actions → General**：
   - Actions permissions 选择 **Allow all actions and reusable workflows**（需要 `actions/checkout` 与 `actions/upload-artifact`）。
   - Workflow permissions 保持 **Read repository contents** 即可。
   - 保存。

3. 打开仓库 **Actions** 标签：
   - 若是首次启用，点击 **I understand my workflows, go ahead and enable them**。
   - 左侧选择 `Template Matrix CI`，可看到 `Linux` / `macOS` 两个 Job。

4. 触发方式：
   - 向 `master`/`main` 推送，或创建/更新 Pull Request（自动触发）。
   - Actions 页面 → `Template Matrix CI` → **Run workflow**（手动触发）。

5. 建议开启分支保护（可选）：**Settings → Branches → Add branch protection rule**，
   勾选 **Require status checks to pass before merging**，选择
   `Linux / ubuntu-24.04` 与 `macOS / macos-26`。

## macOS 27 说明

GitHub 官方托管 macOS 目前最新是 **macOS 26**（`macos-26`，arm64）；
`macos-27` 尚无托管镜像。需要严格 macOS 27 时：

1. 在 macOS 27 机器注册 **self-hosted runner**：
   **Settings → Actions → Runners → New self-hosted runner**，按其命令注册，
   添加自定义标签 `macos-27`。
2. 把工作流 macOS Job 的 `runs-on` 改为：

```yaml
runs-on: [self-hosted, macos-27]
```

3. 其余步骤不变（`ci/install-deps-macos.sh` 会自动补依赖）。

## 依赖说明

- Ubuntu：cmake≥4.4.4（**Kitware 官方二进制，不使用 Python/pip**）、clang 23.1 + libc++（apt.llvm.org，含运行库）、ninja、just、grc、ccache、spdlog/fmt/gtest/benchmark、doxygen。
- **C++ 运行时策略**：
  - `single-exe-classic` 在 Linux 使用系统 **libstdc++**（与 apt 的 spdlog/fmt/gtest/benchmark 一致）；
  - `single-exe-modules` 需要 libc++（`import std` 使用 libc++ 的 std.cppm），CI 用
    `ci/install-libcxx-deps.sh` 构建配套的 libc++ 版本依赖；
  - macOS 两模板都使用 Homebrew libc++ 依赖，无此问题。
- **编译器 auto 顺序**：
  - macOS：`clang++ + libc++`（Homebrew 依赖 ABI 固定；g++ 需自建 libstdc++ 依赖）
  - Linux：`g++ + libstdc++` → `clang++ + libc++`
  - modules 模板固定 `clang++ + libc++`；GCC 不支持 `-stdlib=libc++`。
- macOS：Homebrew 安装/升级 cmake≥4.4.4、clang≥23.1、ninja、just、grc、ccache、spdlog/fmt/gtest/benchmark、doxygen、bqlog。
- `bqlog` 是可选依赖；GitHub macOS Homebrew 若没有该 formula，会 warning 并跳过，不会让流水线失败。
- BqLog 官方安装方式为 `brew install bqlog`，同时支持 **macOS 与 Linux Homebrew**
  （Linuxbrew 默认前缀 `/home/linuxbrew/.linuxbrew`）。CI 会：
  - macOS：本地 brew 索引缺少 `bqlog` 时先 `brew update`，再 `brew install bqlog`；
  - Linux：若存在 Linuxbrew（`brew` 命令或 `/home/linuxbrew/.linuxbrew/bin/brew`）则同样安装 bqlog；
  - 两者都不可用时仅 warning，模板自动跳过 bqlog 接入。
- 非标准安装路径：`-DCXX_BQLOG_ROOT=/path/to/bqlog` 或环境变量 `BQLOG_ROOT`。
- `bqlog` 在 Ubuntu 缺失时模板自动 warning 并跳过，不影响构建与测试。
