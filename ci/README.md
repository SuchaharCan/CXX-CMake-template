# CI — 模板矩阵流水线（GitHub Actions）

## 设计

- 自动发现仓库内**直接包含 `justfile`** 的模板目录，不维护模板白名单；
  新增模板会自动纳入变更检测。
- `push` / `pull_request` 只构建本次差异触及的模板，默认执行 `just build`，
  不再运行所有模板和全部 recipe。
- 模板目录内任一文件变更都会选中该模板；`ci/*.sh`、
  `.github/workflows/template-matrix.yml`、`.github/actions/**` 属于共享
  CI，变更时安全回退为构建全部模板。`ci/README.md` 等纯文档不触发构建。
- `workflow_dispatch` 保留全量模式，对全部模板执行：
  `env list build test parallel-test docs bench fuzz coverage iwyu mutation-test verbose`
  （模板未定义的命令自动跳过）。
- `just local-ci` 和 `just check` 在 CI 矩阵中强制跳过；即使
  `CI_JUST_RECIPES` 或 `CI_CHANGED_RECIPES` 误包含这两个 recipe，也不会执行。
- 增量模式始终要求模板定义 `build` recipe；不会因为命令未定义而被静默跳过，
  从而保证“成功”代表模板至少完成了一次编译。
- `clean` 不自动执行（会删除构建目录）；可用 `CI_JUST_RECIPES` 覆盖命令集合。
- 增量 recipe 可用 Repository Variable `CI_CHANGED_RECIPES` 覆盖（默认 `build`）。
- 所有会编译的 just recipe 固定传 `-DCXX_ENABLE_CCACHE=ON`；CI 使用
  `actions/cache` 跨运行恢复 ccache，并在 recipe 末尾单独打印缓存命中率。
- Job 最后执行 `ci/cleanup-job-workspace.sh`，清理 build、日志、依赖产物等
  工作树生成物；源码和跨任务 ccache/工具链缓存保留。
- 任一命令失败会继续执行其余命令，结尾汇总失败项并返回非零退出码。
- 日志会作为 Actions artifact 上传（7 天保留）。
- 每条命令输出保存到 `.ci-logs/<模板>-<命令>.log`；失败汇总会打印命令与末尾 30 行错误输出。

## 工作流文件

- `.github/workflows/template-matrix.yml`
  - `detect`：拉取完整历史，按 push/PR 的 base/head 差异选择模板
  - `linux`：暂时 `if: false` 禁用（恢复时改为判断选中模板非空）
  - `macos`：仅在选中模板非空时运行；`macos-26`（arm64，Homebrew）
- `ci/install-deps-ubuntu.sh` — Ubuntu 依赖安装（兼容 GitHub runner 的 sudo）
- `ci/install-deps-macos.sh` — macOS 依赖安装/升级（cmake≥4.4.4、clang≥23.1 等）
- `ci/install-libcxx-deps.sh` — Linux modules 专用的 libc++ 第三方依赖（spdlog/fmt/gtest/benchmark）
- `ci/run-template-matrix.sh` — 模板矩阵执行器
- `ci/select-changed-templates.sh` — Git 差异到模板目录的映射器
- `ci/cleanup-job-workspace.sh` — Actions Job 完成前的安全工作区清理

> `justfile` 使用 `bash`（Ubuntu/macOS 默认可用），`just fmt` 通过
> `CLANG_FORMAT` 环境变量（默认 `clang-format`）定位格式化工具；
> 两个 CI Job 都显式设置了绝对路径。

## 本地验证

```bash
CI_DEPS_CHECK_ONLY=1 bash ci/install-deps-macos.sh   # macOS 只检查
bash ci/run-local-ci.sh                              # 本机全量矩阵（兼容原行为）
bash ci/run-local-ci.sh --all                        # 明确执行全量矩阵
bash ci/run-local-ci.sh --changed                    # 只 build HEAD^..HEAD 影响模板
bash ci/run-local-ci.sh --changed origin/master      # 只 build 分支相对 master 的变更
just local-ci                                        # 模板目录内运行全量矩阵

bash ci/select-changed-templates.sh --all            # 列出全部模板
bash ci/select-changed-templates.sh --base HEAD^     # 列出增量模板
bash ci/run-template-matrix.sh                       # 未指定模板时运行全量
CI_TEMPLATES="single-exe-classic" CI_JUST_RECIPES="build" \
  bash ci/run-template-matrix.sh                     # 只构建一个模板
CI_JUST_RECIPES="build test" bash ci/run-template-matrix.sh  # 快速子集
```

`select-changed-templates.sh` 将模板名逐行写到 stdout，诊断信息写到 stderr；
CI 中再由 workflow 转成一行，通过 `CI_TEMPLATES` 传给矩阵执行器。

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
   - 左侧选择 `Template Matrix CI`，可看到模板检测、`Linux` 和 `macOS` Job。

4. 触发方式：
   - 向 `master`/`main` 推送，或创建/更新 Pull Request（自动触发）。
   - Actions 页面 → `Template Matrix CI` → **Run workflow**（手动触发）。

5. 建议开启分支保护（可选）：**Settings → Branches → Add branch protection rule**，
   勾选 **Require status checks to pass before merging**，选择
   `Detect changed templates` 与 `macOS / macos-26`。

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

- Ubuntu：cmake≥4.4.4（**Kitware 官方二进制，不使用 Python/pip**）、GCC 16（Ubuntu Toolchain PPA）、clang 23.1 + libc++（apt.llvm.org，含运行库）、ninja、just、grc、ccache、spdlog/fmt/gtest/benchmark、doxygen。
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
