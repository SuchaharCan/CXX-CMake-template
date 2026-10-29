# single-exe-static-modules 架构设计与 API 设计文档

> 本文件是模板的**权威设计说明**，同时被 Doxygen 用作 HTML 文档主页
> （`docs/Doxyfile.in` 的 `USE_MDFILE_AS_MAINPAGE` 指向本文件）。
> 生成自动化：`just docs`（clang-doc + Doxygen 双后端）。
> 代码内注释与本文档互相补充：本文档讲"为什么"，代码注释讲"是什么"。

## 1. 模板定位

`single-exe-static-modules` 演示"多独立功能模块作为静态库、统一单层导入 main"的模板形态：

- 每个功能模块都是独立的 **STATIC 模块库**（C++20 模块接口单元 `.cppm`）；
- 模块之间**不互相 import**，只依赖公共模块 `InterfaceLog`；
- `main` 只做单层 `import` 与 CLI 路由，不含业务逻辑；
- 日志全部走 **bqlog C++ 模块**（`import bq.log;`），不使用 spdlog（无模块接口）；
- 平台约束：macOS only，Ninja + CMake ≥ 4.4.4 + LLVM/Clang++ ≥ 23.1 + C++26 + libc++。

### 架构图

```
                 ┌─────────────────────────────┐
                 │  Stacks_Tools（exe, main）  │
                 │  单层 import + CLI 路由     │
                 └──────┬──────────┬───────────┘
                        │          │
           ┌────────────▼──┐   ┌───▼──────────────┐
           │ ProjectSync   │   │ TaskManager      │
           │ 项目/Stack 同步│   │ 任务创建 / ID    │
           └──────┬────────┘   └───┬──────────────┘
                  │  （只依赖 InterfaceLog）
           ┌──────▼──────────────────────────────┐
           │ InterfaceLog（bqlog 模块日志 + 1000 行环形缓冲）│
           └─────────────────────────────────────┘
```

依赖规则（模块独立性）：

| 模块                | 依赖                                | 不依赖       |
| ------------------- | ----------------------------------- | ------------ |
| InterfaceLog        | `import std`、`import bq.log`       | 任何业务模块 |
| ProjectSync         | `import std`、`import InterfaceLog` | TaskManager  |
| TaskManager         | `import std`、`import InterfaceLog` | ProjectSync  |
| Stacks_Tools (main) | 三个模块全部                        | —            |

## 2. 设计原则与参考资料映射

本模板的每个设计决定都有出处。下表给出"原则 → 实现位置"的映射。

| 原则                                                       | 出处                                                                                   | 实现位置                                                                                               |
| ---------------------------------------------------------- | -------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------ |
| 类型即注释：强类型替代裸 string/引用参数                   | 小彭《现代化的 API 设计指南》(type_rich_api)                                           | `ApiName`/`LogText`、`ProjectId`/`TaskId`/`TaskTitle`/`TaskDescription`、`DbPaths`/`CreateTaskOptions` |
| 返回结构体而非 pair / 出参                                 | type_rich_api                                                                          | `SyncSummary`、`TaskResult`                                                                            |
| 可恢复错误用 `std::expected<值, 错误枚举>`；不可恢复才终止 | 小彭《现代 C++ 错误处理知多少》(error_code)                                            | 三个模块的 API 边界全部 expected；main 只映射枚举 → 消息                                               |
| 提前返回（每错误分支立即 return，拒绝深层嵌套）            | error_code、cpp_tricks                                                                 | `sync_all` / `create_task` 的立即调用 lambda                                                           |
| 函数对象（模板 Visitor）替代 `std::function` 类型擦除      | 小彭《函数式设计模式》(design_functor)                                                 | `for_each_recent`、`for_each_project_file`、`for_each_backlog_id`                                      |
| 无 UB：无符号 ID 运算、原子计数、JSON 防御访问             | 小彭《未定义行为完整列表》(undef)、Sandor Dargo《C++26: Reducing undefined behaviour》 | `generate_id`、`load_quick_index`、`load_json`                                                         |
| C++26 读未初始化变量 = erroneous behaviour；初始化纪律     | Sandor Dargo《C++26: erroneous behaviour》、初始化四部曲 (initializations-part-1~4)    | 所有结构体成员默认初始化器；`std::tm local{}` 零初始化                                                 |
| 可读 assert 不变式                                         | Sandor Dargo《C++26: A User-Friendly assert() macro》                                  | `generate_id` 的 `assert(cond && "message")`                                                           |
| 可移植 mtime 提取                                          | Sandor Dargo《The Portable Way to Extract Date and Time from a File's mtime》          | `ProjectSync::format_file_time`（含本机 libc++ 无 tzdb 的回退，见 §4.2）                               |

小技巧落地点（《应知应会 C++ 小技巧》cpp_tricks）：

- 立即调用 lambda 初始化/复用：`create_task` 用 IIFE 收敛整段业务，再统一记日志；
- 打表代替 if-else：`status_name` / `sync_error_message` / `task_error_message` 用 switch 映射表；
- 读取整个文件：`read_file`（测试工具函数）用 `istreambuf_iterator`；
- `insert` 不会替换已有值 → backlog 头插新任务时利用该语义保证顺序；
- map 的 `operator[]` 只用于"写入新元素"，查询一律 `find`/`at`；
- 多线程共享状态必须锁保护：`InterfaceLog` 单把 `std::mutex`，ID 计数器用 `std::atomic`。

## 3. 模块详细设计

### 3.1 InterfaceLog（模块：`import InterfaceLog`）

职责：接口输入/输出日志的唯一出口。**bqlog 文本文件落盘 + 内存 1000 行环形缓冲**。

公开 API：

| 类型/函数                           | 签名                                                                                               | 说明                                                    |
| ----------------------------------- | -------------------------------------------------------------------------------------------------- | ------------------------------------------------------- |
| `kMaxLines`                         | `inline constexpr std::size_t = 1000`                                                              | 环形缓冲容量上限                                        |
| `ApiStatus`                         | `enum class { ok, failed }`                                                                        | 调用结果状态                                            |
| `LogError`                          | `enum class { invalid_path, backend_unavailable }`                                                 | 可恢复初始化错误                                        |
| `ApiName`                           | `struct { std::string value; }`                                                                    | 接口名强类型                                            |
| `LogText`                           | `struct { std::string value; }`                                                                    | 输入/输出文本强类型                                     |
| `init`                              | `std::expected<std::monostate, LogError> init(const std::filesystem::path& = "log/interface.log")` | 初始化 bqlog TextFileAppender；重复调用且路径不变时幂等 |
| `record`                            | `void record(ApiName, LogText input, LogText output, ApiStatus)`                                   | 记录一次调用：环形缓冲 + bqlog 文件（后端可用时）       |
| `recent`                            | `std::vector<std::string> recent()`                                                                | 快照，最旧在前                                          |
| `count` / `clear` / `log_path`      | —                                                                                                  | 容量查询 / 清空环形 / 当前日志路径                      |
| `for_each_recent`                   | `template <std::invocable<std::string_view> Visitor> void`                                         | 锁内逐条访问（函数对象设计）                            |
| `status_name` / `log_error_message` | `std::string_view ... noexcept`                                                                    | 枚举 → 可读消息                                         |

实现要点：

- **bqlog 模块模式**：`import bq.log;`，配置字符串启用 `text_file` appender，`max_file_size=1048576`；
- 日志对象名附加 `std::hash<路径>`，避免并行测试进程共享同名日志对象；
- `record` 在锁内执行 `force_flush`（保证闪退前落盘）；bqlog 异常只吞掉，不影响业务接口；
- 内部状态（`g_mutex`/`g_lines`/`g_logger` 等）放在**非导出的 module linkage 命名空间**而非匿名命名空间——`for_each_recent` 是导出模板，会在其他 TU 实例化，引用 TU-local 实体是 C++23 模块规则下不允许的（Clang 会告警 `-Wreference-tu-local-entity-in-other-tu`）。

### 3.2 ProjectSync（模块：`import ProjectSync`）

职责：扫描 Stacks 库目录下的 `.project` 文件，重建 `Project.json` / `Stack.json` 两个索引库。

公开 API：

| 类型/函数               | 签名                                                                                             | 说明                               |
| ----------------------- | ------------------------------------------------------------------------------------------------ | ---------------------------------- |
| `DbPaths`               | `struct { path project_db; path stack_db; }`                                                     | 一次传参避免两个 string 顺序颠倒   |
| `SyncError`             | `enum class { search_path_missing, project_db_unreadable, project_file_unreadable, io_failure }` | 同步可恢复错误                     |
| `SyncSummary`           | `struct { size_t project_count; size_t stack_count; }`                                           | 结果摘要                           |
| `sync_all`              | `std::expected<SyncSummary, SyncError> sync_all(const path&, const DbPaths& = {})`               | 空搜索路径 = 只重扫已记忆路径      |
| `for_each_project_file` | `template <std::invocable<const directory_entry&> Visitor> void`                                 | 目录下 `.project` 遍历（函数对象） |
| `sync_error_message`    | `std::string_view ... noexcept`                                                                  | 错误 → 消息                        |
| `format_file_time`      | `std::string format_file_time(file_time_type)`                                                   | 可移植 mtime 格式化                |

`sync_all` 数据流：

1. 建库目录；读历史 `library_paths`（文件不存在 = 首次同步，空集合）；
2. 搜索路径非空时校验存在并加入集合；
3. 对每个库路径遍历 `.project`，解析出 `id`/`title` 与嵌套 `stacks`；
4. 项目节点写入 `mtime`（`format_file_time`），项目自身同时作为 Stack 首条节点；
5. **容错语义**：单个坏 `.project` 文件记录 `project_file_unreadable` 后继续扫描，最后返回第一个可恢复错误（部分成功也返回错误，由调用方决定处理）；
6. 全程把输入/输出摘要记入 `InterfaceLog`。

`format_file_time` 与 tzdb 缺口：

- 可移植部分：`file_clock` → `system_clock` 用**时长差平移**（`file_time - file_clock::now() + system_clock::now()`），不假设 file_clock 纪元；
- 本机 Homebrew libc++ 以 `_LIBCPP_HAS_TIME_ZONE_DATABASE=0` 构建，**没有 `std::chrono::zoned_time`/`current_zone`**，因此本地时区拆分回退 POSIX `localtime_r`（可重入）+ `strftime`；`std::tm local{}` 先零初始化（初始化纪律）。若未来换用支持 tzdb 的 libc++，可无缝替换为 `std::chrono::zoned_time`。

### 3.3 TaskManager（模块：`import TaskManager`）

职责：为 Project / Stack 目标创建任务：校验目标 → 生成 ID → 写入 `.task` 文件 → 头插 backlog/stack tasks。

公开 API：

| 类型/函数                                                | 签名                                                                                                                     | 说明                                |
| -------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ | ----------------------------------- |
| `kTaskIdLength`                                          | `= 20`                                                                                                                   | ID 固定长度                         |
| `ProjectId` / `TaskId` / `TaskTitle` / `TaskDescription` | 强类型包装 `std::string`                                                                                                 | 类型即注释                          |
| `CreateTaskOptions`                                      | `struct { path project_db; path stack_db; }`                                                                             | 数据库路径一次传参                  |
| `TaskError`                                              | 10 个枚举值（见 §4 错误总表）                                                                                            | 可恢复错误                          |
| `TaskResult`                                             | `struct { TaskId task_id; path task_file; string target_kind; }`                                                         | 成功结果                            |
| `generate_id`                                            | `TaskId generate_id()`                                                                                                   | 20 字符 ID                          |
| `create_task`                                            | `std::expected<TaskResult, TaskError> create_task(ProjectId, TaskTitle, TaskDescription, const CreateTaskOptions& = {})` | 核心业务                            |
| `for_each_backlog_id`                                    | `template <std::invocable<std::string_view> Visitor> std::expected<size_t, TaskError>`                                   | backlog 遍历（函数对象 + expected） |
| `task_error_message`                                     | `std::string_view ... noexcept`                                                                                          | 错误 → 消息                         |

`create_task` 数据流（提前返回风格，IIFE 内每错误立即 return）：

1. 校验 `target_id` / `title` 非空；
2. 读 Stack 索引库构建快速索引（`load_json` 防御解析）；
3. `find` 目标（查询不用 `operator[]`）；拷贝 `TargetLink` 出 map 避免迭代器失效；
4. 从项目索引库取 `.project` 文件路径；
5. 生成 `TaskId`，按目标类型头插：项目 → `backlog`；Stack → 对应 `stacks[].tasks`；
6. **兜底**：Stack 目标在项目文件中缺失时退回项目 `backlog`，保证任务不丢失；
7. 写 `tasks/<id>.task`（Stacks 应用兼容的元数据字段）与更新后的项目文件；两个 `ofstream` 任一失败 → `write_failure`；
8. 输入/输出摘要记入 `InterfaceLog`。

ID 生成算法（无 UB）：

- 字母表 64 字符（`-` `0-9` `A-Z` `_` `a-z`），每字符 6 bit；
- 前 8 字符取 `steady_clock` 纳秒计数低位：`uint64_t` 按位与 + 无符号右移，**无符号运算保证回绕，不触发有符号溢出 UB**；
- 后 12 字符随机：`std::seed_seq` 混合时间戳高低位、`std::atomic` 递增序号、`random_device`，喂给 `std::mt19937_64` + `std::uniform_int_distribution<int>(0, 63)`；
- 不变式 `assert(kTaskIdLength >= 8 && "...")` 与 `static_assert(kAlphabet.size() == 64)`。

### 3.4 main（`src/main.cpp`）

只做三件事：找项目根 → 初始化日志 → 子命令路由。

| 命令   | 语法                                         | 成功输出                                   | 失败输出                        |
| ------ | -------------------------------------------- | ------------------------------------------ | ------------------------------- |
| `sync` | `Stacks_Tools sync <search_path>`            | `sync ok: projects=N stacks=M`             | `sync failed: <消息>`，退出码 2 |
| `add`  | `Stacks_Tools add <id> "title\|description"` | `task created: id=... target=... file=...` | `task failed: <消息>`，退出码 2 |
| `list` | `Stacks_Tools list <project_file>`           | 逐行 `  - <id>` + `backlog tasks: N`       | `list failed: <消息>`，退出码 2 |

退出码约定（枚举 `ExitCode`）：`0` 成功；`1` 用法错误（参数不足/未知命令）；`2` 模块返回可恢复错误。

- bqlog 初始化失败属可恢复：打印 warning 继续（环形缓冲仍在）；
- `add` 的 `title|description` 分隔符不存在时整串作为标题，描述为空；
- `list` 用带状态 lambda（计数+打印）走 `for_each_backlog_id`，演示函数对象设计。

## 4. 错误总表

| 模块         | 枚举                      | 触发条件                             | 可读消息                             |
| ------------ | ------------------------- | ------------------------------------ | ------------------------------------ |
| InterfaceLog | `invalid_path`            | 日志路径为空                         | `log path is empty`                  |
| InterfaceLog | `backend_unavailable`     | bqlog 后端初始化异常                 | `bqlog backend unavailable`          |
| ProjectSync  | `search_path_missing`     | 搜索路径不存在                       | `search path does not exist`         |
| ProjectSync  | `project_db_unreadable`   | 项目索引库解析失败                   | `cannot parse project database`      |
| ProjectSync  | `project_file_unreadable` | 单个 `.project` 解析失败（继续扫描） | `cannot parse project file`          |
| ProjectSync  | `io_failure`              | 写索引库失败                         | `failed to write database`           |
| TaskManager  | `empty_target_id`         | 目标 ID 为空                         | `target id is empty`                 |
| TaskManager  | `empty_title`             | 标题为空                             | `title is empty`                     |
| TaskManager  | `stack_db_missing`        | Stack 索引库不存在                   | `stack database does not exist`      |
| TaskManager  | `stack_db_unreadable`     | Stack 索引库解析失败                 | `cannot parse stack database`        |
| TaskManager  | `stack_db_no_nodes`       | 缺少 `nodes` 数组                    | `stack database missing nodes array` |
| TaskManager  | `target_not_found`        | 目标不在索引中                       | `target id does not exist`           |
| TaskManager  | `project_db_unreadable`   | 项目索引库解析失败                   | `cannot parse project database`      |
| TaskManager  | `project_file_not_found`  | `.project` 路径缺失/不存在           | `project file not found`             |
| TaskManager  | `project_file_unreadable` | `.project` 解析失败                  | `cannot parse project file`          |
| TaskManager  | `write_failure`           | 写 `.task`/项目文件失败              | `failed to write task/project file`  |

设计约定：**API 边界不抛异常**；nlohmann 对空文件/坏内容抛 `parse_error`，统一由 `load_json` 接住映射为对应枚举（错误分类原则：可恢复错误走 expected，绝不扩散成异常）。

## 5. UB 规避清单

对照《未定义行为完整列表》与 C++26 变化，逐项自查：

| 风险类别                                              | 规避措施                                                                        |
| ----------------------------------------------------- | ------------------------------------------------------------------------------- |
| 有符号整数溢出                                        | ID 生成只用 `uint64_t` 无符号右移/按位与（无符号保证回绕）                      |
| 数据竞争                                              | `InterfaceLog` 全部共享状态单把 `std::mutex`；`g_sequence` 用 `std::atomic`     |
| 读未初始化变量（C++26 = erroneous behaviour）         | 结构体成员全部默认初始化器；`std::tm local{}`、`char buffer[32]{}` 显式零初始化 |
| 容器越界访问                                          | 环形缓冲只在锁内 `push_back/pop_front`；map 查询走 `find`/`at`                  |
| JSON 隐式创建节点（`operator[]` 读语义误用）          | 一律先 `contains()` 再 `is_array()` 再 `value()`；`operator[]` 仅用于写新元素   |
| 迭代器失效                                            | `load_quick_index` 后拷贝 `TargetLink` 值再使用                                 |
| 空指针/空路径                                         | `init` 空路径立即返回；目录遍历前先 `exists` 检查                               |
| 悬垂引用（C++26 起绑定临时到返回引用直接 ill-formed） | API 只按值返回结构体 / `std::expected`，不返回引用                              |

## 6. 初始化纪律（initializations-part-1~4 落地）

- **所有 POD/结构体成员**给默认初始化器：`int project_count = 0`、`bool is_project = false`、`std::string target_kind{"project"}`；
- 局部缓冲统一 `{}` 零初始化（`std::tm local{}`、`char buffer[32]{}`）；
- 聚合按字段名初始化：`DbPaths db_paths{root / "data" / "Project.json", root / "data" / "Stack.json"}`；
- 模板变量用 `inline constexpr`（`kMaxLines`/`kTaskIdLength`），无 ODR 与初始化顺序问题。

## 7. 测试策略

`tests/static_modules_tests.cxx`（21 个用例，GTest）：

| 类别       | 用例                                                                                                            | 要点                                                 |
| ---------- | --------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------- |
| 边界：ID   | `GenerateIdHasFixedLengthAndAlphabet`                                                                           | 长度硬编码 20 + 字母表校验（硬编码是为变异测试服务） |
| 边界：同步 | 空搜索路径 / 缺失路径 / 坏 `.project` 继续扫描 / 正常同步+mtime 格式 / `format_file_time` 正则                  | 覆盖全部 `SyncError`                                 |
| 边界：任务 | 空目标/空标题/缺库/未知目标/项目头插顺序/Stack 插入/`.task` 元数据/backlog 遍历顺序/缺失文件错误/错误消息全枚举 | 覆盖全部 `TaskError`                                 |
| 边界：日志 | 空路径拒绝 / 1200 条只留 1000 / 函数对象遍历全量可见                                                            | 环形容量与降级语义                                   |
| 并行       | 8 线程×300 唯一 ID；8 线程×200 写日志容量仍 1000                                                                | 无数据竞争                                           |

变异测试（`just mutation-test`，真实变异体必须被杀死）：

| 变异体                  | 杀死它的测试                                     |
| ----------------------- | ------------------------------------------------ |
| `kTaskIdLength` 20 → 19 | `Boundary.GenerateIdHasFixedLengthAndAlphabet`   |
| backlog 头插 → 尾插     | `Boundary.CreateTaskForProjectGoesToBacklogHead` |

- 类型即注释（type_rich_api）：ApiName/LogText、ProjectId/TaskId/TaskTitle/TaskDescription、DbPaths/CreateTaskOptions，杜绝裸 string 传参顺序错误
- 错误分类（error_code）：所有 API 边界改为 std::expected<结果, 错误枚举>，10 种 TaskError、4 种 SyncError 全枚举带消息；load_json 统一接住 nlohmann 的 parse_error，API 不抛异常
- 函数对象（design_functor）：for_each_recent、for_each_project_file、新增 for_each_backlog_id（模板 Visitor，无 std::function 开销）
- 无 UB（undef + C++26）：ID 生成只用 uint64_t 无符号运算，std::atomic 序号 + seed_seq；JSON 一律 contains() 防御访问；模块内部状态从匿名命名空间移到 module linkage，消除 -Wreference-tu-local-entity 隐患
- C++26 初始化纪律：成员全默认初始化器、std::tm local{}、char buffer[32]{}；可读 assert（assert(cond && "msg")）

## 8. 文档生成（双后端）

```bash
just docs    # clang-doc（LLVM）+ Doxygen 双后端，输出 build/docs/
```

- **clang-doc**：`--executor=all-TUs --format=html` 消费 `compile_commands.json`（`CMAKE_EXPORT_COMPILE_COMMANDS` 已全局开启）；模块 TU 会有解析告警但继续产出 HTML；
- **Doxygen**：`docs/Doxyfile.in` 由 CMake 展开，`INPUT` 含 `src`、`modules`、`docs`，本文件作为 HTML 主页；同时输出 XML（供后续 pipeline）；
- 阅读入口：`build/docs/clang-doc/html/`、`build/docs/doxygen/html/index.html`。

## 9. 参考资料

- [小彭大典：现代化的 API 设计指南（类型即注释）](https://parallel101.github.io/cppguidebook/type_rich_api/)
- [小彭大典：未定义行为完整列表](https://parallel101.github.io/cppguidebook/undef/)
- [小彭大典：现代 C++ 错误处理知多少（错误码 / std::expected）](https://parallel101.github.io/cppguidebook/error_code/)
- [小彭大典：函数式设计模式（函数指针 / 仿函数 / std::function 抉择）](https://parallel101.github.io/cppguidebook/design_functor/)
- [小彭大典：应知应会 C++ 小技巧](https://parallel101.github.io/cppguidebook/cpp_tricks/)
- [Sandor Dargo: C++26: Reducing undefined behaviour](https://www.sandordargo.com/blog/2026/07/29/cpp26-reduces-undefined-behaviour)
- [Sandor Dargo: The Portable Way to Extract Date and Time from a File's mtime in C++](https://www.sandordargo.com/blog/2026/07/22/extracting-datetime-from-mtime)
- [Sandor Dargo: C++26: A User-Friendly assert() macro](https://www.sandordargo.com/blog/2026/03/25/cpp26-user-friendly-assert)
- [Sandor Dargo: C++26: erroneous behaviour](https://www.sandordargo.com/blog/2025/02/05/cpp26-erroneous-behaviour)
- [Sandor Dargo: When and how variables are initialized? Part 1-4](https://www.sandordargo.com/blog/2024/04/10/initializations-part-1)
- [Tencent BqLog](https://github.com/Tencent/BqLog)

## 10. 重大变更记录

| 日期       | 变更                                                                                                                | 影响                                      |
| ---------- | ------------------------------------------------------------------------------------------------------------------- | ----------------------------------------- |
| 2026-10-29 | 按博客原则重构三模块 + main + 测试：强类型、`std::expected` 错误分类、函数对象遍历、无 UB ID 生成、C++26 初始化纪律 | 全量 API 破坏性变更（同模板内一次性迁移） |
| 2026-10-29 | 新增 `for_each_backlog_id`（函数对象+expected）与 CLI `list` 子命令                                                 | 扩展，向后兼容                            |
| 2026-10-29 | `format_file_time` 因 Homebrew libc++ 无 tzdb 回退 `localtime_r`+`strftime`                                         | 行为不变（本地时间格式），实现可替换      |
| 2026-10-29 | 新增 `docs/DESIGN_DOC.md`、`docs/Doxyfile.in` 与 `just docs` 配方                                                   | 双后端文档自动化                          |
