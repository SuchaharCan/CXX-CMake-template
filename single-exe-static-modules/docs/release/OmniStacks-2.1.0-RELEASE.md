# Omni-Stacks 2.1.0

Omni-Stacks 是面向 Stacks 2 的 Alfred 任务中心。Alfred 负责交互，单二进制 C++26
核心负责工作区、项目、Stack、任务、计时、提醒同步和持久化。

## 下载 / Download

- [Omni-Stacks.alfredworkflow](https://github.com/SuchaharCan/AlfredWorkflows_Collection/releases/download/Stacks2-2.1.0/Omni-Stacks.alfredworkflow)
- 安装包版本：`2.1.0`
- Bundle ID：`com.songhoujin.stacks-tools`

发布包不包含 `update.json`、`data/`、`log/` 或用户工作区内容。升级安装会保留
已有索引和日志。

## 中文功能说明

![Omni-Stacks 2.1.0 中文功能图](https://github.com/SuchaharCan/AlfredWorkflows_Collection/releases/download/Stacks2-2.1.0/OmniStacks-2.1.0-ZH.png)

### 单输入框六行创建

- 一次输入 `标题|描述|开始|截止|优先级`。
- 下方六行实时回显各字段完整内容。
- 标题、描述、开始、截止、优先级使用同一创建 action；标题存在后按回车直接创建。
- 开始默认 `today`，优先级默认 `low`，截止默认未设置。
- 描述中的 `|` 使用 `\|` 转义。

### 可配置短键哨兵

- `task:`、`board:`、`inbox:`、路径移除等短键统一追加输入分隔哨兵。
- 默认哨兵为 `✅ `。
- Workflow Configuration 新增 `STACKS_HANDLE_SENTINEL`，可改成任意非空文本。
- 用户删除哨兵后继续输入仍然兼容。

### 已完成 / 已归档任务过滤

- 默认不检索已完成或已归档任务。
- Workflow Configuration 新增 `STACKS_INCLUDE_DONE_ARCHIVED`。
- 开启后，项目、Stack、Inbox 和全局任务搜索同时包含完成与归档任务。

### 工作区与路径

- 有效工作区必须包含正确类型的 companies、documents.tree、people、tasks/
  和 timeboxes/；.project 文件可选。
- `s cf` 只列出路径、移除说明和返回项。
- 回车路径进入确认页；确认后只移除 `Project.json`、`Stack.json`、
  `Timers.json` 中的索引，不删除工作区文件。
- 已有失效路径或已被外部删除的工作区也可以移除其索引。

### 搜索与任务交互

- 模糊搜索标题和描述，标题命中优先于描述命中。
- `s i` 先选工作区，回车查看 Inbox 任务。
- 任务短键使用 `✅xxxxxxxx ✅`，不再把完整任务 ID 放进输入框。
- 空 action、失效 action 不再显示巨大的 `unknown action:`。

### 发布包与插件图标

- 发布包不携带 `update.json`，也不注册 Alfred 自动更新元数据。
- `Stacks_Tools` 使用最小体积构建并移除内部符号。
- Alfred 根 `icon.png` 是指向 `icons/icon.png` 的符号链接，不再复制第二份图标。

### 日志与测试

- 核心写接口记录接口名、入参、字段变化和结果摘要。
- 32 个 GTest、安装布局和打包首启测试通过。

## English Release Notes

![Omni-Stacks 2.1.0 English feature sheet](https://github.com/SuchaharCan/AlfredWorkflows_Collection/releases/download/Stacks2-2.1.0/OmniStacks-2.1.0-EN.png)

Omni-Stacks is an Alfred task center for Stacks 2. The Alfred workflow handles
interaction, while a single C++26 binary owns workspace validation, projects,
stacks, tasks, timers, reminder synchronization and persistence.

### Highlights

- Create a task with one input, `title|description|start|due|priority`, while six
  result rows preview every field in real time.
- Start defaults to `today` and priority defaults to `low`; use `\|` to keep a
  literal pipe inside a description.
- Configure the short-key input sentinel with `STACKS_HANDLE_SENTINEL`; default is
  `✅ `. Parsing remains compatible when the sentinel is manually removed.
- Completed and archived tasks are hidden by default. Enable
  `STACKS_INCLUDE_DONE_ARCHIVED` to include them in project, Inbox and global search.
- Validate the exact Stacks workspace structure and remove only plugin indexes,
  never workspace files.
- Search titles before descriptions, with substring and subsequence fuzzy matching.
- The release package excludes `update.json` and does not register Alfred
  workflow-update metadata.
- `Stacks_Tools` uses the min-size build and has internal symbols stripped.
- Use a root `icon.png` symlink to `icons/icon.png`; the release archive preserves
  the reference with `zip -y`.

### Compatibility

- Existing Stacks workspace files are not modified during upgrades.
- Existing short task IDs and legacy `inbox-workspace:<base64>` queries remain
  accepted for backward compatibility.
- Release package: `Omni-Stacks.alfredworkflow`, version `2.1.0`.

## Source

- [CXX-CMake-template](https://github.com/SuchaharCan/CXX-CMake-template)
- [Omni-Stacks documentation](https://suchaharcan.github.io/CXX-CMake-template/single-exe-static-modules/)
