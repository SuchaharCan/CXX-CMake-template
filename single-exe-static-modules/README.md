# Omni-Stacks

`single-exe-static-modules` 架构的实际应用，也是 Stacks 2 的完整 Alfred
任务工作流核心。每个静态 C++26 模块独立编译，单个 `main` 只负责运行时路径发现
和 CLI 路由；工程名使用 `OmniStacksStaticModules`，发布二进制保留 Alfred
运行契约 `Stacks_Tools`。

核心能力：

- 保存/刷新 Stacks 工作区路径；
- 向项目、Stack 或收件箱创建任务；
- 搜索、查看、编辑任务标题与描述；
- 设置开始日期、截止日期和完成状态；
- 跨全部已保存工作区聚合本周截止任务；
- 启动/停止计时并写入 Stacks `timelogs`；
- 同步到 Apple Reminders 和 OmniFocus；
- 归档和移到可恢复垃圾箱。

完整业务到接口映射、持久化字段和错误分类见
[docs/BUSINESS_API.md](docs/BUSINESS_API.md)。

核心 CLI 源码：[CXX-CMake-template](https://github.com/SuchaharCan/CXX-CMake-template)

开发者个人网站：[https://suchaharcan.github.io/](https://suchaharcan.github.io/)

拉取源码后执行 `just docs`，查看架构设计、接口、持久化字段和测试说明。

## 架构

```text
InterfaceLog (STATIC)    bqlog 文件日志 + 1000 条内存环形日志
ProjectSync  (STATIC)    .project 扫描、Project/Stack 索引、原子重建
TaskManager  (STATIC)    任务 CRUD、日期、完成、周查询、计时、归档/删除
ReminderSync (STATIC)    Reminders / OmniFocus AppleScript 同步
WorkflowUI   (STATIC)    Alfred Script Filter JSON 与 action token 分发
Stacks_Tools (exe)       单 main：路径发现、CLI 路由、输出
```

业务模块不接收拼接后的 shell 字符串。Alfred 只要调用：

```bash
Stacks_Tools ui "<query>"
Stacks_Tools action "<token>"
```

日期校验、任务参数和业务错误都在 C++ 核心完成。

## 注释与文档

源码沿用原 `single-exe-static-modules` 模板的高信息密度注释风格：

- `.cppm` 文件顶部说明模块职责、依赖边界和失败语义；
- 公开类型/API 使用 `@brief/@param/@return/@note` Doxygen 注释；
- 字段和枚举值使用 `///`/`///<`；
- JSON 事务、锁、回滚、日期算法和 Alfred token 在代码段落前解释设计原因；
- shell 脚本按“输入 → 路由 → 二进制 → 后处理 → 反馈”分段注释。

架构说明见 [docs/DESIGN_DOC.md](docs/DESIGN_DOC.md)，业务与 CRUD 映射见
[docs/BUSINESS_API.md](docs/BUSINESS_API.md)。执行 `just docs` 可同时生成：

```text
build/docs/doxygen/html/index.html
build/docs/clang-doc/html/index.html
```

`docs-clangdoc` 会先构建模块目标，再从构建目录执行 clang-doc，确保
`compile_commands.json` 中的 `.modmap` 相对路径存在且可解析；它只处理项目源码，
不会把生成的 `std.cppm`/`bq.log.cppm` 当作文档输入。Doxygen 与 clang-doc
两个 HTML 入口都会生成。

## LLDB 调试

完整的 Debug 构建、断点、变量检查、C++ 模块注意项和三个可复现实验见
[debug.md](debug.md)。实验覆盖：

- `TaskManager::generate_id`；
- `TaskManager::week_bounds`；
- `WorkflowUI::filter`。

## 构建

macOS + CMake 4.4+ + Ninja + Clang 23+ + C++26 + libc++。

```bash
just doctor
just build
just test
just mutation-test
just real-data-smoke "/path/to/StacksLibrary"
just smoke
```

所有完整 `just` 命令可用 `just --list` 查看，包括：

- `configure`、`build`、`build-clang`、`fmt`、`check`
- `test`、`parallel-test`、`mutation-test`、`real-data-smoke`、`smoke`
- `install-layout-test`
- `docs`、`uml`、`local-ci`
- `package`、`install`、`verify-install`、`verify-data`
- `env`、`doctor`、`ccache-stats`、`clean`

## Alfred 使用

默认关键字是 `s`。

| 输入                               | 行为                                                                                        |
| ---------------------------------- | ------------------------------------------------------------------------------------------- |
| `s`                                | 第一行“所有可用操作”，其后列出项目/清单                                                     |
| `s 标题\|描述\|开始\|截止\|优先级` | 每个项目/清单显示完整参数，回车即创建                                                       |
| `s ap` / `s add-path`              | 添加新路径                                                                                  |
| `s rl` / `s reload`                | 重载全部已保存路径，不再弹目录选择器                                                        |
| `s cf` / `s config`                | 查看路径配置，可分别添加或重载                                                              |
| `s i` / `s inbox`                  | 先选工作区，进入后直接列 Inbox 任务；输入即模糊匹配，无匹配自动进入新建；直接跟文本会被屏蔽 |
| `s v` / `s view`                   | 模糊筛选项目/清单 → 进入任务页 → 模糊筛选任务；无匹配进入新建，列表与详情均可返回上一级     |
| `s t 关键词` / `s tasks`           | 跨工作区搜索任务                                                                            |
| `s w` / `s week`                   | 所有已保存路径下本周截止任务                                                                |
| `s tm` / `s timers`                | 查看运行中计时并停止                                                                        |

任务详情中可继续修改标题、描述、开始/截止日期、完成状态、计时、归档、
优先级、可恢复删除和提醒同步。优先级接受 `low/低`、`medium/中`、
`high/高`、`critical/紧急`。

Workflow 配置提供 Debug、Notification、Play Sound、Show Large Type 和
字段分隔符；`ap/rl/cf/v/t/w/tm/i` 八个别名也全部可自定义。Debug 打开后，
Alfred 调试器会显示 `[stacks-trace]` 全链路日志
（入参 → 命令路由 → 预处理 → 二进制 argv → 退出码/返回 → 后处理），同一份
日志写入 workflow 的 `log/trace.log`。通知和大字号由 Alfred 原生
Conditional/Notification/Large Type 执行；声音按退出码区分，成功播放
`Glass`，失败播放 `Basso`。说明行统一使用插件内 `icons/note.png`。

日期接受：

```text
2026-11-08
today
tomorrow
+3d
monday ... sunday
```

## 构建产物与安装

`just package` 生成不含源码、用户数据和运行时日志的：

```text
dist/Omni-Stacks.alfredworkflow
```

发布包不含用户 `data/` 和运行时 `log/`。首次启动时，程序会在二进制同级
自动创建空索引与日志目录：

```text
发布的 workflow/
├── Stacks_Tools       # 单个可执行文件
├── icons/
│   ├── icon.png
│   ├── project.png
│   ├── board.png
│   ├── task.png
│   ├── note.png
│   └── inbox.png
├── info.plist
├── prefs.plist
└── scripts/
    ├── stacks-action.zsh
    └── stacks-filter.zsh
```

安装后首次运行会生成：

```text
data/{Project,Stack,Timers}.json
log/interface.log_YYYYMMDD_N.log
log/trace-YYYYMMDD.log
```

安装当前 Alfred workflow：

```bash
just install
just verify-install
```

默认安装目录是当前 `Omni-Stacks` workflow。也可覆盖：

> 使用该模板的一部分用户是来自我个人开发的`Omni-Stacks`插件， 如果想自定义扩展或更新接口就需要阅读源码和调试工作；
> 优先 先理解好Stacks2自身的业务逻辑 ，最好的话，先结合 just doc 生成的文档 进行查看，毕竟 我个人是梳理过 业务逻辑的
> 根据 你 自己约定好的 出入参编写， 编译构建生成的二进制文件 覆盖插件路径下的同名二进制文件
> 在Alfred插件中进行 联调 Alfred 通过 scripts中的脚本文件 对用户输入的参数进行预处理， 调用二进制文件中的函数接口 根据返回信息进行处理 抛给插件 ；
> 友情提示： 插件变量 有个Debug开关，开启 ， 同时Alfred 内有Debugger窗口栏， 最后 还有 logs文件夹 记录 日志 ；
> 前端Debugger记录预处理 调用接口 ， logs 记录 运行时日志信息， 快速定位 是预处理 还是 接口问题，lldb /AI 均能快速辅助 调试
> 源码已经提供， 调试思路已经提供。 剩下的自定义开发，如果测试成熟 并有单元测试变异边界测试维护好的话，欢迎提交pull 合并申请

```bash
STACKS_WORKFLOW_DIR="/path/to/workflow" just install
```

安装脚本会先把旧 `info.plist`、`prefs.plist`、二进制、`data/`、`log/` 和
`scripts/` 完整备份到 workflow 的 `.backups/<时间戳>/`。旧嵌套布局的
`Stacks_Tools/data/*.json` 会优先迁移到新根目录 `data/`，随后才使用根目录
数据和源码默认值，不会覆盖已保存路径。安装后可用：

```bash
just verify-install
just verify-data
```

## 数据与测试边界

- 真实 Stacks 源库只读；`real-data-smoke` 会复制到
  `tests/.runtime/real-data` 后执行完整增删改查。
- 单元测试工作区位于 `tests/.runtime`。
- 单元测试日志：`tests/logs/unit-tests.log`。
- 变异测试日志：`tests/logs/mutation.log` 和 `tests/logs/mutation-*.log`。
- 安装布局与迁移日志：`tests/logs/install-layout.log`。
- 真实数据副本烟测日志：`tests/logs/real-data-smoke.log`。
- AppleScript 编译日志：`tests/logs/applescript-*.log`。
- 包首次启动测试：`tests/logs/package-first-run.log`。

写入 Stacks 文件前会取得 `data/.stacks-tools.lock` 排他锁；JSON 通过同目录
临时文件 + `rename(2)` 原子替换。创建任务同时更新 `.task` 和 `.project`，
任一步失败会回滚已创建的 `.task`。

Alfred 配置中的 `Logging 日志记录` 可整体关闭 bqlog 与 trace 文件。日志启用
时自动清理超过 3 个月的接口日志和每日 trace 文件。

## 外部应用同步

同步任务必须已有 `duedate`：

```bash
Stacks_Tools sync-reminder <task_id> both --dry-run
Stacks_Tools sync-reminder <task_id> reminders
Stacks_Tools sync-reminder <task_id> omnifocus
```

任务备注包含隐藏标记 `[Stacks-ID:<task_id>]`，重复同步时按该标记更新，
避免产生重复提醒。Reminders 目标列表默认名为 `Stacks`。首次真实同步时，
macOS 可能要求 Alfred/终端获得 Reminders、OmniFocus 自动化权限。

## 计时说明

Stacks 应用把“运行中计时器”保存在 Electron 本地状态中，不写入工作区 JSON。
本工具用 `data/Timers.json` 管理运行状态；停止后向任务写入 Stacks 兼容的
`timelogs`。日期计划区间使用 Stacks 原生 `startdate` / `duedate` 字段。
