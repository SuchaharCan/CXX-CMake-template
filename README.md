# 00CMake-Template

多模板 CMake 工程集合。每个模板目录自成一体，包含 `justfile`，可用 just 直接构建。

| 模板                 | 说明                                                               |
| -------------------- | ------------------------------------------------------------------ |
| `single-exe-classic` | 经典 `.hxx/.cxx` 单可执行（cmake/ 分层 + grc 着色 + bqlog）        |
| `single-exe-modules` | C++23 模块单可执行（`import std` + 自研模块 + CMake 原生模块扫描） |
| `single-exe-static-modules` | 多独立 STATIC 模块 + 单 main（bqlog 模块日志 + 边界/并行/变异测试） |

## Apple container 构建环境

镜像定义位于 [`container/Dockerfile`](container/Dockerfile)，面向 Apple Silicon
（`linux/arm64`），镜像包含：

- Ubuntu 24.04 + Ubuntu Toolchain PPA 当前可用的 GCC 16（`gcc` / `g++`）
- apt.llvm.org 的 LLVM/Clang 23.1、libc++、LLD、LLDB 与常用分析工具
- CMake 4.4.5、Ninja、just、ccache、grc、Doxygen
- 与 libc++ ABI 匹配的 spdlog/fmt/GTest/Benchmark，供模块模板直接使用

### 路径与职责

| 变量 | 路径 | 用途 |
| --- | --- | --- |
| `PROJECT_ROOT` | `${home}/00CMake-Template` | 源码、Dockerfile、编译产物 |
| `IMAGE_HOME` | `${home}/00Docker_Containers/cmake_template_builder` | 镜像 tar 归档 |
| `IMAGE_REF` | `local/cmake-template-ubuntu24:latest` | Apple container 本地镜像名 |
| `IMAGE_ARCHIVE` | `$IMAGE_HOME/cmake-template-ubuntu24.tar` | 可恢复的镜像归档文件 |

Apple container 的实际镜像库由 container 服务统一维护，不能直接指定到
`IMAGE_HOME`。`IMAGE_HOME` 只保存通过 `container image save` 导出的镜像归档；
源码始终从 `PROJECT_ROOT` 挂载到容器的 `/workspace`。

### 1. 初始化当前终端

变量只在当前 shell 有效。新开终端后需要重新执行：

```bash
export PROJECT_ROOT=${home}/00CMake-Template
export IMAGE_HOME=${home}/00Docker_Containers/cmake_template_builder
export IMAGE_REF=local/cmake-template-ubuntu24:latest
export IMAGE_ARCHIVE="$IMAGE_HOME/cmake-template-ubuntu24.tar"
export GITHUB_PROXY=https://gh-proxy.org

mkdir -p "$IMAGE_HOME"
cd "$PROJECT_ROOT"

test -f container/Dockerfile
container system start
```

首次启动 Apple container 时，如果提示安装 Linux 内核，输入 `Y`。

### 2. 国内镜像预拉取

Apple container 没有 Docker Desktop 那种透明的 registry mirror 自动回退。
国内的 `[registry].domain` 只影响未写明 registry 的镜像名，不能自动改写
Dockerfile 中的 `docker.io/...` 或 Apple 内置的 `ghcr.io/...`。

当前环境需要预拉取三类镜像：

| 来源 | 官方引用 | 国内代理 |
| --- | --- | --- |
| GHCR Builder | `ghcr.io/apple/container-builder-shim/builder:0.13.2` | `ghcr.nju.edu.cn/apple/container-builder-shim/builder:0.13.2` |
| GHCR vminit | `ghcr.io/apple/containerization/vminit:0.53.0` | `ghcr.nju.edu.cn/apple/containerization/vminit:0.53.0` |
| Docker Hub | `docker.io/library/ubuntu:24.04` | `docker.m.daocloud.io/library/ubuntu:24.04` |

#### 2.1 加速 builder 和 vminit

`0.13.2` 和 `0.53.0` 是 Apple container 1.6.1 的默认版本。升级 Apple container
后，可先通过 `container system property list` 确认是否需要更新版本号。

```bash
# 约 101 MiB
container image pull \
  --platform linux/arm64 \
  --max-concurrent-downloads 8 \
  ghcr.nju.edu.cn/apple/container-builder-shim/builder:0.13.2

container image tag \
  ghcr.nju.edu.cn/apple/container-builder-shim/builder:0.13.2 \
  ghcr.io/apple/container-builder-shim/builder:0.13.2

container image pull \
  --platform linux/arm64 \
  --max-concurrent-downloads 8 \
  ghcr.nju.edu.cn/apple/containerization/vminit:0.53.0

container image tag \
  ghcr.nju.edu.cn/apple/containerization/vminit:0.53.0 \
  ghcr.io/apple/containerization/vminit:0.53.0
```

#### 2.2 加速 Ubuntu 基础镜像

```bash
container image pull \
  --platform linux/arm64 \
  --max-concurrent-downloads 8 \
  docker.m.daocloud.io/library/ubuntu:24.04

container image tag \
  docker.m.daocloud.io/library/ubuntu:24.04 \
  docker.io/library/ubuntu:24.04
```

DaoCloud 不可用时，可替换为以下备用代理，再执行同样的 `image tag`：

```text
docker.1ms.run/library/ubuntu:24.04
```

> 国内 registry 代理并非 Apple 官方服务。当前已验证上述代理返回与上游相同摘要的
> ARM64 镜像；不要通过公共代理拉取私有镜像。

#### 2.3 加速 GitHub 文件下载

builder 容器内可能出现 `Could not resolve host: github.com`。CMake、just 以及
libc++ 配套依赖的源码都从 GitHub 下载，因此 Dockerfile 增加了
`GITHUB_PROXY` 构建参数，默认值为 `https://gh-proxy.org`。

第 3 步的构建命令会通过
`--build-arg GITHUB_PROXY="${GITHUB_PROXY:-https://gh-proxy.org}"` 显式传入代理地址，
因此变量未设置时也会自动回退到 `gh-proxy.org`。

备用代理可设为 `https://ghfast.top` 或 `https://gh-proxy.com`。这些是第三方
下载代理，仅支持公开资源；不使用代理时传入
`--build-arg GITHUB_PROXY=`。

### 3. 构建镜像

预拉取基础镜像后，**不要再给 `container build` 添加 `--pull`**，否则 BuildKit
会强制重新访问 Docker Hub，可能再次出现 `HTTPClientError.connectTimeout`。

```bash
cd "$PROJECT_ROOT"

container builder start --cpus 8 --memory 16g

container build \
  --file container/Dockerfile \
  --tag "$IMAGE_REF" \
  --build-arg GITHUB_PROXY="${GITHUB_PROXY:-https://gh-proxy.org}" \
  --platform linux/arm64 \
  --cpus 8 \
  --memory 16g \
  .
```

构建完成后归档到 `00Docker_Containers`：

```bash
container image save \
  --output "$IMAGE_ARCHIVE" \
  "$IMAGE_REF"

container image list | grep 'cmake-template-ubuntu24'
```

本机镜像被删除后，可恢复归档：

```bash
container image load --input "$IMAGE_ARCHIVE"
```

### 4. 创建开发容器

```bash
container create \
  --name cmake-template-dev \
  --platform linux/arm64 \
  --cpus 8 \
  --memory 16g \
  --volume "$PROJECT_ROOT:/workspace" \
  --workdir /workspace \
  "$IMAGE_REF"
```

创建后不会自动运行；首次启动并进入 shell：

```bash
container start cmake-template-dev
container exec \
  --interactive \
  --tty \
  --workdir /workspace \
  cmake-template-dev \
  bash
```

### 5. 日常启动与停止

```bash
# 新开终端或重启 macOS 后
container system start
container start cmake-template-dev
container exec --interactive --tty --workdir /workspace cmake-template-dev bash

# 容器内退出 shell；容器继续在后台运行
exit

# 暂停容器和 Apple container 服务
container stop cmake-template-dev
container system stop
```

### 6. 在容器内编译模板

```bash
# 查看实际工具链版本
container exec cmake-template-dev bash -lc \
  'gcc --version; g++ --version; clang++ --version; cmake --version; just --version'

# classic：Linux 默认 g++ + libstdc++
container exec cmake-template-dev bash -lc \
  'cd /workspace/single-exe-classic && just build'

# modules：clang++ + 镜像预装的 libc++ 依赖
container exec cmake-template-dev bash -lc \
  'cd /workspace/single-exe-modules && just build'
```

停止或重建容器不会丢失 `PROJECT_ROOT` 中的源码和构建目录。修改
`container/Dockerfile` 或 `ci/install-deps-ubuntu.sh` 后，需要重新构建并归档镜像，
然后重建开发容器：

```bash
container stop cmake-template-dev
container delete cmake-template-dev
# 重新执行第 3 步和第 4 步
```

### 7. 常见问题

#### `file does not exist container/Dockerfile`

新开终端后变量不会自动保留，而且相对路径依赖当前目录：

```bash
export PROJECT_ROOT=${home}/00CMake-Template
cd "$PROJECT_ROOT"
test -f container/Dockerfile
```

#### `POSIXErrorCode(rawValue: 61): Connection refused`

通常是 container 服务仍继承了已经关闭的本地代理。先停止服务，再用无代理环境启动：

```bash
container system stop

env \
  -u HTTP_PROXY \
  -u HTTPS_PROXY \
  -u http_proxy \
  -u https_proxy \
  -u ALL_PROXY \
  -u all_proxy \
  container system start
```

检查服务配置中是否还有失效代理：

```bash
plutil -p "$HOME/Library/Application Support/com.apple.container/apiserver/apiserver.plist" \
  | grep -i proxy || echo "proxy: clean"
```

#### Docker Hub 下载超时

重新执行第 2.2 节，通过国内代理拉取 Ubuntu 并 tag 回
`docker.io/library/ubuntu:24.04`，然后直接执行第 3 步，不要使用 `--pull`。

#### `Could not resolve host: github.com`

确认当前终端设置了 `GITHUB_PROXY`，并使用第 3 步中的
`--build-arg`。如果当前代理不可用，可替换为
`https://ghfast.top` 或 `https://gh-proxy.com` 后重新构建。

> OCI registry 镜像加速只处理镜像拉取。Dockerfile 内部的 `archive.ubuntu.com`、
> `ppa.launchpadcontent.net`、`apt.llvm.org` 和 GitHub 下载仍使用各自网络路径；
> 如果构建停在这些步骤，应根据实际报错单独调整 APT 或 HTTPS 代理。

## CI（GitHub Actions）

- `.github/workflows/template-matrix.yml` — push/PR 按 Git 差异只构建变更模板；
  手动触发才执行全部模板的完整 recipe 矩阵
- `ci/select-changed-templates.sh` — 将 Git 变更映射到模板目录；共享 CI 变更回退全量
- `ci/run-template-matrix.sh` — 对选中的模板执行 just 命令并汇总结果
- `ci/cleanup-job-workspace.sh` — Job 结束前清理工作树中的 build、日志与依赖产物
- `ci/install-deps-ubuntu.sh` / `ci/install-deps-macos.sh` — 两平台依赖安装
- 启用步骤、macOS 27 自建 runner 与本地验证见 [`ci/README.md`](ci/README.md)

本地验证：

```bash
bash ci/run-local-ci.sh              # 全量（兼容原行为）
bash ci/run-local-ci.sh --changed    # 只 build HEAD^..HEAD 影响的模板
bash ci/run-local-ci.sh --all        # 明确执行全量
bash ci/select-changed-templates.sh --base origin/master  # 查看增量模板
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
