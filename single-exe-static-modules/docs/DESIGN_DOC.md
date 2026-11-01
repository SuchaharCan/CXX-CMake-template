# Omni-Stacks 架构设计与接口文档

> 本文档是 `single-exe-static-modules` 实际应用的权威架构说明，并作为
> Doxygen HTML 主页。业务到 CRUD 的逐项映射见
> [BUSINESS_API.md](BUSINESS_API.md)。

## 1. 模板定位

工程名为 `OmniStacksStaticModules`，对外二进制保持 Alfred 运行契约
`Stacks_Tools`。项目由一个可执行文件和五个独立 STATIC C++20 模块组成：

```text
┌──────────────────────────────────────────────────────────────┐
│ Stacks_Tools（单 main）                                      │
│ 运行目录发现 · CLI 路由 · 启动日志 · stdout/stderr 边界      │
└──────────────────────────┬───────────────────────────────────┘
                           │ 单层 import
┌──────────────────────────▼───────────────────────────────────┐
│ WorkflowUI（STATIC）                                         │
│ Alfred JSON · 层级导航 · action token · 双语反馈             │
└──────────────┬──────────────────┬────────────────────────────┘
               │                  │
┌──────────────▼────────┐  ┌──────▼──────────────┐
│ ProjectSync           │  │ ReminderSync        │
│ 工作区扫描/索引       │  │ Reminders/OmniFocus │
└──────────────┬────────┘  └──────┬──────────────┘
               │                  │
┌──────────────▼──────────────────▼────────────┐
│ TaskManager                                  │
│ 任务 CRUD · 日期 · 完成 · 本周 · 计时 · 删除│
└──────────────────────┬───────────────────────┘
                       │
┌──────────────────────▼───────────────────────┐
│ InterfaceLog                                 │
│ bqlog 文件后端 · 1000 条内存环形缓冲         │
└──────────────────────────────────────────────┘
```

所有模块是一份源码内的独立静态库；不生成额外动态库。发布时只携带
`Stacks_Tools` 单二进制和 Alfred 资源文件。

## 2. 依赖规则

| 模块 | 对外职责 | 允许依赖 |
| --- | --- | --- |
| `InterfaceLog` | 结构化接口日志、bqlog、日志清理 | `std`、`bq.log` |
| `ProjectSync` | `.project` 扫描、Project/Stack 索引 | `std`、`InterfaceLog` |
| `TaskManager` | 任务文件 CRUD、日期和周查询、计时 | `std`、`InterfaceLog` |
| `ReminderSync` | AppleScript 参数和外部应用同步 | `std`、`InterfaceLog` |
| `WorkflowUI` | Alfred 表现层和 action 分发 | 上述领域模块 |
| `Stacks_Tools` | CLI 入口 | 全部导出模块 |

约束：

- 领域模块不反向依赖 `WorkflowUI` 或 `main`；
- `ProjectSync` 与 `TaskManager` 不直接互相 `import`，避免生命周期耦合；
- 只有 `InterfaceLog` 直接 `import bq.log`，统一日志后端可替换；
- 可恢复错误通过 `std::expected<T, Error>` 返回，API 边界不抛业务异常；
- `main` 不解析 Stacks JSON，也不实现任务写回规则。

## 3. 启动与运行生命周期

`Stacks_Tools` 每次由 Alfred 启动，按照固定顺序初始化：

1. 无参数时打印 CLI 帮助并返回使用错误；
2. `discover_runtime(argv[0])` 判断发布目录或源码目录；
3. `ensure_runtime_layout` 创建缺失的 `data/`、`log/` 和三个空 JSON；
4. `STACKS_LOGGING` 决定是否启用 bqlog 和 trace；
5. 读取 `Project.json.library_paths[0]` 作为默认 Inbox 工作区；
6. 路由 `ui`、`action` 或调试 CLI；
7. 统一返回 `0` 成功、`1` 用法错误、`2` 业务/IO 错误。

发布包不包含 `data/` 与 `log/`，因此同一二进制既可在源码树运行，也可在 Alfred
workflow 根目录首次自举。

## 4. 模块设计

### 4.1 InterfaceLog

- `kMaxLines = 1000`，内存队列超出后从队首淘汰；
- `init` 创建 1 MiB 文本 appender，并写入开发者网站水印；
- `record` 在单锁内同时维护内存记录与 bqlog，文件异常不破坏业务返回；
- `set_enabled` 响应 `STACKS_LOGGING`；关闭时不写内存和文件；
- `cleanup_old_logs` 只删除超过三个月的 `interface.log_YYYYMMDD_*`、
  `trace-YYYYMMDD.*` 和旧 trace，不批量删除无关文件；
- 文件名带路径哈希，避免并行测试共享 bqlog logger。

### 4.2 ProjectSync

`sync_all(search_path, DbPaths)` 的同步阶段：

1. 读取历史 `library_paths` 并规范化绝对路径；
2. 校验新路径存在且是目录，加入路径集合；
3. 递归扫描 `.project`，按绝对路径排序后解析；
4. 生成 Project 节点和扁平的 Project/Stack 节点；
5. 单坏项目记录到 `failed_projects`，继续扫描其他项目；
6. 两个索引通过临时文件 + `rename(2)` 原子替换。

`Project.json` 保存工作区与项目文件定位；`Stack.json` 保存 `kind`、
`projectid`、`project_path`、`library_path`，供 Alfred 三级导航和任务创建。

### 4.3 TaskManager

公开输入使用强类型 `TargetId`、`TaskId`、`TaskTitle`、`TaskDescription`；
批量参数使用 `DatabasePaths`、`CreateTaskOptions`、`TaskPatch` 结构体。

主要事务：

| 操作 | 写入顺序 | 失败处理 |
| --- | --- | --- |
| 看板创建 | `.task` → `.project` | 删除新 `.task` |
| Inbox 创建 | `.task` | 返回 `write_failure` |
| 任务编辑 | 临时文件 + rename | 原文件保持完整 |
| 标记完成 | `done/progress/completed` 同文件 | 原子替换 |
| 停止计时 | `.task.timelogs` → `Timers.json` | 第二步失败恢复任务 JSON |
| 删除任务 | 全部 `.project` 去引用 → `.trash` | 任意失败恢复项目快照 |

任务 ID 为 20 字符：8 个 steady-clock 位 + 12 个随机位。所有写操作使用
`data/.stacks-tools.lock` 排他锁；读取 `timelogs` 前保留完整原始 JSON，以支持
跨文件回滚且不丢失 Stacks 未知字段。

### 4.4 ReminderSync

- `Provider` 支持 `reminders`、`omnifocus`、`both`；
- 同步前校验任务 ID 和 `YYYY-MM-DD` 截止日期；
- 用户标题/描述作为独立 argv 传入 `osascript`，不经过 shell；
- 备注/正文追加 `[Stacks-ID:<id>]` 去重标记；
- `dry-run` 只生成结果，不启动外部应用；
- stdout/stderr 分别通过 pipe 读取，退出码映射为可恢复错误。

### 4.5 WorkflowUI

`filter` 和 `action` 是两个稳定入口：

```text
Stacks_Tools ui "<query>"       → Alfred Script Filter JSON
Stacks_Tools action "<token>"   → 领域操作结果
```

UI 层级：

1. `s`：不可选操作说明 + 一级命令 + 项目/清单入口；
2. 项目/清单：模糊过滤，回车进入任务级；
3. 任务：详情、编辑、日期、完成、计时、同步、归档、删除；
4. Inbox：先选工作区，再显示/搜索/创建；
5. 本周：跨全部工作区聚合，并支持批量同步。

action token 使用 `command:id:base64url(payload)`。base64url 不包含 `:`、
空格和 shell 元字符，标题、描述、Unicode、引号与换行不会改变 token 结构。
说明行固定 `valid=false`，Back 固定最后一行。

## 5. 持久化与并发

- 用户 Stacks 工作区只保存项目文件、任务文件、people 和 timelogs 内容；
- `Project.json`、`Stack.json`、`Timers.json` 属于插件运行状态；
- JSON 使用四空格缩进，便于与 Stacks 文本格式人工比对；
- 原子写实现始终在目标同目录创建临时文件；
- `ScopedFileLock` 使用 `flock(LOCK_EX | LOCK_NB)`，冲突返回 `lock_failure`；
- 测试、打包和开发 CLI 不得以真实工作区作为直接写入目标。

## 6. Alfred 与调试边界

`stacks-filter.zsh`：

1. 读取并 trim Alfred 查询；
2. 把用户自定义分隔符替换为 `|`；
3. 把可配置简称路由到 canonical command；
4. 调用 `Stacks_Tools ui`；
5. 过滤 bqlog 行，校验 JSON，并把 trace 写入 STDERR 和日志。

`stacks-action.zsh`：

1. 读取 action token；
2. 对目录选择器结果做 base64url；
3. 调用 `Stacks_Tools action`；
4. 过滤 bqlog 行；
5. 根据退出码播放 `Glass`/`Basso` 并输出最终文本。

`STACKS_DEBUG` 控制 `[stacks-trace]`，`STACKS_LOGGING` 控制所有文件日志。
Alfred JSON 的 stdout 必须只包含一行完整对象，诊断信息进入 stderr。

## 7. 注释与文档规范

本模板沿用原 `single-exe-static-modules` 的高信息密度注释格式：

- 每个文件顶部使用 `@file/@module/@brief` 说明职责、边界和关键设计；
- 每个导出类型、公开函数使用 `/** @brief ... @param ... @return ... @note ... */`；
- 简单字段/常量使用 `///`，枚举值使用 `///<` 行尾注释；
- 内部 helper 至少用 `///` 或 `/** */` 说明输入、回退语义和失败条件；
- 复杂事务在代码段落前说明阶段、原子性和回滚范围；
- 注释解释“为什么/边界/失败语义”，不重复逐行复述代码；
- 新增或修改公共接口时必须同步 `DESIGN_DOC.md`、`BUSINESS_API.md` 和相关测试。

## 8. 测试与文档生成

```bash
just build             # RelWithDebInfo 构建
just test              # 20 个 GTest 边界/并发用例
just mutation-test     # 4 个真实 mutant 必须全部 killed
just install-layout-test
just smoke
just package
just docs              # clang-doc + Doxygen
```

单元测试工作区固定为 `tests/.runtime`，日志固定为 `tests/logs`。真实数据烟测
先复制工作区，再执行 create/read/update/done/reopen/timer/inbox/delete。

`just docs` 生成：

```text
build/docs/clang-doc/html/index.html
build/docs/doxygen/html/index.html
```

`docs-clangdoc` 会先构建 `Stacks_Tools`；启用 GTest 时同时构建
`static_modules_tests`。这是 C++26 模块文档的必要步骤：clang-doc 读取
`compile_commands.json` 中的 `@...modmap` 参数时，模块映射必须已经存在。
该目标还固定从构建目录执行，使相对 `.modmap` 路径能够正确解析。

clang-doc 显式接收项目 `.cppm/.cpp` 源文件列表，只把构建目录中的 `std.cppm`
和 `bq.log.cppm` 当作模块依赖，不把它们当成文档输入，因此不会产生 libc++
保留模块名告警，也不会生成无关的标准库文档。

LLDB 的实际操作、常用命令和三个可复现实验见项目根目录
[debug.md](../debug.md)。

文档、测试和实现必须在同一变更中保持一致；接口错误文案或持久化字段发生变化时，
PR 不应只修改代码而遗漏契约文档。
