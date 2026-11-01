# Omni-Stacks 业务与接口约定

本文档是 Alfred UI、C++ 核心、持久化文件与测试之间的共同契约。接口或字段
变化时，必须同步更新本文档、单元测试和变异测试。

<span style = ' color:red; font-size:26px;'> 调试 提示：</span> <br/>

> 优先 先理解好Stacks2自身的业务逻辑 ，最好的话，先结合 just doc 生成的文档 进行查看，毕竟 我个人是梳理过 业务逻辑的<br/>
> 根据 你 自己约定好的 出入参编写， 编译构建生成的二进制文件 覆盖插件路径下的同名二进制文件<br/>
> 在Alfred插件中进行 联调 Alfred 通过 scripts中的脚本文件 对用户输入的参数进行预处理， 调用二进制文件中的函数接口 根据返回信息进行处理 抛给插件 ；<br/>
> 友情提示： 插件变量 有个`Debug`开关，开启 ， 同时Alfred 内有`Debugger`窗口栏， 最后 还有 logs文件夹 记录 日志 ；<br/>
> 前端Debugger记录预处理 调用接口 ， `logs` 记录 运行时日志信息， 快速定位 是预处理 还是 接口问题，lldb /AI 均能快速辅助 调试<br/>
> 源码已经提供， 调试思路已经提供。<br/>
> 剩下的自定义开发，如果测试成熟 并有单元测试变异边界测试维护好的话，欢迎提交pull 合并申请

> 我对 业务逻辑的术语描述： 路径 是设置工作区 `workspace` ， 每个工作区 可设置多个项目`Task`，也就是左侧下方`File`，一个收件箱 `inbox` ; 每个项目 可设置多个看板`stack` ，每个看板有多个任务`task`

## 文档与注释约定

源码沿用原模板的高密度注释格式：文件顶部说明模块边界，导出类型/函数使用
`@brief`、`@param`、`@return`、`@note`，字段使用 `///`/`///<`，复杂事务和
错误分支使用段落级注释说明阶段、原子性与回滚范围。接口注释是 Doxygen/clang-doc
的输入，`docs/DESIGN_DOC.md` 是架构总览，本文件是业务字段与 CRUD 的权威契约。

新增公开接口、持久化字段或 Alfred action 时，应同时更新：

1. 源码接口注释；
2. `DESIGN_DOC.md` 的架构/依赖说明；
3. 本文件的接口和字段映射；
4. 单元测试、变异测试或真实数据烟测。

## 1. 数据边界

### 1.1 工作区文件

一个 Stacks 工作区目录包含：

```text
Library/
├── <project-id>.project
├── people
└── tasks/
    ├── <task-id>.task
    ├── .trash/
    └── activities/
```

项目与 Stack 成员关系保存在 `.project`：

```json
{
  "id": "project-id",
  "title": "Project",
  "backlog": ["task-a", "task-b"],
  "stacks": [
    {
      "id": "stack-id",
      "title": "Stack",
      "tasks": ["task-c"]
    }
  ],
  "updated": "2026-11-01 12:00:00"
}
```

任务保存在 `tasks/<task-id>.task`。本工具使用的关键字段：

| 字段                  | 类型         | 业务含义                               |
| --------------------- | ------------ | -------------------------------------- |
| `id`                  | string       | 20 字符任务 ID                         |
| `title`               | string       | 标题，不允许全空白                     |
| `description`         | string       | 描述，可为空                           |
| `project`             | string       | 项目 ID；全局收件箱固定为 `"inbox"`    |
| `created` / `updated` | string       | 创建/更新时间                          |
| `startdate`           | `YYYY-MM-DD` | 开始日期                               |
| `duedate`             | `YYYY-MM-DD` | 截止日期                               |
| `priority`            | string       | `low` / `medium` / `high` / `critical` |
| `done`                | bool         | 是否完成                               |
| `progress`            | number       | 完成时写 `100`，重开写 `0`             |
| `completed`           | string       | 完成时的本地时间                       |
| `archived`            | string       | 空串表示未归档                         |
| `timelogs`            | array        | Stacks 兼容计时记录                    |

正常任务元数据继续保留 `public`、`owner`、`visibleUsers`、`visibleRoles`、
`tags`、`assignees` 等 Stacks 字段。

### 1.2 插件文件

```text
workflow/
├── Stacks_Tools       # 单个可执行文件
├── data/
│   ├── Project.json
│   ├── Stack.json
│   ├── Timers.json
│   └── .stacks-tools.lock
├── icons/
│   ├── icon.png
│   ├── project.png
│   ├── board.png
│   ├── task.png
│   ├── note.png
│   └── inbox.png
├── info.plist
├── log/
├── prefs.plist
└── scripts/
    ├── stacks-action.zsh
    └── stacks-filter.zsh
```

`Project.json` 保存 `library_paths` 与项目节点；`Stack.json` 保存项目和 Stack
节点，并附 `kind`、`project_path`、`library_path`。两个索引都由
`ProjectSync::sync_all` 重建，并分别通过同目录临时文件 + rename 原子替换。

发布包不包含 `data/` 和 `log/`。二进制首次运行会创建空索引和运行日志目录；
安装器升级时仍会迁移并保留用户已有数据。

## 2. 模块接口

### 2.1 ProjectSync

```cpp
std::expected<SyncSummary, SyncError> sync_all(
    const std::filesystem::path& search_path,
    const DbPaths& paths);
```

| 参数                 | 语义                                    |
| -------------------- | --------------------------------------- |
| `search_path`        | 新增/刷新工作区；空路径只扫描已保存路径 |
| `DbPaths.project_db` | Project 索引输出路径                    |
| `DbPaths.stack_db`   | Stack 索引输出路径                      |

同步结果包含 `project_count`、`stack_count`、`library_paths` 和
`failed_projects`。递归扫描 `.project`，单坏文件记录后继续扫描其他文件。

### 2.2 TaskManager 创建接口

```cpp
std::expected<TaskResult, TaskError> create_task(
    TargetId target_id,
    TaskInput input,
    const CreateTaskOptions& options);

std::expected<TaskResult, TaskError> create_inbox_task(
    TaskInput input,
    const CreateTaskOptions& options);
```

`TaskInput` 固定包含：

```cpp
TaskTitle title;
TaskDescription description;
std::optional<std::string> start_date;
std::optional<std::string> due_date;
std::string priority;
```

Alfred 录入顺序固定为：

```text
标题|描述|开始时间|截止时间|优先级
```

后四项可留空。优先级接受 `low/低`、`medium/中`、`high/高`、
`critical/紧急`，落盘为 Stacks 原生字符串值。CLI 同时支持
`--start`、`--due` 和 `--priority`。

目标类型：

- 项目 ID：任务插入项目 `backlog` 头部，并写入 `.task.project=<project-id>`；
- Stack ID：任务插入对应 `stack.tasks` 头部；
- 收件箱：写入工作区 `tasks/`，任务 `project` 为 `"inbox"`。

### 2.3 TaskManager 查询接口

```cpp
list_boards(paths)
list_tasks(paths, TaskListOptions)
list_week_due(paths, anchor_date)
get_task(TaskId, paths)
list_timers(paths)
```

`TaskListOptions`：

- `target`：项目、Stack 或 `inbox`；
- `query`：标题、描述、ID、项目名、看板名大小写不敏感搜索；
- `include_done`；
- `include_archived`。

`list_week_due` 以周一 00:00 到周日 23:59 为边界，读取 `Project.json` 中
全部 `library_paths`，排除完成和归档任务，按 `duedate` 升序返回。

### 2.4 TaskManager 更新接口

```cpp
update_task(TaskId, TaskPatch, paths)
set_start_date(TaskId, date, paths)
set_due_date(TaskId, date, paths)
set_schedule(TaskId, start, due, paths)
set_task_done(TaskId, bool, paths)
archive_task(TaskId, paths)
```

`TaskPatch` 支持标题、描述、开始日期、截止日期、优先级及显式清除字段。
开始日期不得晚于截止日期。

完成操作固定写：

```json
{
  "done": true,
  "progress": 100,
  "completed": "YYYY-MM-DD HH:MM:SS"
}
```

重开固定写 `done=false`、`progress=0` 并移除 `completed`。

### 2.5 TaskManager 删除接口

```cpp
std::expected<DeleteResult, TaskError> delete_task(TaskId, paths);
```

删除不是立即销毁：

1. 从全部 `.project` 的 `backlog` 和 `stacks[].tasks` 移除 ID；
2. 把 `.task` 移入 `tasks/.trash/<timestamp>-<task-id>.task`；
3. 项目更新失败或移动失败时回滚项目 JSON。

### 2.6 TaskManager 计时接口

```cpp
start_timer(TaskId, paths)
stop_timer(TaskId, paths)
```

`Timers.json` 结构：

```json
{
  "timers": {
    "<task-id>": {
      "title": "Task",
      "started_at": "YYYY-MM-DD HH:MM:SS",
      "start_epoch": 1762000000,
      "task_file": "/path/to/task"
    }
  }
}
```

停止时计算秒数，写入：

```json
{
  "date": "开始时间",
  "duration": 3600,
  "billable": false,
  "billed": false,
  "description": "",
  "person": "people 文件中的首个联系人 ID（可为空）",
  "project": "project-id",
  "id": "20 字符 timelog ID"
}
```

先更新任务，再清除运行状态；第二步失败会恢复原任务 JSON。

### 2.7 ReminderSync

```cpp
std::expected<SyncResult, SyncError> sync_task(
    const TaskReminder& task,
    Provider provider,
    const SyncOptions& options);
```

`Provider` 为 `reminders`、`omnifocus`、`both`。任务必须有 `duedate`。
用户字段只作为 `osascript` 独立 argv 传入，不经过 shell 拼接。

去重标记：

```text
[Stacks-ID:<task_id>]
```

该标记追加到 Reminders `body` / OmniFocus `note`。重复同步执行更新而不是
重复创建。日期只映射到本地当天 `23:59:00`。

## 3. 需求到 CRUD 映射

| 需求                   | Create                                        | Read                             | Update                       | Delete/恢复                                                             |
| ---------------------- | --------------------------------------------- | -------------------------------- | ---------------------------- | ----------------------------------------------------------------------- |
| 1. 添加路径 / 重载全部 | `sync_all(new_path)` / `sync_all("")`         | `Project.json.library_paths`     | 全量重载更新索引             | 本版本不做路径删除，避免误删；索引可重建                                |
| 2. 插入看板任务        | `create_task`                                 | `get_task/list_tasks`            | `update_task`                | `delete_task` / `archive_task`                                          |
| 3. 添加到收件箱        | `create_inbox_task`（先选全部路径，首个默认） | `list_tasks(target=inbox)`       | `update_task`                | `delete_task` / `archive_task`                                          |
| 4. 查看/编辑指定看板   | `list_boards`                                 | `list_tasks(target)`、`get_task` | `TaskPatch`                  | `delete_task` / `archive_task`                                          |
| 5. 设置截止日期        | `create_task` 的 `due_date`                   | `TaskRecord.due_date`            | `set_due_date`               | 清空用 `TaskPatch.clear_due_date`                                       |
| 新增：扩展录入参数     | `start_date` / `due_date` / `priority`        | `TaskRecord`                     | `TaskPatch`                  | 分别支持清空/更新                                                       |
| 6. 本周截止多库聚合    | —                                             | `list_week_due`                  | 日期更新后重查               | 完成/归档后自动排除                                                     |
| 7. 设置已完成          | —                                             | `TaskRecord.done`                | `set_task_done(true)`        | `set_task_done(false)` 重开                                             |
| 8. 同步提醒            | `sync_task`                                   | `SyncResult`                     | 同 ID 再同步即更新           | 外部删除需在对应应用处理；Stacks 侧 `archive/delete` 不自动删除外部项目 |
| 9. 开始/结束/计时      | `set_schedule` / `start_timer`                | `TaskRecord` / `list_timers`     | `update_task` / `stop_timer` | 清空日期/停止后运行状态移除                                             |

“开始时间/结束时间”有两层含义，接口明确分开：

- 业务计划区间：Stacks `startdate` / `duedate`；
- 实际计时区间：`start_timer` 开始时间 + `stop_timer` 写入 `timelogs.duration`。

## 4. CLI 接口

```text
Stacks_Tools sync <library_path>
Stacks_Tools add <target_id> "title|description" [--start d] [--due d] [--priority p]
Stacks_Tools inbox "title|description" [--start d] [--due d] [--priority p]
Stacks_Tools boards [query]
Stacks_Tools tasks [--board id] [--query text] [--todo-only]
Stacks_Tools week [anchor_date]
Stacks_Tools get <task_id>
Stacks_Tools edit <task_id> [--title t] [--description d] [--start d] [--due d] [--priority p]
Stacks_Tools due <task_id> <date>
Stacks_Tools start <task_id> <date>
Stacks_Tools schedule <task_id> <start_date> <due_date>
Stacks_Tools done <task_id>
Stacks_Tools reopen <task_id>
Stacks_Tools archive <task_id>
Stacks_Tools delete <task_id>
Stacks_Tools timer-start <task_id>
Stacks_Tools timer-stop <task_id>
Stacks_Tools timers
Stacks_Tools sync-reminder <task_id> reminders|omnifocus|both [--dry-run]
Stacks_Tools sync-week reminders|omnifocus|both [--dry-run]
Stacks_Tools ui <query>
Stacks_Tools action <token>
```

退出码：

| 退出码 | 语义           |
| ------ | -------------- |
| `0`    | 成功           |
| `1`    | 参数/用法错误  |
| `2`    | 业务或 IO 失败 |

## 5. 错误分类

| 模块         | 错误                                                                                                                 |
| ------------ | -------------------------------------------------------------------------------------------------------------------- |
| ProjectSync  | `search_path_missing`、`search_path_not_directory`、`project_db_unreadable`、`project_file_unreadable`、`io_failure` |
| TaskManager  | 无效 ID/标题/日期、日期区间错误、索引/项目/任务不可读、目标/任务不存在、计时状态错误、锁冲突、写入/删除失败          |
| ReminderSync | 任务 ID/截止日期错误、`osascript` 不可用、Reminders/OmniFocus 同步失败、子进程失败                                   |

所有可恢复错误通过 `std::expected` 返回，不跨越模块 API 抛出 JSON/IO 异常。

## 6. 并发与事务

- `TaskManager` 的写操作取得 `data/.stacks-tools.lock` 的跨进程排他锁；
- `ProjectSync::sync_all` 不获取 TaskManager 写锁，通过单文件原子替换保护每个索引；
- JSON 写入同目录临时文件并原子 `rename`；
- 创建任务：先写 `.task`，再写 `.project`；后者失败则删除新任务；
- 计时停止：先写 `.task`，再写 `Timers.json`；后者失败则恢复任务；
- 删除任务：项目更新或文件移动失败时回滚已改项目。

Stacks 应用本身若同时编辑同一文件，仍可能出现外部进程竞态。建议批量操作前
关闭正在编辑的 Stacks 任务详情，操作后让其文件监听刷新。

## 7. Alfred UI 契约

`ui` 在 stdout 输出严格 Alfred JSON；bqlog 的系统诊断由 workflow shell
适配层过滤，防止污染 JSON。界面采用逐层补全而不是复杂参数分隔符：

```text
s                → 第一行“所有可用操作”，随后列出项目/清单
s 标题|描述|开始|截止|优先级 → 每个层级显示完整参数，回车创建
s v  → 模糊项目/清单 → 任务页 → 模糊任务 → 任务详情；各级支持 Back
s ap / s rl      → 添加新路径 / 重载全部路径（两个独立动作）
s cf             → 查看路径配置
s i              → 选工作区 → 直接列 Inbox 任务 → 模糊过滤/编辑；无匹配自动新建
s t 关键词       → 全局搜索全部工作区/项目/清单
s w / s tm       → 本周截止 / 计时
```

每个条目副标题固定中文在上、英文在下。一级命令使用 Stacks 图标；总览
Note、项目、清单和任务使用插件内 SF Symbols PNG，其他动作使用 macOS 内置图标。

shell 适配层负责命令判定、分隔符替换和 argv 拼装，并把调用链写入 STDERR
（`[stacks-trace]` 前缀）与 `log/trace.log`，与 Alfred 调试器协作定位问题：

1. Alfred 输入；
2. 命令路由判定；
3. 分隔符/参数预处理；
4. 二进制 argv 拼装；
5. 二进制退出码与返回内容；
6. 插件对返回结果的后处理（过滤 bqlog、条目计数、JSON 透传）。

Alfred Workflow 配置提供 `STACKS_DEBUG`、`STACKS_NOTIFICATION`、
`STACKS_SOUND`、`STACKS_LARGE_TYPE`、`STACKS_DELIMITER`，以及 8 个
`STACKS_ALIAS_*` 命令别名。Run Script 输出连接到 Post Notification 与
Large Type 的 Conditional；声音由 action shell 根据退出码选择成功
`Glass` 或失败 `Basso`，避免静态 PlaySound 无法区分结果。说明行固定使用
`icons/note.png`。OmniFocus/Reminders 同步结果复用同一反馈链。

`STACKS_LOGGING` 控制 bqlog 接口日志与每日 trace；关闭后不写入任何日志文件。
启用时，初始化阶段删除超过 3 个月的 `interface.log_YYYYMMDD_*` 和
`trace-YYYYMMDD.*`。所有模块公共接口、CLI 路由和 Workflow UI 的输入/输出摘要
均通过 `interface_log::record` 写入 bqlog。

## 8. 测试映射

| 测试                                                             | 覆盖                                     |
| ---------------------------------------------------------------- | ---------------------------------------- |
| `Boundary.GenerateIdFixedLengthAndAlphabet`                      | 20 字符 ID 与字母表                      |
| `Boundary.CreateTaskForProjectGoesToBacklogHead`                 | 项目 backlog 头插                        |
| `Boundary.CreateTaskForStackWritesStackTasks`                    | Stack 成员关系                           |
| `Boundary.CreateInboxUsesInboxProject`                           | `project="inbox"`                        |
| `Boundary.UpdateTitleDescriptionAndDates`                        | 标题/描述/日期更新                       |
| `Boundary.WeekDueAggregatesMultipleLibrariesAndExcludesDone`     | 多库本周聚合                             |
| `Boundary.DoneWritesStacksCompletionFieldsAndReopenClearsThem`   | 完成/重开字段                            |
| `Boundary.TimerStopWritesStacksTimelogAndClearsState`            | 计时落盘                                 |
| `Boundary.ArchiveAndDeleteAreObservableAndRecoverable`           | 归档和可恢复删除                         |
| `Boundary.WorkflowUiEmitsValidAlfredJsonAndExecutesCreateAction` | Alfred JSON/action、五段参数和优先级落盘 |
| `Boundary.ReminderSyncDryRunAndScriptsContainDedupMarker`        | 外部同步去重 dry-run                     |
| `Parallel.*`                                                     | ID 与接口日志并发                        |
| `tests/install_layout_smoke.sh`                                  | 根目录布局、旧数据迁移、备份、可执行文件 |

变异测试固定包含：

1. 任务 ID 长度 20 → 19；
2. 完成进度 100 → 99；
3. 每周结束周日 → 周六；
4. 收件箱 `project` 由 `inbox` → `backlog`。

每个 mutant 必须在源码内隔离目录中构建，并由指定测试杀死；日志保存在
`tests/logs/mutation-*.log`。

## 9. Read-Code 验证

开发期验证使用真实 `Read - Code` 与 `Rule - KanBan` 的隔离副本，观察到
项目文件、任务文件和 timelogs 与上述字段一致。`tests/real_data_smoke.sh`
在副本上实际执行：

```text
sync → boards → tasks → create → edit → get → done → reopen
→ timer-start → timer-stop → UI JSON → reminder dry-run
→ inbox create → delete → trash verify
```

源库不被修改；结果日志位于 `tests/logs/real-data-smoke.log`。

## 10. Stacks 文件变化总览

本节把业务、接口和 Stacks 文件变化绑定在一起。接口调试、字段升级和版本排查时，
应先确定“调用哪个接口”，再确定“应该读写哪些文件”，最后比较调用前后的 JSON。

### 10.1 业务术语与截图

下图标出了 Stacks 2 中 Workspace、Project、Stack 和 Task 的界面位置：

![Stacks 2 工作区、项目、看板和任务层级](https://suchaharcan.github.io/03Share_blog/private-source-public-deploy/stacks-business-hierarchy.png)

Stacks 页面标题可能显示“项目 - Projekt”。本文和持久化模型统一称它为 Project，
避免与任务 Task 混淆；Task 只表示最小任务卡片。

普通 Stack 任务从自身向上查找有三层上级节点：

```text
Workspace 工作区
└── Project 项目
    └── Stack 看板
        └── Task 任务
```

| 术语      | 业务位置                    | 持久化映射                                    | 上级节点                    |
| --------- | --------------------------- | --------------------------------------------- | --------------------------- |
| Workspace | Stacks 左上角工作区         | `Project.json.library_paths` 的一项           | 根节点                      |
| Project   | 左侧 Documents/File 项目    | `<project-id>.project` 与项目索引节点         | Workspace                   |
| Stack     | 项目中的看板                | `.project` 的 `stacks[]`                      | Project → Workspace         |
| Task      | Stack 下的任务卡片          | `tasks/<task-id>.task` 与 `stacks[].tasks` ID | Stack → Project → Workspace |
| Inbox     | 工作区 General 下的特殊入口 | 任务 K.project 为 inbox                       | Workspace → Inbox           |

说明：

- Project 中未放入具体 Stack 的任务位于 `.project` 的 `backlog` 数组；
- 调试时可以把 Backlog 理解为项目级虚拟看板，但它不属于 `stacks[]`；
- Inbox 任务不写 `backlog` 或 `stacks[].tasks`，只有工作区一个上级节点；
- 一个普通 Stack 任务最多对应 Workspace、Project、Stack 三个上级节点。

完整截图说明同时发布在博客：

<https://suchaharcan.github.io/03Share_blog/private-source-public-deploy/>

### 10.2 文件代号

为避免表格重复绝对路径，后续使用以下代号：

| 代号 | 文件                                 | 归属          | 说明                                      |
| ---- | ------------------------------------ | ------------- | ----------------------------------------- |
| P    | `data/Project.json`                  | 插件索引      | 保存 `library_paths`、项目文件定位、mtime |
| S    | `data/Stack.json`                    | 插件索引      | 保存项目/Stack 扁平节点和 `kind`          |
| T    | `data/Timers.json`                   | 插件状态      | 保存运行中的计时器                        |
| L    | `data/.stacks-tools.lock`            | 插件锁        | TaskManager 跨进程写锁文件                |
| W    | `<library>/<project>.project`        | Stacks 工作区 | 项目、backlog、Stack 和成员 ID            |
| K    | `<library>/tasks/<task-id>.task`     | Stacks 工作区 | 单个任务完整 JSON                         |
| R    | `<library>/people`                   | Stacks 工作区 | 计时日志的人员 ID 来源                    |
| X    | `<library>/tasks/.trash/*.task`      | Stacks 工作区 | 可恢复删除的任务文件                      |
| G    | `log/interface.log_*`、`log/trace-*` | 插件日志      | 接口和 Alfred 调试记录，不属于业务数据    |

### 10.3 全局文件变化规则

1. `TaskManager` 的创建、编辑、完成、归档、删除和计时接口都会先取得 L。
2. `ProjectSync` 只读取 W，重建 P 和 S；不会修改 W、K 或 T。
3. JSON 写入先生成同目录临时文件，再 rename 到目标路径。
4. 创建任务先写 K，再写 W；W 写失败时删除 K。
5. 删除任务先修改 W 引用，再把 K rename 到 X；任一阶段失败会恢复 W 快照。
6. 停止计时先写 K 的 `timelogs`，再写 T；T 写失败时恢复 K。
7. 编辑任务保留 K 中本工具不认识的 Stacks 字段，只修改明确指定的字段。
8. `ProjectSync` 的 P、S 是两个独立的原子文件，不构成跨文件事务。
9. 所有读接口对 Stacks 业务文件只读；日志是否写入由 `STACKS_LOGGING` 决定。

## 11. 接口到文件变化矩阵

| 接口 / 业务入口                      | 读取                    | 写入          | 字段或文件变化                                                   | 锁           | 失败与回滚                                                                       |
| ------------------------------------ | ----------------------- | ------------- | ---------------------------------------------------------------- | ------------ | -------------------------------------------------------------------------------- |
| `ProjectSync::sync_all(new_path)`    | P、W                    | P、S          | 追加规范化 library_path；重建项目/Stack 索引节点                 | 否           | 单坏 W 记录到 failed_projects；坏 P 返回 unreadable；索引写入失败返回 io_failure |
| `ProjectSync::sync_all("")`          | P、W                    | P、S          | 用已保存 library_paths 全量重建索引                              | 否           | 不弹出目录选择器；单个坏 W 不阻断其他项目                                        |
| `TaskManager::generate_id()`         | 无业务文件              | 无            | 生成 20 字符 TaskId                                              | 否           | 不触碰 Stacks 数据                                                               |
| `TaskManager::create_task()`         | P、W                    | K、W          | 新建 K；在 backlog 或 stack.tasks 头部插入 ID；刷新 W.updated    | 是           | W 写失败删除 K                                                                   |
| `TaskManager::create_inbox_task()`   | P（未显式路径时）       | K             | 新建 project 为 inbox 的 K；不改 W、P、S、T                      | 是           | K 写失败返回 write_failure                                                       |
| `TaskManager::list_boards()`         | P、W                    | 无            | 恢复项目/Stack 展示节点                                          | 否           | 损坏项目跳过；索引错误返回 TaskError                                             |
| `TaskManager::list_tasks()`          | P、W、K                 | 无            | 根据 target、完成态、归档态和 query 过滤                         | 否           | 损坏任务文件跳过；索引错误返回 TaskError                                         |
| `TaskManager::list_week_due()`       | P、W、K                 | 无            | 过滤本周 duedate、未完成、未归档任务                             | 否           | 与 list_tasks 相同                                                               |
| `TaskManager::get_task()`            | P、W、K                 | 无            | 将 K 字段和 W 成员关系合并为 TaskRecord                          | 否           | 找不到返回 task_not_found                                                        |
| `TaskManager::update_task()`         | P、W、K                 | K             | 修改 title、description、startdate、duedate、priority、updated   | 是           | 校验失败不写；写失败原 K 保持完整                                                |
| `TaskManager::set_start_date()`      | P、W、K                 | K             | 复用 update_task；写或清除 startdate                             | 是           | 开始日期晚于 due 返回 invalid_date_range                                         |
| `TaskManager::set_due_date()`        | P、W、K                 | K             | 复用 update_task；写或清除 duedate                               | 是           | 日期非法返回 invalid_date                                                        |
| `TaskManager::set_schedule()`        | P、W、K                 | K             | 同一事务写 startdate 和 duedate                                  | 是           | 区间非法时不产生半更新                                                           |
| `TaskManager::set_task_done(true)`   | P、W、K                 | K             | done=true、progress=100、completed=当前时间                      | 是           | 失败时原 K 保持完整                                                              |
| `TaskManager::set_task_done(false)`  | P、W、K                 | K             | done=false、progress=0、删除 completed                           | 是           | 同上                                                                             |
| `TaskManager::archive_task()`        | P、W、K                 | K             | archived=当前时间，K 文件保留                                    | 是           | 失败时原 K 保持完整                                                              |
| `TaskManager::delete_task()`         | P、W、K                 | W、X          | 从所有 backlog/stack.tasks 移除 ID；K 移到 X                     | 是           | W 或多文件 rename 失败时恢复项目快照                                             |
| `TaskManager::start_timer()`         | P、W、K、T              | T             | T.timers[task_id] 写入 title、started_at、start_epoch、task_file | 是           | 重复启动返回 timer_already_running                                               |
| `TaskManager::stop_timer()`          | P、W、K、T、R           | K、T          | K.timelogs 追加记录；T.timers 删除该 ID                          | 是           | T 写失败时恢复 K 原始 JSON                                                       |
| `TaskManager::list_timers()`         | T                       | 无            | 读取并排序运行中的计时器                                         | 否           | T 损坏返回 timer_db_unreadable                                                   |
| `TaskManager::normalize_date()`      | 无                      | 无            | 返回规范化 YYYY-MM-DD                                            | 否           | 非法表达式返回 invalid_date                                                      |
| `TaskManager::week_bounds()`         | 无                      | 无            | 返回周一和周日日期                                               | 否           | 锚点非法返回空边界                                                               |
| `TaskManager::for_each_backlog_id()` | W                       | 无            | 遍历 W.backlog 字符串 ID                                         | 否           | W 不可读返回 project_file_unreadable                                             |
| `ReminderSync::sync_task()`          | 外部应用                | 外部应用      | 更新或创建 Reminders/OmniFocus 条目                              | 否           | 不改 P、S、T、W、K；失败分类为 SyncError                                         |
| `WorkflowUI::filter()`               | 按页面需要读 P、W、K、T | 无业务文件    | 将领域结果映射为 Alfred JSON                                     | 否           | 异常转为不可选 Alfred 条目                                                       |
| `WorkflowUI::action()`               | 按 token 决定           | 按 token 决定 | 分发到 ProjectSync、TaskManager、ReminderSync                    | 由子接口决定 | 返回稳定错误文本                                                                 |

## 12. Alfred 业务到接口和文件变化

| 用户业务        | Alfred 输入 / action                         | 核心接口                                     | 主要文件变化                                     | 观察点                                    |
| --------------- | -------------------------------------------- | -------------------------------------------- | ------------------------------------------------ | ----------------------------------------- |
| 添加工作区路径  | `s ap`，action `add-path:<base64>`           | `ProjectSync::sync_all(new_path)`            | P.library_paths 追加；P.nodes、S.nodes 重建      | 新路径规范化后只出现一次                  |
| 重载全部路径    | `s rl`，action `reload-all`                  | `ProjectSync::sync_all("")`                  | P、S 重建；W 只读                                | 不弹目录窗口；failed_projects 应有诊断    |
| 创建项目任务    | `s 标题\|描述\|开始\|截止\|优先级`，选择项目 | `TaskManager::create_task()`                 | W.backlog 头插 ID；新建 K；W.updated 刷新        | K.project 等于项目 ID；backlog[0] 是新 ID |
| 创建清单任务    | 同上，选择 Stack                             | `TaskManager::create_task()`                 | 对应 stack.tasks 头插 ID；新建 K；W.updated 刷新 | K.project 仍是所属项目 ID                 |
| 创建 Inbox 任务 | `s i` → 工作区 → 输入                        | `TaskManager::create_inbox_task()`           | 仅新建 K；K.project 为 inbox                     | 不写 W.backlog 或 stack.tasks             |
| 查看项目和任务  | `s v`、`s t`、`s w`                          | `list_boards`、`list_tasks`、`list_week_due` | 无业务文件变化                                   | 只产生 UI 和可选日志                      |
| 打开任务详情    | filter `task:<id>`                           | `TaskManager::get_task()`                    | 无                                               | 确认 W 成员关系和 K 字段一致              |
| 修改标题或描述  | detail → rename/desc → action                | `TaskManager::update_task()`                 | K.title/description/updated                      | W 不变，未知字段仍保留                    |
| 设置开始时间    | detail → start                               | `set_start_date()` / `update_task()`         | K.startdate、updated                             | 与 due 的先后关系必须满足                 |
| 设置截止时间    | detail → due                                 | `set_due_date()` / `update_task()`           | K.duedate、updated                               | 本周过滤以 duedate 为准                   |
| 设置计划区间    | detail → schedule                            | `set_schedule()`                             | K.startdate、K.duedate、updated                  | 两个日期一次写回                          |
| 设置优先级      | detail → priority                            | `update_task()`                              | K.priority、updated                              | 仅保存 low/medium/high/critical           |
| 标记完成        | action `done:<id>:1`                         | `set_task_done(true)`                        | K.done、progress、completed、updated             | done=true、progress=100                   |
| 重新打开        | action `done:<id>:0`                         | `set_task_done(false)`                       | K.done、progress、updated；删除 completed        | completed 不残留                          |
| 本周截止        | `s w`                                        | `list_week_due()`                            | 无                                               | 跨全部 library_paths 聚合                 |
| 同步提醒        | action `sync:<id>:provider`                  | `ReminderSync::sync_task()`                  | 外部应用变化；Stacks 文件不变                    | 备注含 `[Stacks-ID:<id>]`                 |
| 同步本周        | action `sync-week:provider`                  | `list_week_due()` + `sync_task()`            | 外部应用变化；Stacks 文件不变                    | 只处理本周未完成未归档项                  |
| 启动计时        | action `timer-start:<id>`                    | `start_timer()`                              | T.timers 新增任务项                              | 同一任务重复启动失败                      |
| 停止计时        | action `timer-stop:<id>`                     | `stop_timer()`                               | K.timelogs 追加；T.timers 删除                   | duration >= 1；person 可为空              |
| 归档任务        | action `archive:<id>`                        | `archive_task()`                             | K.archived、updated                              | K 文件不移动                              |
| 删除任务        | `delete:<id>` → `delete-confirm:<id>`        | `delete_task()`                              | W 引用删除；K 移到 X                             | `.project` 不应残留 ID                    |

## 13. 典型文件前后变化

### 13.1 从项目 backlog 创建任务

调用前 W：

```json
{
  "id": "project-a",
  "backlog": ["old-task"],
  "stacks": [],
  "updated": "2026-11-01 10:00:00"
}
```

调用后 W：

```json
{
  "id": "project-a",
  "backlog": ["new-task-id", "old-task"],
  "stacks": [],
  "updated": "2026-11-02 09:00:00"
}
```

新增 K 的核心字段：

```json
{
  "id": "new-task-id",
  "title": "Task title",
  "description": "Task description",
  "project": "project-a",
  "startdate": "2026-11-02",
  "duedate": "2026-11-08",
  "priority": "high"
}
```

接口：`TaskManager::create_task()`。

### 13.2 编辑任务

调用前 K：

```json
{
  "title": "old",
  "description": "old body",
  "duedate": "2026-11-08",
  "custom_stacks_field": "keep-me"
}
```

调用后 K：

```json
{
  "title": "new",
  "description": "new body",
  "startdate": "2026-11-02",
  "duedate": "2026-11-08",
  "priority": "high",
  "custom_stacks_field": "keep-me"
}
```

接口：`TaskManager::update_task()`。W 不变。

### 13.3 完成与重开

完成：

```json
{
  "done": true,
  "progress": 100,
  "completed": "2026-11-02 09:30:00"
}
```

重开：

```json
{
  "done": false,
  "progress": 0
}
```

重开后 K 中不再存在 `completed`。接口：`TaskManager::set_task_done()`。

### 13.4 计时开始与停止

开始时 T：

```json
{
  "timers": {
    "task-id": {
      "title": "Task",
      "started_at": "2026-11-02 09:40:00",
      "start_epoch": 1793602800,
      "task_file": "/library/tasks/task-id.task"
    }
  }
}
```

停止后 K 增加：

```json
{
  "timelogs": [
    {
      "date": "2026-11-02 09:40:00",
      "duration": 125,
      "billable": false,
      "billed": false,
      "description": "",
      "person": "",
      "project": "project-a",
      "id": "timelog-id"
    }
  ]
}
```

停止后 T.timers 删除该任务 ID。接口：`start_timer()`、`stop_timer()`。

### 13.5 可恢复删除

调用前：

```text
W.backlog      包含 task-id
K              存在
X              task-id 不存在
```

调用后：

```text
W.backlog      不包含 task-id
K              原 tasks/task-id.task 不存在
X              tasks/.trash/<timestamp>-task-id.task 存在
```

接口：`TaskManager::delete_task()`。

### 13.6 工作区索引重建

P 的核心结构：

```json
{
  "library_paths": ["/path/to/library"],
  "nodes": [
    {
      "id": "project-a",
      "title": "Project",
      "path": "/path/to/library/project-a.project",
      "library_path": "/path/to/library",
      "mtime": "2026-11-02 09:00:00"
    }
  ],
  "updateTime": "2026-11-02 09:00:00"
}
```

S 的核心结构：

```json
{
  "nodes": [
    {
      "id": "project-a",
      "projectid": "project-a",
      "title": "Project",
      "kind": "project",
      "project_path": "/path/to/library/project-a.project",
      "library_path": "/path/to/library"
    },
    {
      "id": "stack-a",
      "projectid": "project-a",
      "title": "Stack",
      "kind": "stack",
      "project_path": "/path/to/library/project-a.project",
      "library_path": "/path/to/library"
    }
  ],
  "updateTime": "2026-11-02 09:00:00"
}
```

接口：`ProjectSync::sync_all()`。W 不作为输出被修改。

## 14. 接口版本变化检查清单

修改接口、文件字段或 Alfred token 时，按以下顺序检查：

1. 找出接口属于 ProjectSync、TaskManager、ReminderSync 还是 WorkflowUI。
2. 对照第 11 节确认应读取和写入哪些文件。
3. 对照第 12 节确认 Alfred 业务入口是否使用了该接口。
4. 修改 C++ 接口注释和本文档。
5. 更新单元测试，至少覆盖成功、校验失败、IO 失败和回滚。
6. 如果字段影响真实 Stacks 应用，运行隔离副本 real-data-smoke。
7. 重跑 mutation-test，确认关键字段或边界仍被测试保护。
8. 重跑 package 和 Alfred action 联调，确认 UI token 与二进制参数没有漂移。

## 15. 接口调试定位

开启 `STACKS_LOGGING` 和 `STACKS_DEBUG` 后，一次业务调用应按以下链路定位：

```text
Alfred 输入
  → stacks-filter.zsh 命令判定
  → C++ ui/action 参数
  → 领域接口
  → 前后文件 JSON
  → 返回 expected 错误或结果
  → Alfred 后处理
```

常用只读检查：

```bash
jq . data/Project.json
jq . data/Stack.json
jq . data/Timers.json
jq . /path/to/task-id.task
jq '.backlog' /path/to/project.project
```

如果要调试写接口，应先把工作区复制到测试目录，再在副本上执行：

```bash
just real-data-smoke "/path/to/StacksLibrary"
```

接口调用前后比较 JSON，能快速区分：

- 参数在 Alfred 预处理阶段已经错误；
- C++ 接口校验失败；
- 文件写入成功但成员关系未更新；
- W 与 K 之间出现跨文件不一致；
- 日志关闭导致误以为接口没有执行。

## 16. Doxygen 中的使用方式

本文位于 Doxygen 的 `INPUT` 目录中，文件扩展名为 `.md`。执行：

```bash
just docs
```

然后在 Doxygen 的 Related Pages 或主页链接中打开
`Omni-Stacks 业务与接口约定`。第 10 至 15 节会形成可搜索、可跳转的独立标题：

- `Stacks 文件变化总览`
- `接口到文件变化矩阵`
- `Alfred 业务到接口和文件变化`
- `典型文件前后变化`
- `接口版本变化检查清单`
- `接口调试定位`

这样接口实现、持久化字段、Alfred action、测试和版本调试可以在同一页面交叉核对。
