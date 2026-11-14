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
- `ci/install-deps-macos.sh` — macOS 依赖安装/升级（cmake≥4.5.0、clang≥23.1 等）
- `ci/install-libcxx-deps.sh` — Linux modules 专用的 libc++ 第三方依赖（spdlog/fmt/gtest/benchmark）
- `ci/run-template-matrix.sh` — 模板矩阵执行器
- `ci/select-changed-templates.sh` — Git 差异到模板目录的映射器
- `ci/cleanup-job-workspace.sh` — Actions Job 完成前的安全工作区清理
- `.github/workflows/docs-pages.yml` — 构建全部模板 Doxygen + clang-doc 并发布 GitHub Pages
- `ci/build-pages-docs.sh` — 汇总三个模板到单一 Pages 站点
- `ci/pages-templates.tsv` — Pages 首页模板清单与说明维护点
- `ci/site-nav.html`、`ci/site-nav.css` — 详情页公共导航与首页博客入口
- `ci/umami.html` — 发布站点共用的 Umami 追踪片段

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

bash ci/build-pages-docs.sh build/pages-site  # 本地生成完整 Pages 站点
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
   - 左侧选择 `Template API Docs Pages`，可查看或手动重跑 Pages 构建与部署。

4. 触发方式：
   - 向 `master`/`main` 推送，或创建/更新 Pull Request（自动触发）。
   - Actions 页面 → `Template Matrix CI` → **Run workflow**（手动触发）。

5. 建议开启分支保护（可选）：**Settings → Branches → Add branch protection rule**，
   勾选 **Require status checks to pass before merging**，选择
   `Detect changed templates` 与 `macOS / macos-26`。

## GitHub Pages 文档

Pages 使用**方案 A**：每次构建全部模板 Doxygen 与 clang-doc，生成一个完整静态
artifact，再由 `actions/deploy-pages` 原子发布。生成 HTML 不进入源码分支。

URL 结构固定为：

```text
https://suchaharcan.github.io/CXX-CMake-template/
https://suchaharcan.github.io/CXX-CMake-template/single-exe-classic/
https://suchaharcan.github.io/CXX-CMake-template/single-exe-modules/
https://suchaharcan.github.io/CXX-CMake-template/single-exe-static-modules/
https://suchaharcan.github.io/CXX-CMake-template/single-exe-classic/clang-doc/
https://suchaharcan.github.io/CXX-CMake-template/single-exe-modules/clang-doc/
https://suchaharcan.github.io/CXX-CMake-template/single-exe-static-modules/clang-doc/
```

兼容规则：

- `/<template>/` 永久保留为 Doxygen 入口，已有博客/外部链接无需修改；
- clang-doc 只新增 `/<template>/clang-doc/`，不能占用旧路径；
- 两种文档的共享导航提供双向切换，深层页面使用相对路径返回对应入口；
- `ci/verify-pages-site.sh` 会在上传前验证全部旧入口和新增入口；
- `actions/deploy-pages` 只在构建和验证全部成功后运行，失败时线上继续提供
  上一成功 artifact，不会用半成品覆盖现有文档。

新增模板时：

1. 在 `ci/pages-templates.tsv` 增加一行 `模板目录<TAB>说明`；
2. 确保模板有 CMakeLists.txt、`docs-doxygen` 与 `docs-clangdoc` target；
3. C++ 模块模板应设置 `CXX_DOC_CLANGDOC_BUILD_TARGETS` 和
   `CXX_DOC_CLANGDOC_SOURCES`，先构建模块映射再运行 clang-doc；
4. 推送 `master`，`Template API Docs Pages` 自动重建总站。

### Umami 访问统计

`ci/build-pages-docs.sh` 会在站点全部生成后，把 `ci/umami.html` 注入每个 HTML
页面的 `</head>` 前。覆盖范围包括：

- Pages 根入口 `index.html`；
- 三个模板的 Doxygen 与 clang-doc 首页、类/函数/文件等全部 HTML 子页面；
- 后续按 `ci/pages-templates.tsv` 新增模板生成的两套文档页面。

当前片段复用博客的 Umami Website ID，并以 `data-domains` 将采集限制在
`suchaharcan.github.io`；`data-exclude-hash` 与参考站保持一致，不把 Doxygen
页内锚点拆成独立访问。更换统计属性或域名时只修改这一个文件。GitHub Pages
当前未设置 CSP；若以后增加 CSP，需要同时允许 `script-src cloud.umami.is`
和连接 `cloud.umami.is`。

发布前本地验证：

```bash
bash ci/build-pages-docs.sh build/pages-site
bash ci/verify-pages-site.sh build/pages-site
test -f build/pages-site/.nojekyll
test -f build/pages-site/index.html
test -f build/pages-site/single-exe-classic/index.html
test -f build/pages-site/single-exe-modules/index.html
test -f build/pages-site/single-exe-static-modules/index.html
test -f build/pages-site/single-exe-classic/clang-doc/index.html
test -f build/pages-site/single-exe-modules/clang-doc/index.html
test -f build/pages-site/single-exe-static-modules/clang-doc/index.html
rg -q '返回模板集合' build/pages-site/single-exe-static-modules/index.html
rg -q '返回模板集合' build/pages-site/single-exe-static-modules/clang-doc/index.html
rg -q '03Share_blog' build/pages-site/index.html
rg -q 'cloud.umami.is/script.js' build/pages-site/index.html
rg -q 'cloud.umami.is/script.js' build/pages-site/single-exe-classic/index.html
```

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

- Ubuntu：cmake≥4.5.0（**Kitware 官方二进制，不使用 Python/pip**）、GCC 16（Ubuntu Toolchain PPA）、clang 23.1 + libc++（apt.llvm.org，含运行库）、ninja、just、grc、ccache、spdlog/fmt/gtest/benchmark、doxygen。
- **C++ 运行时策略**：
  - `single-exe-classic` 在 Linux 使用系统 **libstdc++**（与 apt 的 spdlog/fmt/gtest/benchmark 一致）；
  - `single-exe-modules` 需要 libc++（`import std` 由 CMake 4.5 原生处理），CI 用
    `ci/install-libcxx-deps.sh` 构建配套的 libc++ 版本依赖；
  - macOS 两模板都使用 Homebrew libc++ 依赖，无此问题。
- **编译器 auto 顺序**：
  - macOS：`clang++ + libc++`（Homebrew 依赖 ABI 固定；g++ 需自建 libstdc++ 依赖）
  - Linux：`g++ + libstdc++` → `clang++ + libc++`
  - modules 模板固定 `clang++ + libc++`；GCC 不支持 `-stdlib=libc++`。
- macOS：Homebrew 安装/升级 cmake≥4.5.0（索引过旧无 4.5.0 bottle 时回退安装 Kitware 官方二进制）、clang≥23.1、ninja、just、grc、ccache、spdlog/fmt/gtest/benchmark、doxygen、graphviz、bqlog。
- `bqlog` 是可选依赖；GitHub macOS Homebrew 若没有该 formula，会 warning 并跳过，不会让流水线失败。
- BqLog 官方安装方式为 `brew install bqlog`，同时支持 **macOS 与 Linux Homebrew**
  （Linuxbrew 默认前缀 `/home/linuxbrew/.linuxbrew`）。CI 会：
  - macOS：本地 brew 索引缺少 `bqlog` 时先 `brew update`，再 `brew install bqlog`；
  - Linux：若存在 Linuxbrew（`brew` 命令或 `/home/linuxbrew/.linuxbrew/bin/brew`）则同样安装 bqlog；
  - 两者都不可用时仅 warning，模板自动跳过 bqlog 接入。
- 非标准安装路径：`-DCXX_BQLOG_ROOT=/path/to/bqlog` 或环境变量 `BQLOG_ROOT`。
- `bqlog` 在 Ubuntu 缺失时模板自动 warning 并跳过，不影响构建与测试。
