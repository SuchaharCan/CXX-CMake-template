/**
 * @file main.cpp
 * @brief Stacks_Tools 单入口：只负责运行时路径发现、CLI 路由与输出。
 *
 * 单 main 约束：
 * - 只 import 已导出的静态模块，不直接实现任务/路径/提醒业务；
 * - 启动时发现源码树或 Alfred workflow 根目录，并补齐 data/log 目录；
 * - CLI 参数只做轻量拆分，日期、优先级等规则全部下沉到 TaskManager；
 * - 所有命令统一返回 ok=0、usage=1、failed=2，供 Alfred 判断成功/失败音效。
 */
#include <unistd.h>

#include <cstdio>

#include "json.hpp"

import std;
import InterfaceLog;
import ProjectSync;
import ReminderSync;
import TaskManager;
import WorkflowUI;

namespace {

namespace fs = std::filesystem;

enum class ExitCode : int {
  ok = 0,      ///< 命令完成。
  usage = 1,   ///< 参数不足或未知命令。
  failed = 2,  ///< 可恢复业务/IO 失败。
};

/// 一次进程调用共享的根目录、数据库路径和默认 Inbox 工作区。
struct Runtime {
  fs::path root;                          ///< data/log 所在根目录。
  fs::path workflow_dir;                  ///< Alfred workflow 根目录。
  TaskManager::DatabasePaths databases;   ///< 三个运行 JSON。
  std::optional<fs::path> inbox_library;  ///< library_paths[0]。

  /// 把 CLI Runtime 映射为 WorkflowUI 的只读调用上下文。
  [[nodiscard]] WorkflowUI::Context ui_context() const {
    return WorkflowUI::Context{
        .workflow_dir = workflow_dir,
        .databases = databases,
        .inbox_library = inbox_library,
    };
  }
};

/**
 * @brief 初始化 bqlog，并临时把 stdout 指向 stderr。
 * @note bqlog 初始化 banner 只能进入 Alfred Debugger，不能污染 Script Filter JSON。
 */
std::expected<std::monostate, interface_log::LogError> initialize_log(
    const std::filesystem::path& path) {
  std::fflush(stdout);
  const int stdout_copy = ::dup(STDOUT_FILENO);
  if (stdout_copy < 0) {
    return interface_log::init(path);
  }
  if (::dup2(STDERR_FILENO, STDOUT_FILENO) < 0) {
    ::close(stdout_copy);
    return interface_log::init(path);
  }
  auto result = interface_log::init(path);
  std::fflush(stdout);
  ::dup2(stdout_copy, STDOUT_FILENO);
  ::close(stdout_copy);
  return result;
}

/// 解析 argv[0] 的绝对目录；weakly_canonical 失败时回退到 absolute。
fs::path executable_directory(const char* argv0) {
  std::error_code error;
  const fs::path executable = fs::weakly_canonical(fs::absolute(argv0), error);
  return error ? fs::absolute(argv0).parent_path() : executable.parent_path();
}

/// 源码运行时向上查找 CMakeLists.txt，作为项目根目录。
fs::path find_source_root(fs::path start) {
  std::error_code error;
  start = fs::absolute(start, error);
  while (!start.empty() && start != start.root_path()) {
    if (fs::exists(start / "CMakeLists.txt")) {
      return start;
    }
    start = start.parent_path();
  }
  return fs::current_path();
}

/**
 * @brief Alfred 布尔变量的宽容解析。
 * @note “空串、0、false、off、unchecked”统一视为关闭；缺失时返回 fallback。
 */
bool environment_flag(std::string_view name, bool fallback) {
  const std::string key(name);
  const char* value = std::getenv(key.c_str());
  if (value == nullptr) {
    return fallback;
  }
  if (*value == '\0') {
    return false;
  }
  std::string normalized(value);
  std::ranges::transform(normalized, normalized.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return normalized != "0" && normalized != "false" && normalized != "off" &&
         normalized != "unchecked";
}

/**
 * @brief 首次运行补齐 data/log 和三个空 JSON。
 * @return 成功为空；失败返回可用于 stderr 的路径创建错误。
 * @note 已存在文件绝不覆盖，发布会话不会清除用户路径或任务。
 */
std::optional<std::string> ensure_runtime_layout(const fs::path& root) {
  std::error_code error;
  fs::create_directories(root / "data", error);
  if (error) {
    return std::format("cannot create data directory: {}", error.message());
  }
  fs::create_directories(root / "log", error);
  if (error) {
    return std::format("cannot create log directory: {}", error.message());
  }

  const auto create_if_missing = [](const fs::path& path, std::string_view content) {
    // 只创建缺失文件；已有空文件/用户数据保持原样。
    if (fs::exists(path)) {
      return true;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
      return false;
    }
    output << content;
    output.flush();
    return static_cast<bool>(output);
  };

  if (!create_if_missing(root / "data" / "Project.json",
                         R"({"library_paths":[],"nodes":[],"updateTime":""})")) {
    return "cannot create data/Project.json";
  }
  if (!create_if_missing(root / "data" / "Stack.json", R"({"nodes":[],"updateTime":""})")) {
    return "cannot create data/Stack.json";
  }
  if (!create_if_missing(root / "data" / "Timers.json", R"({"timers":{}})")) {
    return "cannot create data/Timers.json";
  }
  return std::nullopt;
}

/**
 * @brief 发现运行根目录、workflow 目录和默认 Inbox。
 * @note 包内运行时以 info.plist/data 判定；源码运行时向上查找 CMakeLists.txt。
 */
Runtime discover_runtime(const char* argv0) {
  const fs::path executable_dir = executable_directory(argv0);
  const char* configured_value = std::getenv("STACKS_WORKFLOW_DIR");
  const fs::path configured = configured_value != nullptr && *configured_value != '\0'
                                  ? fs::path(configured_value)
                                  : fs::path{};
  fs::path root;
  std::error_code path_error;
  const fs::path canonical_configured = fs::weakly_canonical(configured, path_error);
  // Alfred 总以二进制路径启动；源码 CLI 则允许从任意子目录执行。
  if (fs::exists(executable_dir / "data" / "Project.json") ||
      fs::exists(executable_dir / "info.plist") ||
      (!canonical_configured.empty() && !path_error && canonical_configured == executable_dir)) {
    root = executable_dir;
  } else {
    root = find_source_root(fs::current_path());
  }

  fs::path workflow_dir;
  if (!configured.empty()) {
    workflow_dir = configured;
  } else if (fs::exists(root.parent_path() / "info.plist")) {
    workflow_dir = root.parent_path();
  } else {
    workflow_dir = root;
  }

  const fs::path project_db = root / "data" / "Project.json";
  const fs::path stack_db = root / "data" / "Stack.json";
  const fs::path timer_db = root / "data" / "Timers.json";

  std::optional<fs::path> inbox_library;
  // library_paths 的第一个路径是产品约定，与 `s i` 默认工作区一致。
  std::ifstream input(project_db);
  try {
    nlohmann::json project;
    if (input >> project && project.contains("library_paths") &&
        project["library_paths"].is_array() && !project["library_paths"].empty() &&
        project["library_paths"][0].is_string()) {
      inbox_library = project["library_paths"][0].get<std::string>();
    }
  } catch (const std::exception&) {
  }

  return Runtime{
      .root = root,
      .workflow_dir = workflow_dir,
      .databases = {project_db, stack_db, timer_db},
      .inbox_library = inbox_library,
  };
}

/// 打印完整 CLI 帮助；仅用于终端调试，不进入 Alfred Script Filter 输出。
void print_usage() {
  std::println("Stacks_Tools 0.2");
  std::println("  Stacks_Tools sync <library_path>");
  std::println(
      "  Stacks_Tools add <project_or_stack_id> \"title|description\" [--start d] [--due d] "
      "[--priority p]");
  std::println("  Stacks_Tools inbox \"title|description\" [--start d] [--due d] [--priority p]");
  std::println("  Stacks_Tools boards [query]");
  std::println("  Stacks_Tools tasks [--board <id>] [--query <text>] [--todo-only]");
  std::println("  Stacks_Tools week [anchor_date]");
  std::println("  Stacks_Tools get <task_id>");
  std::println(
      "  Stacks_Tools edit <task_id> [--title t] [--description d] [--start d] [--due d] "
      "[--priority p]");
  std::println("  Stacks_Tools due <task_id> <date>");
  std::println("  Stacks_Tools start <task_id> <date>");
  std::println("  Stacks_Tools schedule <task_id> <start_date> <due_date>");
  std::println("  Stacks_Tools done|reopen|archive|delete <task_id>");
  std::println("  Stacks_Tools timer-start|timer-stop <task_id>");
  std::println("  Stacks_Tools timers");
  std::println("  Stacks_Tools sync-reminder <task_id> reminders|omnifocus|both [--dry-run]");
  std::println("  Stacks_Tools sync-week reminders|omnifocus|both [--dry-run]");
  std::println("  Stacks_Tools reminder-script reminders|omnifocus");
  std::println("  Stacks_Tools ui <query>");
  std::println("  Stacks_Tools action <action_token>");
}

/// 任务列表行的稳定 TSV 格式，便于 `awk`/人工调试。
void print_task(const TaskManager::TaskRecord& task) {
  std::println("{}\t{}\t{}\t{} / {}\t{}", task.id, task.done ? "done" : "todo",
               task.due_date.empty() ? "-" : task.due_date, task.project_title, task.board_title,
               task.title);
}

/// CLI `sync`：把新工作区交给 ProjectSync，并输出项目/清单统计。
int run_sync(const Runtime& runtime, const fs::path& path) {
  const auto result =
      ProjectSync::sync_all(path, {runtime.databases.project_db, runtime.databases.stack_db});
  if (!result) {
    std::println(std::cerr, "sync failed: {}", ProjectSync::sync_error_message(result.error()));
    return static_cast<int>(ExitCode::failed);
  }
  std::println("synced: projects={} stacks={} failed_projects={}", result->project_count,
               result->stack_count, result->failed_projects.size());
  return static_cast<int>(ExitCode::ok);
}

/**
 * @brief CLI `add`：向项目或清单创建任务。
 * @param runtime 当前运行根目录与数据库路径。
 * @param target 项目/清单 ID。
 * @param payload `title|description`。
 * @param values `--start/--due/--priority` 等额外 argv。
 * @return 成功 0；领域错误 2。
 */
int run_add(const Runtime& runtime, const std::string& target, const std::string& payload,
            const std::vector<std::string>& values) {
  // CLI 仍兼容 `title|description`，额外日期/优先级从后续 --option 读取。
  const auto separator = payload.find('|');
  const std::string title = separator == std::string::npos ? payload : payload.substr(0, separator);
  const std::string description =
      separator == std::string::npos ? std::string{} : payload.substr(separator + 1);
  std::optional<std::string> start;
  std::optional<std::string> due;
  std::string priority;
  // 这里只做 token 提取；未知/非法日期和优先级由 TaskManager 统一分类。
  for (std::size_t index = 0; index + 1 < values.size(); ++index) {
    if (values[index] == "--start") {
      start = values[index + 1];
    } else if (values[index] == "--due") {
      due = values[index + 1];
    } else if (values[index] == "--priority") {
      priority = values[index + 1];
    }
  }
  const auto result =
      TaskManager::create_task(TaskManager::TargetId(target),
                               TaskManager::TaskInput{TaskManager::TaskTitle(title),
                                                      TaskManager::TaskDescription(description),
                                                      std::move(start), std::move(due), priority},
                               {runtime.databases, runtime.inbox_library});
  if (!result) {
    // 失败文本进入 stderr，脚本层据此切换 Basso 音效；stdout 不输出半结果。
    std::println(std::cerr, "add failed: {}", TaskManager::task_error_message(result.error()));
    return static_cast<int>(ExitCode::failed);
  }
  std::println("created: id={} kind={} file={}", result->task_id.value, result->target_kind,
               result->task_file.string());
  return static_cast<int>(ExitCode::ok);
}

/**
 * @brief CLI `inbox`：在默认/显式工作区创建收件箱任务。
 * @param runtime 当前运行上下文。
 * @param payload `title|description`。
 * @param values 日期和优先级 argv。
 * @return 成功 0；领域错误 2。
 */
int run_inbox(const Runtime& runtime, const std::string& payload,
              const std::vector<std::string>& values) {
  // Inbox CLI 与 add 共享相同字段/选项语法，但目标固定为 inbox。
  const auto separator = payload.find('|');
  const std::string title = separator == std::string::npos ? payload : payload.substr(0, separator);
  const std::string description =
      separator == std::string::npos ? std::string{} : payload.substr(separator + 1);
  std::optional<std::string> start;
  std::optional<std::string> due;
  std::string priority;
  for (std::size_t index = 0; index + 1 < values.size(); ++index) {
    if (values[index] == "--start") {
      start = values[index + 1];
    } else if (values[index] == "--due") {
      due = values[index + 1];
    } else if (values[index] == "--priority") {
      priority = values[index + 1];
    }
  }
  const auto result = TaskManager::create_inbox_task(
      TaskManager::TaskInput{TaskManager::TaskTitle(title),
                             TaskManager::TaskDescription(description), std::move(start),
                             std::move(due), priority},
      {runtime.databases, runtime.inbox_library});
  if (!result) {
    std::println(std::cerr, "inbox failed: {}", TaskManager::task_error_message(result.error()));
    return static_cast<int>(ExitCode::failed);
  }
  std::println("created: id={} file={}", result->task_id.value, result->task_file.string());
  return static_cast<int>(ExitCode::ok);
}

/// 从 argv[start..] 复制额外参数；start 之后的参数所有权仍属于进程。
std::vector<std::string> arguments(int argc, char* argv[], int start) {
  std::vector<std::string> result;
  for (int index = start; index < argc; ++index) {
    result.emplace_back(argv[index]);
  }
  return result;
}

/// 返回第一个 `--option value` 的值；缺失返回 nullopt。
std::optional<std::string> option_value(const std::vector<std::string>& values,
                                        std::string_view option) {
  for (std::size_t index = 0; index + 1 < values.size(); ++index) {
    if (values[index] == option) {
      return values[index + 1];
    }
  }
  return std::nullopt;
}

/// 检查布尔开关参数是否存在，例如 `--dry-run`、`--todo-only`。
bool has_option(const std::vector<std::string>& values, std::string_view option) {
  return std::ranges::find(values, option) != values.end();
}

/**
 * @brief CLI `boards`：按显示名/ID 过滤项目与清单。
 * @param runtime 当前运行上下文。
 * @param query 空表示全部；否则做简单子串过滤。
 * @return 查询成功 0；索引损坏 2。
 */
int run_boards(const Runtime& runtime, std::string query) {
  const auto result = TaskManager::list_boards(runtime.databases);
  if (!result) {
    std::println(std::cerr, "boards failed: {}", TaskManager::task_error_message(result.error()));
    return static_cast<int>(ExitCode::failed);
  }
  for (const auto& board : *result) {
    const std::string text = std::format("{} {} {}", board.id, board.project_title, board.title);
    if (query.empty() || text.find(query) != std::string::npos) {
      std::println("{}\t{}\t{} / {}", board.id, board.kind, board.project_title, board.title);
    }
  }
  return static_cast<int>(ExitCode::ok);
}

/**
 * @brief CLI `tasks`：把 --board/--query/--todo-only 映射为过滤条件。
 * @param runtime 当前运行上下文。
 * @param values tasks 命令后的参数列表。
 * @return 成功 0；索引/任务读取失败 2。
 */
int run_tasks(const Runtime& runtime, const std::vector<std::string>& values) {
  TaskManager::TaskListOptions options;
  if (const auto board = option_value(values, "--board")) {
    options.target = TaskManager::TargetId(*board);
  }
  if (const auto query = option_value(values, "--query")) {
    options.query = *query;
  }
  options.include_done = !has_option(values, "--todo-only");
  const auto result = TaskManager::list_tasks(runtime.databases, options);
  if (!result) {
    std::println(std::cerr, "tasks failed: {}", TaskManager::task_error_message(result.error()));
    return static_cast<int>(ExitCode::failed);
  }
  for (const auto& task : *result) {
    print_task(task);
  }
  return static_cast<int>(ExitCode::ok);
}

/**
 * @brief CLI `week`：返回指定锚点周的未完成截止任务。
 * @param runtime 当前运行上下文。
 * @param anchor 可选 YYYY-MM-DD；空表示今天。
 * @return 成功 0；查询失败 2。
 */
int run_week(const Runtime& runtime, std::optional<std::string> anchor) {
  const auto result = TaskManager::list_week_due(runtime.databases, std::move(anchor));
  if (!result) {
    std::println(std::cerr, "week failed: {}", TaskManager::task_error_message(result.error()));
    return static_cast<int>(ExitCode::failed);
  }
  for (const auto& task : *result) {
    print_task(task);
  }
  return static_cast<int>(ExitCode::ok);
}

/**
 * @brief CLI `get`：输出单个任务的 Stacks 兼容字段。
 * @param runtime 当前运行上下文。
 * @param id 任务 ID。
 * @return 成功 0；任务不存在/不可读 2。
 */
int run_get(const Runtime& runtime, const std::string& id) {
  const auto result = TaskManager::get_task(TaskManager::TaskId(id), runtime.databases);
  if (!result) {
    std::println(std::cerr, "get failed: {}", TaskManager::task_error_message(result.error()));
    return static_cast<int>(ExitCode::failed);
  }
  std::println("id: {}", result->id);
  std::println("title: {}", result->title);
  std::println("description: {}", result->description);
  std::println("project: {} ({})", result->project_title, result->project_id);
  std::println("board: {} ({})", result->board_title, result->board_id);
  std::println("startdate: {}", result->start_date);
  std::println("duedate: {}", result->due_date);
  std::println("priority: {}", result->priority.empty() ? "-" : result->priority);
  std::println("done: {} progress: {} completed: {}", result->done, result->progress,
               result->completed);
  std::println("archived: {}", result->archived);
  std::println("file: {}", result->file.string());
  return static_cast<int>(ExitCode::ok);
}

/**
 * @brief CLI `edit`：构造三态 TaskPatch 并写回任务。
 * @param runtime 当前运行上下文。
 * @param id 任务 ID。
 * @param values `--title/--description/--start/--due/--priority/--clear-*`。
 * @return 成功 0；校验或写回失败 2。
 */
int run_edit(const Runtime& runtime, const std::string& id,
             const std::vector<std::string>& values) {
  TaskManager::TaskPatch patch;
  patch.title = option_value(values, "--title");
  patch.description = option_value(values, "--description");
  patch.start_date = option_value(values, "--start");
  patch.due_date = option_value(values, "--due");
  patch.priority = option_value(values, "--priority");
  patch.clear_start_date = has_option(values, "--clear-start");
  patch.clear_due_date = has_option(values, "--clear-due");
  patch.clear_priority = has_option(values, "--clear-priority");
  const auto result = TaskManager::update_task(TaskManager::TaskId(id), patch, runtime.databases);
  if (!result) {
    std::println(std::cerr, "edit failed: {}", TaskManager::task_error_message(result.error()));
    return static_cast<int>(ExitCode::failed);
  }
  print_task(*result);
  return static_cast<int>(ExitCode::ok);
}

/**
 * @brief 统一打印各种 expected 任务操作结果。
 * @note if constexpr 分派编译期 Result 类型，避免为每种 CLI 命令重复样板。
 */
template <typename Result>
int print_task_result(const std::string& command, const Result& result) {
  if (!result) {
    std::println(std::cerr, "{} failed: {}", command,
                 TaskManager::task_error_message(result.error()));
    return static_cast<int>(ExitCode::failed);
  }
  if constexpr (std::same_as<Result,
                             std::expected<TaskManager::TaskRecord, TaskManager::TaskError>>) {
    print_task(*result);
  } else if constexpr (std::same_as<Result, std::expected<TaskManager::TimerRecord,
                                                          TaskManager::TaskError>>) {
    std::println("timer started: task={} title={} at={}", result->task_id, result->title,
                 result->started_at);
  } else if constexpr (std::same_as<Result, std::expected<TaskManager::StopTimerResult,
                                                          TaskManager::TaskError>>) {
    std::println("timer stopped: duration={} log_id={} file={}", result->duration_seconds,
                 result->timelog_id, result->task_file.string());
  } else if constexpr (std::same_as<Result, std::expected<TaskManager::DeleteResult,
                                                          TaskManager::TaskError>>) {
    std::println("task moved to trash: {}", result->trash_file.string());
  }
  return static_cast<int>(ExitCode::ok);
}

/// 将 provider 文本解析为枚举；invalid 通过输出参数报告，避免抛异常。
ReminderSync::Provider parse_provider_or_default(const std::string& value, bool* valid) {
  *valid = true;
  if (value == "reminders") {
    return ReminderSync::Provider::reminders;
  }
  if (value == "omnifocus") {
    return ReminderSync::Provider::omnifocus;
  }
  if (value == "both") {
    return ReminderSync::Provider::both;
  }
  *valid = false;
  return ReminderSync::Provider::both;
}

/**
 * @brief CLI `sync-reminder`：读取任务快照并同步到指定外部应用。
 * @param runtime 当前运行上下文。
 * @param id 任务 ID。
 * @param provider_text reminders/omnifocus/both。
 * @param dry_run 只验证，不执行 AppleScript。
 * @return 成功 0；用法错误 1；同步失败 2。
 */
int run_sync_reminder(const Runtime& runtime, const std::string& id,
                      const std::string& provider_text, bool dry_run) {
  bool valid = false;
  const ReminderSync::Provider provider = parse_provider_or_default(provider_text, &valid);
  if (!valid) {
    std::println(std::cerr, "provider must be reminders, omnifocus or both");
    return static_cast<int>(ExitCode::usage);
  }
  const auto task = TaskManager::get_task(TaskManager::TaskId(id), runtime.databases);
  if (!task) {
    std::println(std::cerr, "sync failed: {}", TaskManager::task_error_message(task.error()));
    return static_cast<int>(ExitCode::failed);
  }
  ReminderSync::SyncOptions options;
  options.dry_run = dry_run;
  const auto result =
      ReminderSync::sync_task(ReminderSync::TaskReminder{.task_id = task->id,
                                                         .title = task->title,
                                                         .description = task->description,
                                                         .due_date = task->due_date,
                                                         .done = task->done},
                              provider, options);
  if (!result) {
    std::println(std::cerr, "sync failed: {}", ReminderSync::sync_error_message(result.error()));
    return static_cast<int>(ExitCode::failed);
  }
  std::println("synced: reminders={} omnifocus={} marker={}", result->reminders_ok,
               result->omnifocus_ok, result->marker);
  return static_cast<int>(ExitCode::ok);
}

/**
 * @brief CLI `sync-week`：逐个同步本周截止任务。
 * @param runtime 当前运行上下文。
 * @param provider_text reminders/omnifocus/both。
 * @param dry_run 只验证，不执行外部应用。
 * @return 全部成功 0；首个失败或其他用法错误非零。
 */
int run_sync_week(const Runtime& runtime, const std::string& provider_text, bool dry_run) {
  bool valid = false;
  const ReminderSync::Provider provider = parse_provider_or_default(provider_text, &valid);
  if (!valid) {
    std::println(std::cerr, "provider must be reminders, omnifocus or both");
    return static_cast<int>(ExitCode::usage);
  }
  const auto tasks = TaskManager::list_week_due(runtime.databases);
  if (!tasks) {
    std::println(std::cerr, "sync-week failed: {}", TaskManager::task_error_message(tasks.error()));
    return static_cast<int>(ExitCode::failed);
  }
  std::size_t succeeded = 0;
  for (const auto& task : *tasks) {
    ReminderSync::SyncOptions options;
    options.dry_run = dry_run;
    const auto result =
        ReminderSync::sync_task(ReminderSync::TaskReminder{.task_id = task.id,
                                                           .title = task.title,
                                                           .description = task.description,
                                                           .due_date = task.due_date,
                                                           .done = task.done},
                                provider, options);
    if (!result) {
      std::println(std::cerr, "sync-week failed at {}: {}", task.id,
                   ReminderSync::sync_error_message(result.error()));
      return static_cast<int>(ExitCode::failed);
    }
    ++succeeded;
  }
  std::println("sync-week ok: tasks={} dry_run={}", succeeded, dry_run);
  return static_cast<int>(ExitCode::ok);
}

/**
 * @brief CLI `ui`：把查询交给 WorkflowUI。
 * @param runtime 当前运行上下文。
 * @param query Alfred 原始查询（已完成脚本层别名转换）。
 * @return 始终 0；错误已转换为 Alfred JSON 条目。
 */
int run_ui(const Runtime& runtime, const std::string& query) {
  std::cout << WorkflowUI::filter(runtime.ui_context(), query);
  return static_cast<int>(ExitCode::ok);
}

/**
 * @brief CLI `action`：执行 Alfred 选中的 action token。
 * @param runtime 当前运行上下文。
 * @param token WorkflowUI 生成的稳定 token。
 * @return 成功 0；领域/解析错误 2。
 */
int run_action(const Runtime& runtime, const std::string& token) {
  const auto result = WorkflowUI::action(runtime.ui_context(), token);
  if (!result) {
    std::println("{}", result.error());
    return static_cast<int>(ExitCode::failed);
  }
  std::println("{}", *result);
  return static_cast<int>(ExitCode::ok);
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    // 阶段 1：无参数只打印帮助，不触碰运行时数据。
    if (argc < 2) {
      print_usage();
      return static_cast<int>(ExitCode::usage);
    }

    // 阶段 2：发现根目录并确保 data/log/三个 JSON 存在。
    Runtime runtime = discover_runtime(argv[0]);
    if (const auto layout_error = ensure_runtime_layout(runtime.root); layout_error) {
      std::println(std::cerr, "startup failed: {}", *layout_error);
      return static_cast<int>(ExitCode::failed);
    }

    // 阶段 3：按 Alfred 配置启用日志；日志失败只警告，不影响业务命令。
    const bool logging_enabled = environment_flag("STACKS_LOGGING", true);
    interface_log::set_enabled(logging_enabled);
    if (logging_enabled) {
      const auto log_init = initialize_log(runtime.root / "log" / "interface.log");
      if (!log_init) {
        std::println(std::cerr, "warning: {}; in-memory log remains active",
                     interface_log::log_error_message(log_init.error()));
      } else {
        const std::size_t removed =
            interface_log::cleanup_old_logs(runtime.root / "log" / "interface.log", 3);
        const std::vector<std::string> invocation(argv, argv + argc);
        interface_log::record(
            interface_log::ApiName("main::invoke"),
            interface_log::LogText(std::format(
                "argc={} argv={}", argc,
                invocation | std::views::join_with(' ') | std::ranges::to<std::string>())),
            interface_log::LogText(std::format("runtime={} log_cleanup_removed={} developer={}",
                                               runtime.root.string(), removed,
                                               interface_log::kDeveloperWebsite)),
            interface_log::ApiStatus::ok);
      }
    }

    const std::string command = argv[1];

    // 阶段 4：Alfred UI/action 是主要入口，保持最先匹配。
    if (command == "ui") {
      return run_ui(runtime, argc >= 3 ? argv[2] : "");
    }
    if (command == "action") {
      if (argc < 3) {
        std::println(std::cerr, "action requires an action token");
        return static_cast<int>(ExitCode::usage);
      }
      return run_action(runtime, argv[2]);
    }
    if (command == "sync") {
      if (argc < 3) {
        std::println(std::cerr, "sync requires <library_path>");
        return static_cast<int>(ExitCode::usage);
      }
      return run_sync(runtime, argv[2]);
    }
    if (command == "add") {
      if (argc < 4) {
        std::println(std::cerr, "add requires <target_id> and \"title|description\"");
        return static_cast<int>(ExitCode::usage);
      }
      return run_add(runtime, argv[2], argv[3], arguments(argc, argv, 4));
    }
    if (command == "inbox") {
      if (argc < 3) {
        std::println(std::cerr, "inbox requires \"title|description\"");
        return static_cast<int>(ExitCode::usage);
      }
      return run_inbox(runtime, argv[2], arguments(argc, argv, 3));
    }
    if (command == "boards") {
      return run_boards(runtime, argc >= 3 ? argv[2] : "");
    }
    if (command == "tasks") {
      return run_tasks(runtime, arguments(argc, argv, 2));
    }
    if (command == "week") {
      return run_week(runtime, argc >= 3 ? std::optional<std::string>(argv[2]) : std::nullopt);
    }
    if (command == "get") {
      if (argc < 3) {
        std::println(std::cerr, "get requires <task_id>");
        return static_cast<int>(ExitCode::usage);
      }
      return run_get(runtime, argv[2]);
    }
    if (command == "edit") {
      if (argc < 3) {
        std::println(std::cerr, "edit requires <task_id>");
        return static_cast<int>(ExitCode::usage);
      }
      return run_edit(runtime, argv[2], arguments(argc, argv, 3));
    }
    if (command == "due" || command == "start" || command == "schedule") {
      if (argc < 4) {
        std::println(std::cerr, "{} requires task id and date argument(s)", command);
        return static_cast<int>(ExitCode::usage);
      }
      if (command == "due") {
        return print_task_result(command, TaskManager::set_due_date(TaskManager::TaskId(argv[2]),
                                                                    argv[3], runtime.databases));
      }
      if (command == "start") {
        return print_task_result(command, TaskManager::set_start_date(TaskManager::TaskId(argv[2]),
                                                                      argv[3], runtime.databases));
      }
      if (argc < 5) {
        std::println(std::cerr, "schedule requires start date and due date");
        return static_cast<int>(ExitCode::usage);
      }
      return print_task_result(
          command, TaskManager::set_schedule(TaskManager::TaskId(argv[2]), argv[3], argv[4],
                                             runtime.databases));
    }
    if (command == "done" || command == "reopen" || command == "archive" || command == "delete") {
      if (argc < 3) {
        std::println(std::cerr, "{} requires <task_id>", command);
        return static_cast<int>(ExitCode::usage);
      }
      if (command == "delete") {
        return print_task_result(
            command, TaskManager::delete_task(TaskManager::TaskId(argv[2]), runtime.databases));
      }
      if (command == "archive") {
        return print_task_result(
            command, TaskManager::archive_task(TaskManager::TaskId(argv[2]), runtime.databases));
      }
      return print_task_result(
          command, TaskManager::set_task_done(TaskManager::TaskId(argv[2]), command == "done",
                                              runtime.databases));
    }
    if (command == "timer-start" || command == "timer-stop") {
      if (argc < 3) {
        std::println(std::cerr, "{} requires <task_id>", command);
        return static_cast<int>(ExitCode::usage);
      }
      if (command == "timer-start") {
        return print_task_result(
            command, TaskManager::start_timer(TaskManager::TaskId(argv[2]), runtime.databases));
      }
      return print_task_result(
          command, TaskManager::stop_timer(TaskManager::TaskId(argv[2]), runtime.databases));
    }
    if (command == "timers") {
      const auto timers = TaskManager::list_timers(runtime.databases);
      if (!timers) {
        std::println(std::cerr, "timers failed: {}",
                     TaskManager::task_error_message(timers.error()));
        return static_cast<int>(ExitCode::failed);
      }
      for (const auto& timer : *timers) {
        std::println("{}\t{}\t{}", timer.task_id, timer.started_at, timer.title);
      }
      return static_cast<int>(ExitCode::ok);
    }
    if (command == "sync-reminder") {
      if (argc < 4) {
        std::println(std::cerr, "sync-reminder requires <task_id> reminders|omnifocus|both");
        return static_cast<int>(ExitCode::usage);
      }
      return run_sync_reminder(runtime, argv[2], argv[3],
                               has_option(arguments(argc, argv, 4), "--dry-run"));
    }
    if (command == "sync-week") {
      if (argc < 3) {
        std::println(std::cerr, "sync-week requires reminders|omnifocus|both");
        return static_cast<int>(ExitCode::usage);
      }
      return run_sync_week(runtime, argv[2], has_option(arguments(argc, argv, 3), "--dry-run"));
    }
    if (command == "reminder-script") {
      if (argc < 3 ||
          (std::string_view(argv[2]) != "reminders" && std::string_view(argv[2]) != "omnifocus")) {
        std::println(std::cerr, "reminder-script requires reminders|omnifocus");
        return static_cast<int>(ExitCode::usage);
      }
      std::println("{}", std::string_view(argv[2]) == "reminders"
                             ? ReminderSync::build_reminders_script()
                             : ReminderSync::build_omnifocus_script());
      return static_cast<int>(ExitCode::ok);
    }
    if (command == "list") {
      if (argc < 3) {
        std::println(std::cerr, "list requires <project_file>");
        return static_cast<int>(ExitCode::usage);
      }
      std::size_t count = 0;
      const auto result = TaskManager::for_each_backlog_id(argv[2], [&count](std::string_view id) {
        std::println("- {}", id);
        ++count;
      });
      if (!result) {
        std::println(std::cerr, "list failed: {}", TaskManager::task_error_message(result.error()));
        return static_cast<int>(ExitCode::failed);
      }
      std::println("backlog tasks: {}", count);
      return static_cast<int>(ExitCode::ok);
    }

    std::println(std::cerr, "unknown command: {}", command);
    print_usage();
    return static_cast<int>(ExitCode::usage);
  } catch (const std::exception& error) {
    // main 是最后的异常边界；把意外错误转换为稳定退出码 2。
    std::println(std::cerr, "fatal: {}", error.what());
    return static_cast<int>(ExitCode::failed);
  }
}
