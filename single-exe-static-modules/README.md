# Omni-Stacks

`single-exe-static-modules` 架构的实际应用，也是 Stacks 2 的完整 Alfred
任务工作流核心。每个静态 C++26 模块独立编译，单个 `main` 只负责运行时路径发现
和 CLI 路由；工程名使用 `OmniStacksStaticModules`，发布二进制保留 Alfred
运行契约 `Stacks_Tools`。

核心能力：

- 保存/刷新/诊断/移除 Stacks 工作区路径；
- 向项目、Stack 或收件箱创建任务，单输入框录入五字段并自动提供 today/low；
- 按标题优先、描述其次模糊搜索，查看和编辑任务；
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
ProjectSync  (STATIC)    工作区有效性、路径索引移除、Project/Stack 索引
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

`docs-clangdoc` 会先构建模块目标，再把 `compile_commands.json` 过滤为所选目标
和项目源码的正常 `.o` 条目，确保 `.modmap` 存在且不会误选 CMake 的扫描临时项。
生成的 std BMI/`bq.log.cppm` 不作为直接文档输入；clang-doc 仍按 import 关系
为 `std` 等依赖生成映射页。Doxygen 与 clang-doc 两个 HTML 入口都会生成。

## LLDB 调试

完整的 Debug 构建、断点、变量检查、C++ 模块注意项和三个可复现实验见
[debug.md](debug.md)。实验覆盖：

- `TaskManager::generate_id`；
- `TaskManager::week_bounds`；
- `WorkflowUI::filter`。

## 构建

macOS + CMake 4.5+ + Ninja + Clang 23+ + C++26 + libc++。

```bash
just doctor
just build
just test
just mutation-test
just real-data-smoke "/path/to/StacksLibrary"
just smoke
```

所有完整 `just` 命令可用 `just --list` 查看，包括：

- `configure`、`build`、`build-clang`、`release-binary`、`fmt`、`check`
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
| `s ap` / `s add-path`              | 添加新路径                                                                                  |
| `s rl` / `s reload`                | 重载全部已保存路径，不再弹目录选择器                                                        |
| `s cf` / `s config`                | 列出全部路径、有效性和项目数；回车进入移除索引确认，不删除工作区文件                        |
| `s i` / `s inbox`                  | 选工作区 → 回车查看 Inbox 任务；输入关键词模糊搜索，无命中自动进入六行创建面板              |
| `s di` / `s default-inbox`         | 列出全部收件箱工作区，回车设置唯一默认收件箱，⭐ 标识；重复执行可重设                        |
| `s pin`                            | 列出全部项目/看板，回车固定或取消固定，📌 标识并优先显示，可固定多个                        |
| `s v` / `s view`                   | 项目/清单 → 回车查看任务；输入五字段后切换为六行创建面板 → 任务详情                         |
| `s t 关键词` / `s tasks`           | 跨工作区按标题优先、描述其次搜索任务                                                        |
| `s w` / `s week`                   | 所有已保存路径下本周截止任务                                                                |
| `s tm` / `s timers`                | 查看运行中计时并停止                                                                        |

任务详情中可继续修改标题、描述、开始/截止日期、完成状态、计时、归档、
优先级、可恢复删除和提醒同步。优先级接受 `low/低`、`medium/中`、
`high/高`、`critical/紧急`。

短输入键显示为 `task:✅xxxxxxxx ✅`、`board:✅xxxxxxxx ✅`、`inbox:✅xxxxxxxx ✅`；
完整主键保存在 `data/UiState.json`。任务创建格式为
`标题|描述|开始|截止|优先级`，描述中的 `|` 使用 `\|` 转义。开始默认 `today`，
截止默认 `tomorrow`，优先级默认 `low`；只输入标题即可直接回车创建，其余字段
可选或有预设值。第二段若是 `low/medium/high/critical` 会自动归入优先级，例如
`标题|high`。半角 `|`、全角 `｜` 及对应中英文标点可随输入法混用，
无需为分隔符切换状态。六行面板实时回显五字段，最后一行回车直接创建。
`s` 后输入不在枚举范围内的触发词时，直接进入六行创建面板，再次回车在
⭐ 默认收件箱和 📌 固定项目/看板的聚合列表中选择创建目标；固定仅影响排序，
项目/看板名称本身包含 📌 不会被误判或改写。若尚未设置默认收件箱和固定看板，
聚合页会按 Alfred 配置的触发词提示先用 `s di` / `s pin` 维护创建目标。

任务详情中新增“新建日历日程”：本机已安装苹果日历且未来 N 分钟（Alfred 变量
`STACKS_CALENDAR_DURATION`，默认 30）内没有来自 Stacks2 的日程时才显示。
日程标题=任务标题，备注=描述并携带 `[Stacks2]` 去重标记；同步到 Apple 提醒 /
OmniFocus 前同样会先判断对应应用是否安装，未安装则不展示、不执行。详情页只读
`Timers.json` 中最近一次成功插入日历的日期时间判断窗口，不再调用 AppleScript
探测，进入详情页不会因日历查询卡顿。

详情页操作顺序为：新建日历日程 → 同步到 OmniFocus → 修改标题，其余操作跟随其后。
标题/描述/开始/截止/优先级的编辑页显示两行：第一行待更新值（回车直接执行），
第二行原值参考，无需复制粘贴即可参考原文重新表述；操作成功后 Alfred 输入框自动清空。

Workflow 配置提供 Debug、Notification、Play Sound、Show Large Type 和
字段分隔符；`ap/rl/cf/v/t/w/tm/i/di/pin` 十个触发词别名全部可自定义。
字段分隔符按 Unicode 全角/半角等价解析，例如配置 `|` 时 `|` 与 `｜` 均有效。
`Short Key Sentinel 短键分隔哨兵` 默认为 `✅ `，支持改成任意非空文本；
`Include Completed & Archived 检索完成/归档任务` 默认关闭，开启后项目、
Inbox 和全局任务搜索会同时包含已完成与已归档任务。
Debug 打开后，
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

`just release-binary` 使用 `-Os`、ThinLTO、dead-strip 和非动态导入符号剥离，
生成最小体积的 `build/release/Stacks_Tools`。`just package` 会先重新执行该
构建，再生成不含源码、用户数据、运行时日志和 `update.json` 的：

```text
dist/Omni-Stacks.alfredworkflow
```

发布包不含用户 `data/` 和运行时 `log/`。首次启动时，程序会在二进制同级
自动创建空索引与日志目录：

```text
发布的 workflow/
├── Stacks_Tools       # 单个可执行文件
├── icon.png           # 符号链接 → icons/icon.png（不复制图片）
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
data/UiState.json
log/interface.log_YYYYMMDD_N.log
log/trace-YYYYMMDD.log
```

Alfred Workflow 元数据图标使用根目录 `icon.png` 符号链接，指向唯一的
`icons/icon.png`。打包使用 `zip -y` 保留链接，避免发布包或插件目录重复保存
同一张 PNG。

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
