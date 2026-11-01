# Omni-Stacks 业务与接口约定

本文档是 Alfred UI、C++ 核心、持久化文件与测试之间的共同契约。接口或字段
变化时，必须同步更新本文档、单元测试和变异测试。

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

| 字段 | 类型 | 业务含义 |
| --- | --- | --- |
| `id` | string | 20 字符任务 ID |
| `title` | string | 标题，不允许全空白 |
| `description` | string | 描述，可为空 |
| `project` | string | 项目 ID；全局收件箱固定为 `"inbox"` |
| `created` / `updated` | string | 创建/更新时间 |
| `startdate` | `YYYY-MM-DD` | 开始日期 |
| `duedate` | `YYYY-MM-DD` | 截止日期 |
| `priority` | string | `low` / `medium` / `high` / `critical` |
| `done` | bool | 是否完成 |
| `progress` | number | 完成时写 `100`，重开写 `0` |
| `completed` | string | 完成时的本地时间 |
| `archived` | string | 空串表示未归档 |
| `timelogs` | array | Stacks 兼容计时记录 |

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
`ProjectSync::sync_all` 原子重建。

发布包不包含 `data/` 和 `log/`。二进制首次运行会创建空索引和运行日志目录；
安装器升级时仍会迁移并保留用户已有数据。

## 2. 模块接口

### 2.1 ProjectSync

```cpp
std::expected<SyncSummary, SyncError> sync_all(
    const std::filesystem::path& search_path,
    const DbPaths& paths);
```

| 参数 | 语义 |
| --- | --- |
| `search_path` | 新增/刷新工作区；空路径只扫描已保存路径 |
| `DbPaths.project_db` | Project 索引输出路径 |
| `DbPaths.stack_db` | Stack 索引输出路径 |

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

| 需求 | Create | Read | Update | Delete/恢复 |
| --- | --- | --- | --- | --- |
| 1. 添加路径 / 重载全部 | `sync_all(new_path)` / `sync_all("")` | `Project.json.library_paths` | 全量重载更新索引 | 本版本不做路径删除，避免误删；索引可重建 |
| 2. 插入看板任务 | `create_task` | `get_task/list_tasks` | `update_task` | `delete_task` / `archive_task` |
| 3. 添加到收件箱 | `create_inbox_task`（先选全部路径，首个默认） | `list_tasks(target=inbox)` | `update_task` | `delete_task` / `archive_task` |
| 4. 查看/编辑指定看板 | `list_boards` | `list_tasks(target)`、`get_task` | `TaskPatch` | `delete_task` / `archive_task` |
| 5. 设置截止日期 | `create_task` 的 `due_date` | `TaskRecord.due_date` | `set_due_date` | 清空用 `TaskPatch.clear_due_date` |
| 新增：扩展录入参数 | `start_date` / `due_date` / `priority` | `TaskRecord` | `TaskPatch` | 分别支持清空/更新 |
| 6. 本周截止多库聚合 | — | `list_week_due` | 日期更新后重查 | 完成/归档后自动排除 |
| 7. 设置已完成 | — | `TaskRecord.done` | `set_task_done(true)` | `set_task_done(false)` 重开 |
| 8. 同步提醒 | `sync_task` | `SyncResult` | 同 ID 再同步即更新 | 外部删除需在对应应用处理；Stacks 侧 `archive/delete` 不自动删除外部项目 |
| 9. 开始/结束/计时 | `set_schedule` / `start_timer` | `TaskRecord` / `list_timers` | `update_task` / `stop_timer` | 清空日期/停止后运行状态移除 |

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

| 退出码 | 语义 |
| --- | --- |
| `0` | 成功 |
| `1` | 参数/用法错误 |
| `2` | 业务或 IO 失败 |

## 5. 错误分类

| 模块 | 错误 |
| --- | --- |
| ProjectSync | `search_path_missing`、`search_path_not_directory`、`project_db_unreadable`、`project_file_unreadable`、`io_failure` |
| TaskManager | 无效 ID/标题/日期、日期区间错误、索引/项目/任务不可读、目标/任务不存在、计时状态错误、锁冲突、写入/删除失败 |
| ReminderSync | 任务 ID/截止日期错误、`osascript` 不可用、Reminders/OmniFocus 同步失败、子进程失败 |

所有可恢复错误通过 `std::expected` 返回，不跨越模块 API 抛出 JSON/IO 异常。

## 6. 并发与事务

- 所有写操作取得 `data/.stacks-tools.lock` 的跨进程排他锁；
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

| 测试 | 覆盖 |
| --- | --- |
| `Boundary.GenerateIdFixedLengthAndAlphabet` | 20 字符 ID 与字母表 |
| `Boundary.CreateTaskForProjectGoesToBacklogHead` | 项目 backlog 头插 |
| `Boundary.CreateTaskForStackWritesStackTasks` | Stack 成员关系 |
| `Boundary.CreateInboxUsesInboxProject` | `project="inbox"` |
| `Boundary.UpdateTitleDescriptionAndDates` | 标题/描述/日期更新 |
| `Boundary.WeekDueAggregatesMultipleLibrariesAndExcludesDone` | 多库本周聚合 |
| `Boundary.DoneWritesStacksCompletionFieldsAndReopenClearsThem` | 完成/重开字段 |
| `Boundary.TimerStopWritesStacksTimelogAndClearsState` | 计时落盘 |
| `Boundary.ArchiveAndDeleteAreObservableAndRecoverable` | 归档和可恢复删除 |
| `Boundary.WorkflowUiEmitsValidAlfredJsonAndExecutesCreateAction` | Alfred JSON/action、五段参数和优先级落盘 |
| `Boundary.ReminderSyncDryRunAndScriptsContainDedupMarker` | 外部同步去重 dry-run |
| `Parallel.*` | ID 与接口日志并发 |
| `tests/install_layout_smoke.sh` | 根目录布局、旧数据迁移、备份、可执行文件 |

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
