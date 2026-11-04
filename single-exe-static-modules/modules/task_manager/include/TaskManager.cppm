/**
 * @file TaskManager.cppm
 * @module TaskManager
 * @brief Stacks 2 任务业务模块。
 *
 * 职责边界：
 * - 创建看板任务与全局收件箱任务；
 * - 查询项目、Stack 和任务；
 * - 修改标题/描述、开始日期、截止日期；
 * - 完成/重新打开、归档、可恢复删除；
 * - 在本周截止范围内聚合所有已保存工作区；
 * - 启动/停止计时并写入 Stacks 2 兼容的 `timelogs`。
 *
 * 所有写操作都通过同目录临时文件 + `rename(2)` 原子替换。需要同时更新
 * `.task` 和 `.project` 的创建操作带失败回滚；删除操作移动到 `tasks/.trash`。
 *
 * 实现边界：
 * - 所有公开 API 接收强类型 ID、标题和描述，错误通过 std::expected 返回；
 * - 写入前统一取得 `.stacks-tools.lock`，避免 Alfred 与 CLI 并发破坏 JSON；
 * - 任务文件中 Stacks 原生字段保持原名（startdate/duedate/completed/timelogs）；
 * - 插件私有运行状态只写入 Timers.json，不污染 Stacks 工作区；
 * - 读取路径尽量容错，写入路径失败时明确返回 TaskError，不静默吞掉业务错误。
 */
module;

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "json.hpp"

export module TaskManager;

import std;
import InterfaceLog;

export namespace TaskManager {

// ---- 公共值类型与查询模型 ------------------------------------------------

/// 任务 ID 固定为 20 字符，变异测试会硬编码检查此协议。
inline constexpr std::size_t kTaskIdLength = 20;

/**
 * @brief 可创建任务的看板目标 ID。
 * @note Project 与 Stack 都使用这一强类型，由 TaskManager 查询索引判定 kind。
 */
struct TargetId {
  std::string value;  ///< Stacks 项目或清单 ID。
  explicit TargetId(std::string id) : value(std::move(id)) {}
};

/// 已校验任务 ID；作为所有任务读写接口的稳定主键。
struct TaskId {
  std::string value;  ///< 20 字符任务 ID。
  explicit TaskId(std::string id) : value(std::move(id)) {}
};

/// 任务标题；create/update 时仍会执行 trim 与空值校验。
struct TaskTitle {
  std::string value;  ///< 用户输入标题。
  explicit TaskTitle(std::string title) : value(std::move(title)) {}
};

/// 任务描述；允许空字符串，不改写 Markdown/HTML 内容。
struct TaskDescription {
  std::string value;  ///< 用户输入描述。
  explicit TaskDescription(std::string description) : value(std::move(description)) {}
};

/// 三个运行数据库路径；一次传参避免同类 path 参数顺序错误。
struct DatabasePaths {
  std::filesystem::path project_db{"data/Project.json"};  ///< 项目/工作区索引。
  std::filesystem::path stack_db{"data/Stack.json"};      ///< 项目/清单扁平索引。
  std::filesystem::path timer_db{"data/Timers.json"};     ///< 插件计时状态。
};

/// 创建任务所需的数据库与 Inbox 默认工作区。
struct CreateTaskOptions {
  DatabasePaths databases{};                           ///< 全部索引路径。
  std::optional<std::filesystem::path> inbox_library;  ///< Inbox 显式工作区；空则取首路径。
};

/// 创建任务输入；日期保留用户文本，由 normalize_date 在业务层校验。
struct TaskInput {
  TaskTitle title{std::string{}};              ///< 必填标题。
  TaskDescription description{std::string{}};  ///< 可空描述。
  std::optional<std::string> start_date;       ///< 可空开始日期表达式。
  std::optional<std::string> due_date;         ///< 可空截止日期表达式。
  std::string priority;                        ///< 低/中/高/紧急或空。
};

/**
 * @brief 部分更新任务；optional 表示“未提供”，clear_* 表示“显式清空”。
 * @note 不能用空 optional 同时表达“不修改”和“删除字段”，因此清空单独建模。
 */
struct TaskPatch {
  std::optional<std::string> title;        ///< 新标题。
  std::optional<std::string> description;  ///< 新描述。
  std::optional<std::string> start_date;   ///< 新开始日期表达式。
  std::optional<std::string> due_date;     ///< 新截止日期表达式。
  std::optional<std::string> priority;     ///< 新优先级表达式。
  bool clear_start_date = false;           ///< 从 Stacks 文件移除 startdate。
  bool clear_due_date = false;             ///< 从 Stacks 文件移除 duedate。
  bool clear_priority = false;             ///< 从 Stacks 文件移除 priority。
};

/// 任务列表的过滤条件；默认保留已完成任务、排除已归档任务。
struct TaskListOptions {
  std::optional<TargetId> target;  ///< 项目、清单或特殊值 inbox。
  std::string query;               ///< 标题优先、描述其次的模糊关键词。
  bool include_done = true;        ///< 是否显示已完成任务。
  bool include_archived = false;   ///< 是否显示 archived 非空任务。
};

/// 项目或清单的统一展示目标；WorkflowUI 用它构造 Alfred 条目。
struct BoardTarget {
  std::string id;                      ///< 项目/清单 ID。
  std::string project_id;              ///< 所属项目 ID。
  std::string title;                   ///< 当前层级标题。
  std::string project_title;           ///< 上层项目标题。
  std::string kind;                    ///< project | stack。
  std::filesystem::path project_file;  ///< 对应 .project 文件。
  std::filesystem::path library_path;  ///< 工作区根目录。
};

/// 跨模块使用的任务只读快照；字段名对应 Stacks 文件与 UI 展示语义。
struct TaskRecord {
  std::string id;                      ///< 任务 ID。
  std::string title;                   ///< 任务标题。
  std::string description;             ///< 任务描述。
  std::string project_id;              ///< 所属项目；Inbox 为 inbox。
  std::string project_title;           ///< 项目显示名。
  std::string board_id;                ///< backlog、inbox 或 Stack ID。
  std::string board_title;             ///< 清单显示名。
  std::string start_date;              ///< Stacks `startdate`。
  std::string due_date;                ///< Stacks `duedate`。
  std::string priority;                ///< low/medium/high/critical。
  std::string completed;               ///< 完成时间文本；重开时删除。
  std::string archived;                ///< 归档时间文本。
  bool done = false;                   ///< Stacks `done`。
  int progress = 0;                    ///< Stacks `progress` 百分比。
  std::filesystem::path file;          ///< `.task` 文件路径。
  std::filesystem::path library_path;  ///< 所属工作区。
};

/// 创建任务的稳定结果，不返回可被后续修改的内部 JSON。
struct TaskResult {
  TaskId task_id{std::string{}};    ///< 新任务 ID。
  std::filesystem::path task_file;  ///< 已写入的 `.task` 文件。
  std::string target_kind;          ///< project | stack | inbox。
  std::string target_id;            ///< 实际目标 ID。
};

/// Timers.json 中的运行中计时快照。
struct TimerRecord {
  std::string task_id;              ///< 计时任务 ID。
  std::string title;                ///< 启动时标题快照。
  std::string started_at;           ///< 本地开始时间。
  std::int64_t start_epoch = 0;     ///< Unix 秒，用于稳定计算时长。
  std::filesystem::path task_file;  ///< 对应 `.task` 文件。
};

/// 停止计时结果；timelog_id 可在任务文件中定位新记录。
struct StopTimerResult {
  TaskId task_id{std::string{}};      ///< 停止计时的任务。
  std::int64_t duration_seconds = 0;  ///< 至少 1 秒的计时时长。
  std::string timelog_id;             ///< 新 timelog 的 ID。
  std::filesystem::path task_file;    ///< 已更新的任务文件。
};

/// 可恢复删除结果；trash_file 指向 `tasks/.trash` 中的副本。
struct DeleteResult {
  TaskId task_id{std::string{}};          ///< 被删除任务。
  std::filesystem::path trash_file;       ///< 新位置。
  std::size_t project_files_updated = 0;  ///< 成功移除任务引用的项目文件数。
};

/// 周一至周日的 ISO 日期区间。
struct WeekBounds {
  std::string first;  ///< 周一 YYYY-MM-DD。
  std::string last;   ///< 周日 YYYY-MM-DD。
};

/// 任务业务的可恢复错误分类；UI 只消费 task_error_message。
enum class TaskError {
  invalid_target_id,        ///< 项目/清单 ID 含非法字符或为空。
  invalid_task_id,          ///< 任务 ID 格式非法。
  empty_title,              ///< trim 后标题为空。
  invalid_date,             ///< 日期表达式或日历日期非法。
  invalid_date_range,       ///< 开始日期晚于截止日期。
  invalid_priority,         ///< 未识别优先级文本。
  project_db_unreadable,    ///< Project.json 无法解析。
  stack_db_unreadable,      ///< Stack.json 无法解析。
  target_not_found,         ///< 索引中无目标看板。
  target_kind_unknown,      ///< 目标类型不是 project/stack。
  project_file_not_found,   ///< `.project` 路径不存在。
  project_file_unreadable,  ///< `.project` 无法解析。
  task_not_found,           ///< 全部工作区中无此任务。
  task_file_unreadable,     ///< `.task` 无法解析。
  timer_not_running,        ///< 目标任务未在计时。
  timer_already_running,    ///< 目标任务已有计时。
  timer_db_unreadable,      ///< Timers.json 无法解析。
  no_inbox_library,         ///< 没有可用于 Inbox 的工作区。
  invalid_library_path,     ///< 指定工作区不存在或不是目录。
  lock_failure,             ///< 另一个进程持写锁。
  write_failure,            ///< 写任务/索引文件失败。
  delete_failure,           ///< 项目引用回滚或移入垃圾箱失败。
};

// ---- 创建与查询 API -------------------------------------------------------

/// 生成线程安全的 20 字符任务 ID。
TaskId generate_id();

/// 在项目或清单下创建任务，并回滚“任务已写、项目未写”的半成功状态。
std::expected<TaskResult, TaskError> create_task(TargetId target_id, TaskInput input,
                                                 const CreateTaskOptions& options);

/// 在指定/默认工作区创建 Stacks `project=inbox` 任务。
std::expected<TaskResult, TaskError> create_inbox_task(TaskInput input,
                                                       const CreateTaskOptions& options);

/// 按项目名排序加载项目与清单的扁平列表。
std::expected<std::vector<BoardTarget>, TaskError> list_boards(const DatabasePaths& paths);

/// 扫描所有工作区任务并按目标、完成状态、归档状态和关键词过滤。
std::expected<std::vector<TaskRecord>, TaskError> list_tasks(const DatabasePaths& paths,
                                                             const TaskListOptions& options = {});

/// 返回指定自然周内未完成、未归档且有截止日期的任务。
std::expected<std::vector<TaskRecord>, TaskError> list_week_due(
    const DatabasePaths& paths, std::optional<std::string> anchor_date = std::nullopt);

/// 以任务 ID 在工作区索引中定位并读取任务快照。
std::expected<TaskRecord, TaskError> get_task(TaskId task_id, const DatabasePaths& paths);

// ---- 编辑与状态 API -------------------------------------------------------

/// 应用部分更新；修改前后都会验证日期区间与优先级。
std::expected<TaskRecord, TaskError> update_task(TaskId task_id, const TaskPatch& patch,
                                                 const DatabasePaths& paths);

/// 仅更新截止日期；内部复用 update_task。
std::expected<TaskRecord, TaskError> set_due_date(TaskId task_id, std::string due_date,
                                                  const DatabasePaths& paths);

/// 仅更新开始日期；内部复用 update_task。
std::expected<TaskRecord, TaskError> set_start_date(TaskId task_id, std::string start_date,
                                                    const DatabasePaths& paths);

/// 同时更新开始与截止日期，并校验区间顺序。
std::expected<TaskRecord, TaskError> set_schedule(TaskId task_id, std::string start_date,
                                                  std::string due_date, const DatabasePaths& paths);

/// 写入 Stacks 完成字段或清空完成状态。
std::expected<TaskRecord, TaskError> set_task_done(TaskId task_id, bool done,
                                                   const DatabasePaths& paths);

/// 写入 archived 时间，保留任务文件供后续恢复。
std::expected<TaskRecord, TaskError> archive_task(TaskId task_id, const DatabasePaths& paths);

/// 从所有项目引用移除任务，并把 `.task` 移入可恢复 `.trash`。
std::expected<DeleteResult, TaskError> delete_task(TaskId task_id, const DatabasePaths& paths);

// ---- 计时 API -------------------------------------------------------------

/// 在 Timers.json 中登记运行中计时；重复启动返回分类错误。
std::expected<TimerRecord, TaskError> start_timer(TaskId task_id, const DatabasePaths& paths);

/// 停止计时、写 Stacks timelog，并在第二步失败时回滚任务文件。
std::expected<StopTimerResult, TaskError> stop_timer(TaskId task_id, const DatabasePaths& paths);

/// 列出 Timers.json 中的运行中计时快照。
std::expected<std::vector<TimerRecord>, TaskError> list_timers(const DatabasePaths& paths);

// ---- 日期工具与错误映射 ---------------------------------------------------

/// 将 YYYY-MM-DD、today、tomorrow、+Nd 或英文星期解析为 ISO 日期。
std::expected<std::string, TaskError> normalize_date(
    std::string_view value, std::optional<std::string> anchor_date = std::nullopt);

/// 计算锚点日期所在周的周一/周日边界。
WeekBounds week_bounds(std::optional<std::string> anchor_date = std::nullopt);

/// 将 TaskError 映射为稳定的 CLI/Alfred 文本。
std::string_view task_error_message(TaskError error) noexcept;

/**
 * @brief 以函数对象遍历项目 backlog ID。
 * @return 成功返回字符串 ID 数量；文件损坏返回分类错误。
 * @note backlog 缺失/非数组视为空列表，不隐式修改源文件。
 */
template <std::invocable<std::string_view> Visitor>
std::expected<std::size_t, TaskError> for_each_backlog_id(const std::filesystem::path& project_file,
                                                          Visitor&& visitor) {
  std::ifstream input(project_file, std::ios::binary);
  try {
    nlohmann::json content;
    if (!(input >> content) || !content.is_object()) {
      return std::unexpected(TaskError::project_file_unreadable);
    }
    if (!content.contains("backlog") || !content["backlog"].is_array()) {
      return std::size_t{0};
    }
    std::size_t count = 0;
    for (const auto& item : content["backlog"]) {
      if (item.is_string()) {
        std::invoke(visitor, item.get_ref<const std::string&>());
        ++count;
      }
    }
    return count;
  } catch (const std::exception&) {
    return std::unexpected(TaskError::project_file_unreadable);
  }
}

}  // namespace TaskManager

namespace TaskManager {

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

/// Project.json 节点的内部视图，不进入公开 API。
struct ProjectNode {
  std::string id;         ///< 项目 ID。
  std::string title;      ///< 项目标题。
  fs::path path;          ///< `.project` 文件。
  fs::path library_path;  ///< 工作区根目录。
};

/// 任务在项目/清单层级中的成员关系缓存。
struct TaskLocation {
  std::string project_id;     ///< 所属项目。
  std::string project_title;  ///< 项目显示名。
  std::string board_id;       ///< backlog/Stack ID。
  std::string board_title;    ///< 清单显示名。
};

/// 原始 JSON 与展示快照一起返回，编辑时可保留未知 Stacks 字段。
struct LoadedTask {
  TaskRecord record;  ///< 跨模块只读快照。
  json raw;           ///< 待写回的完整任务 JSON。
};

/**
 * @brief 进程内排他写锁；析构时自动 flock unlock + close。
 * @note 使用 LOCK_NB，第二个写操作立即返回 lock_failure，而不是阻塞 Alfred。
 */
class ScopedFileLock {
 public:
  explicit ScopedFileLock(const fs::path& path) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    if (error) {
      return;
    }
    descriptor_ = ::open(path.c_str(), O_CREAT | O_RDWR, 0600);
    if (descriptor_ < 0) {
      return;
    }
    if (::flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
      ::close(descriptor_);
      descriptor_ = -1;
    }
  }

  ~ScopedFileLock() {
    if (descriptor_ >= 0) {
      ::flock(descriptor_, LOCK_UN);
      ::close(descriptor_);
    }
  }

  ScopedFileLock(const ScopedFileLock&) = delete;
  ScopedFileLock& operator=(const ScopedFileLock&) = delete;

  [[nodiscard]] bool ok() const noexcept { return descriptor_ >= 0; }

 private:
  int descriptor_ = -1;
};

/// 进程内原子序号，与时间/随机设备一起构成 ID 种子的去重分量。
std::atomic<std::uint64_t> g_sequence{0};

/// 去除首尾 ASCII 空白，保留标题/描述内部内容。
std::string trim(std::string value) {
  const auto not_space = [](unsigned char character) { return !std::isspace(character); };
  value.erase(value.begin(), std::ranges::find_if(value, not_space));
  value.erase(std::ranges::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
}

/// 仅做 ASCII 小写化，足够处理命令别名、优先级和任务搜索。
std::string lower_ascii(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

/// 校验 Stacks ID 的跨 Alfred token 安全字符集。
bool valid_identifier(std::string_view value) {
  return !value.empty() && std::ranges::all_of(value, [](char character) {
    return std::isalnum(static_cast<unsigned char>(character)) || character == '-' ||
           character == '_';
  });
}

/// 本地秒级时间，用于 Stacks UI 可读的 updated/completed/archived 字段。
std::string local_timestamp() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
  if (localtime_r(&now, &local) == nullptr) {
    return {};
  }
  char buffer[32]{};
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local) == 0) {
    return {};
  }
  return buffer;
}

/// UTC 微秒时间，用于 task.created/updated 的 ISO 兼容格式。
std::string utc_timestamp() {
  const auto now = std::chrono::system_clock::now();
  const auto micros =
      std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()) %
      std::chrono::seconds(1);
  const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
  if (gmtime_r(&seconds, &utc) == nullptr) {
    return {};
  }
  char buffer[48]{};
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &utc) == 0) {
    return {};
  }
  return std::format("{}.{:06}.000Z", buffer, micros.count());
}

/// 从本地时间戳提取 YYYY-MM-DD；转换失败时返回空串。
std::string local_date() {
  const std::string timestamp = local_timestamp();
  return timestamp.size() >= 10 ? timestamp.substr(0, 10) : std::string{};
}

/// 规范化绝对路径；不要求目标路径已存在，适配首次创建工作区目录。
fs::path normalized_path(const fs::path& value) {
  std::error_code error;
  const fs::path canonical = fs::weakly_canonical(value, error);
  if (!error) {
    return canonical;
  }
  return fs::absolute(value, error).lexically_normal();
}

/**
 * @brief 防御性读取 JSON 文件。
 * @param file 源文件。
 * @param parse_error 打开、读取或解析失败时返回的业务错误。
 */
std::expected<json, TaskError> load_json(const fs::path& file, TaskManager::TaskError parse_error) {
  std::ifstream input(file, std::ios::binary);
  try {
    json content;
    if (!(input >> content)) {
      return std::unexpected(parse_error);
    }
    return content;
  } catch (const std::exception&) {
    return std::unexpected(parse_error);
  }
}

/**
 * @brief 以同目录临时文件 + rename 原子替换 JSON。
 * @return 创建目录、写入、flush 或 rename 任一步失败均返回 false。
 */
bool atomic_write_json(const fs::path& target, const json& value) {
  std::error_code error;
  fs::create_directories(target.parent_path(), error);
  if (error) {
    return false;
  }
  const fs::path temporary =
      target.parent_path() /
      std::format(".{}.tmp.{}.{}", target.filename().string(), ::getpid(),
                  std::chrono::steady_clock::now().time_since_epoch().count());
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      return false;
    }
    output << value.dump(4) << '\n';
    output.flush();
    if (!output) {
      fs::remove(temporary, error);
      return false;
    }
  }
  fs::rename(temporary, target, error);
  if (error) {
    fs::remove(temporary, error);
    return false;
  }
  return true;
}

/// 只在目标字段为字符串时返回值，避免 nlohmann 类型异常穿透 API。
std::string string_value(const json& object, std::string_view key, std::string fallback = {}) {
  if (object.is_object() && object.contains(key) && object[key].is_string()) {
    return object[key].get<std::string>();
  }
  return fallback;
}

/// 只在目标字段为整数时返回值；浮点和字符串一律回退。
int int_value(const json& object, std::string_view key, int fallback = 0) {
  if (object.is_object() && object.contains(key) && object[key].is_number_integer()) {
    return object[key].get<int>();
  }
  return fallback;
}

/// 只在目标字段为布尔值时返回值。
bool bool_value(const json& object, std::string_view key, bool fallback = false) {
  if (object.is_object() && object.contains(key) && object[key].is_boolean()) {
    return object[key].get<bool>();
  }
  return fallback;
}

/// 从 JSON 字符串数组中移除指定任务 ID，保留其他元素顺序。
void erase_json_id(json& array, std::string_view task_id) {
  if (!array.is_array()) {
    return;
  }
  array.erase(std::remove_if(array.begin(), array.end(),
                             [task_id](const json& item) {
                               return item.is_string() &&
                                      item.get_ref<const std::string&>() == task_id;
                             }),
              array.end());
}

/// 严格解析 `YYYY-MM-DD`，并借助 year_month_day::ok 拒绝 2 月 30 日等非法日期。
std::optional<std::chrono::year_month_day> parse_iso_date(std::string_view value) {
  if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
    return std::nullopt;
  }
  int parts[3]{};
  std::size_t part = 0;
  for (std::size_t index = 0; index < value.size();) {
    if (value[index] == '-') {
      ++index;
      ++part;
      continue;
    }
    if (!std::isdigit(static_cast<unsigned char>(value[index])) || part >= 3) {
      return std::nullopt;
    }
    int number = 0;
    std::size_t digits = 0;
    while (index < value.size() && std::isdigit(static_cast<unsigned char>(value[index]))) {
      number = number * 10 + (value[index] - '0');
      ++index;
      ++digits;
    }
    if (digits == 0) {
      return std::nullopt;
    }
    parts[part] = number;
  }
  const std::chrono::year_month_day date{
      std::chrono::year{parts[0]},
      std::chrono::month{static_cast<unsigned>(parts[1])},
      std::chrono::day{static_cast<unsigned>(parts[2])},
  };
  if (!date.ok()) {
    return std::nullopt;
  }
  return date;
}

/// 将日历日期格式化为 Alfred/Stacks 共用的 ISO 文本。
std::string format_date(std::chrono::year_month_day date) {
  return std::format("{:04d}-{:02d}-{:02d}", static_cast<int>(date.year()),
                     static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()));
}

/// 优先使用显式锚点；锚点非法或缺失时回退到当前本地日期。
std::string effective_anchor(const std::optional<std::string>& anchor) {
  if (anchor) {
    const auto parsed = parse_iso_date(*anchor);
    if (parsed) {
      return *anchor;
    }
  }
  return local_date();
}

/**
 * @brief 将用户优先级表达式归一化为 Stacks 值。
 * @return 空字符串表示清除；nullopt 表示未知输入。
 */
std::optional<std::string> normalize_priority_value(std::string_view value) {
  const std::string normalized = lower_ascii(trim(std::string(value)));
  if (normalized.empty() || normalized == "none" || normalized == "default" || normalized == "无" ||
      normalized == "默认") {
    return std::string{};
  }
  if (normalized == "low" || normalized == "低" || normalized == "低优先级") {
    return "low";
  }
  if (normalized == "medium" || normalized == "mid" || normalized == "中" ||
      normalized == "中优先级") {
    return "medium";
  }
  if (normalized == "high" || normalized == "高" || normalized == "高优先级") {
    return "high";
  }
  if (normalized == "critical" || normalized == "urgent" || normalized == "紧急" ||
      normalized == "紧急优先级") {
    return "critical";
  }
  return std::nullopt;
}

/// 读取 Project.json，过滤无效/重复节点并恢复项目到工作区的归属。
std::expected<std::vector<ProjectNode>, TaskError> load_projects(const DatabasePaths& paths) {
  // Project.json 是唯一可信的项目文件索引；字段不完整或重复的节点直接跳过。
  const auto content = load_json(paths.project_db, TaskError::project_db_unreadable);
  if (!content) {
    return std::unexpected(content.error());
  }
  if (!content->is_object() || !content->contains("nodes") || !(*content)["nodes"].is_array()) {
    return std::unexpected(TaskError::project_db_unreadable);
  }

  std::vector<ProjectNode> projects;
  std::unordered_map<std::string, bool> seen;
  for (const auto& node : (*content)["nodes"]) {
    if (!node.is_object()) {
      continue;
    }
    const std::string id = string_value(node, "id");
    const std::string path = string_value(node, "path");
    // ID 必须可安全用于 Alfred token；路径必须存在，否则交由调用方忽略。
    if (!valid_identifier(id) || path.empty() || seen.contains(id)) {
      continue;
    }
    ProjectNode project{
        .id = id,
        .title = string_value(node, "title", "Untitled"),
        .path = normalized_path(path),
        .library_path = normalized_path(string_value(node, "library_path", "")),
    };
    if (project.library_path.empty()) {
      // 旧索引没有 library_path 时，以项目文件父目录作为兼容回退。
      project.library_path = project.path.parent_path();
    }
    seen.emplace(id, true);
    projects.push_back(std::move(project));
  }
  return projects;
}

/// 合并 library_paths 与项目节点推导出的工作区，确保旧索引也能扫描任务。
std::expected<std::set<fs::path>, TaskError> load_libraries(const DatabasePaths& paths) {
  // 先取显式 library_paths，再合并项目节点推导路径，兼容索引演进前后的数据。
  std::set<fs::path> libraries;
  if (fs::exists(paths.project_db)) {
    const auto content = load_json(paths.project_db, TaskError::project_db_unreadable);
    if (!content) {
      return std::unexpected(content.error());
    }
    if (content->contains("library_paths") && (*content)["library_paths"].is_array()) {
      for (const auto& item : (*content)["library_paths"]) {
        if (item.is_string() && !item.get<std::string>().empty()) {
          libraries.insert(normalized_path(item.get<std::string>()));
        }
      }
    }
  }
  const auto projects = load_projects(paths);
  if (projects) {
    for (const auto& project : *projects) {
      libraries.insert(project.library_path);
    }
  }
  return libraries;
}

/**
 * @brief 从项目文件恢复项目和清单的扁平看板列表。
 * @note 排序键为 项目标题 → 项目节点优先 → 清单标题，保证 Alfred 顺序稳定。
 */
std::expected<std::vector<BoardTarget>, TaskError> load_boards_impl(const DatabasePaths& paths) {
  // 项目索引给出文件定位；每个 .project 实际内容决定最终标题和 Stack 列表。
  const auto projects = load_projects(paths);
  if (!projects) {
    return std::unexpected(projects.error());
  }

  std::vector<BoardTarget> result;
  for (const auto& project : *projects) {
    // 文件可能被用户删除；静默跳过，让下一轮 sync 重建索引。
    if (!fs::exists(project.path)) {
      continue;
    }
    const auto project_json = load_json(project.path, TaskError::project_file_unreadable);
    // 看板列表是查询入口，单坏项目不应让整个 Alfred 页面不可用。
    if (!project_json || !project_json->is_object()) {
      continue;
    }
    const std::string project_title = string_value(*project_json, "title", project.title);
    // 项目自身也作为一级可选目标，允许任务直接进入 backlog。
    result.push_back({
        .id = project.id,
        .project_id = project.id,
        .title = project_title,
        .project_title = project_title,
        .kind = "project",
        .project_file = project.path,
        .library_path = project.library_path,
    });
    if (project_json->contains("stacks") && (*project_json)["stacks"].is_array()) {
      for (const auto& stack : (*project_json)["stacks"]) {
        if (!stack.is_object()) {
          continue;
        }
        const std::string stack_id = string_value(stack, "id");
        if (!valid_identifier(stack_id)) {
          // 无效 Stack ID 不进入 UI，避免生成不可执行的 action token。
          continue;
        }
        result.push_back({
            .id = stack_id,
            .project_id = project.id,
            .title = string_value(stack, "title", "Untitled"),
            .project_title = project_title,
            .kind = "stack",
            .project_file = project.path,
            .library_path = project.library_path,
        });
      }
    }
  }
  // 同级项目按标题稳定排序，项目节点排在所属清单之前。
  std::ranges::sort(result, {}, [](const BoardTarget& board) {
    return std::tuple(board.project_title, board.kind == "stack", board.title);
  });
  return result;
}

/// 在扁平看板列表中解析项目/清单目标，供创建任务定位 `.project` 文件。
std::expected<BoardTarget, TaskError> resolve_board(std::string_view id,
                                                    const DatabasePaths& paths) {
  // 创建任务必须找到唯一的项目/清单目标；找不到时返回 target_not_found。
  const auto boards = load_boards_impl(paths);
  if (!boards) {
    return std::unexpected(boards.error());
  }
  const auto iterator =
      std::ranges::find_if(*boards, [id](const BoardTarget& board) { return board.id == id; });
  if (iterator == boards->end()) {
    return std::unexpected(TaskError::target_not_found);
  }
  return *iterator;
}

/**
 * @brief 建立“任务 ID → 项目/清单位置”缓存。
 * @note backlog 任务与 Stack 任务都进入同一映射；重复 ID 保留首次位置。
 */
std::expected<std::unordered_map<std::string, TaskLocation>, TaskError> load_memberships(
    const DatabasePaths& paths) {
  // 成员关系用于把扫描到的 .task 映射回项目/清单；任务文件本身不保存 board id。
  std::unordered_map<std::string, TaskLocation> result;
  const auto projects = load_projects(paths);
  if (!projects) {
    return std::unexpected(projects.error());
  }
  for (const auto& project : *projects) {
    const auto content = load_json(project.path, TaskError::project_file_unreadable);
    if (!content || !content->is_object()) {
      continue;
    }
    const std::string project_title = string_value(*content, "title", project.title);
    if (content->contains("backlog") && (*content)["backlog"].is_array()) {
      // backlog 中的所有任务统一归入虚拟 board "backlog"。
      for (const auto& item : (*content)["backlog"]) {
        if (item.is_string()) {
          result.try_emplace(item.get<std::string>(),
                             TaskLocation{project.id, project_title, "backlog", "Backlog"});
        }
      }
    }
    if (!content->contains("stacks") || !(*content)["stacks"].is_array()) {
      continue;
    }
    for (const auto& stack : (*content)["stacks"]) {
      // 同一任务若被多个清单引用，保留首次出现的清单归属。
      if (!stack.is_object() || !stack.contains("tasks") || !stack["tasks"].is_array()) {
        continue;
      }
      const std::string stack_id = string_value(stack, "id");
      const std::string stack_title = string_value(stack, "title", "Untitled");
      for (const auto& item : stack["tasks"]) {
        if (item.is_string()) {
          result.try_emplace(item.get<std::string>(),
                             TaskLocation{project.id, project_title, stack_id, stack_title});
        }
      }
    }
  }
  return result;
}

/// 读取单个 `.task`：保留未知字段，并把 Stacks 原生字段映射为 TaskRecord。
std::expected<LoadedTask, TaskError> load_task_from_file(
    const fs::path& file, const std::filesystem::path& library_path,
    const std::unordered_map<std::string, TaskLocation>& memberships) {
  // 以 JSON id 为准，缺失时回退文件名 stem；两者都必须通过 ID 校验。
  const auto content = load_json(file, TaskError::task_file_unreadable);
  if (!content || !content->is_object()) {
    return std::unexpected(TaskError::task_file_unreadable);
  }
  const std::string id = string_value(*content, "id", file.stem().string());
  if (!valid_identifier(id)) {
    return std::unexpected(TaskError::invalid_task_id);
  }

  TaskLocation location{
      .project_id = string_value(*content, "project"),
      .project_title = "Inbox",
      .board_id = "inbox",
      .board_title = "Inbox",
  };
  if (const auto found = memberships.find(id); found != memberships.end()) {
    // 项目/清单成员关系优先于任务文件里的 project 字段，适配 Inbox 与旧数据。
    location = found->second;
  }

  // 仅把 Alfred/TaskManager 需要的字段复制到 snapshot；raw 保留全部未知字段。
  TaskRecord record{
      .id = id,
      .title = string_value(*content, "title"),
      .description = string_value(*content, "description"),
      .project_id = location.project_id,
      .project_title = location.project_title,
      .board_id = location.board_id,
      .board_title = location.board_title,
      .start_date = string_value(*content, "startdate"),
      .due_date = string_value(*content, "duedate"),
      .priority = string_value(*content, "priority"),
      .completed = string_value(*content, "completed"),
      .archived = string_value(*content, "archived"),
      .done = bool_value(*content, "done"),
      .progress = int_value(*content, "progress"),
      .file = normalized_path(file),
      .library_path = normalized_path(library_path),
  };
  return LoadedTask{std::move(record), std::move(*content)};
}

/// 扫描所有已保存工作区的 tasks 目录，跳过重复文件与损坏任务。
std::expected<std::vector<TaskRecord>, TaskError> scan_tasks(const DatabasePaths& paths) {
  // 扫描范围来自 Project.json 的工作区集合；每个工作区只看 tasks/ 顶层。
  const auto libraries = load_libraries(paths);
  if (!libraries) {
    return std::unexpected(libraries.error());
  }
  const auto memberships = load_memberships(paths);
  if (!memberships) {
    return std::unexpected(memberships.error());
  }

  std::vector<TaskRecord> records;
  std::set<fs::path> seen_files;
  for (const auto& library : *libraries) {
    const fs::path task_directory = library / "tasks";
    if (!fs::exists(task_directory) || !fs::is_directory(task_directory)) {
      continue;
    }
    std::error_code error;
    // directory_iterator 的单目录错误只跳过该目录，不影响其他工作区。
    for (const auto& entry : fs::directory_iterator(task_directory, error)) {
      if (error || !entry.is_regular_file(error) || entry.path().extension() != ".task") {
        continue;
      }
      const fs::path normalized = normalized_path(entry.path());
      // 同一工作区可能因相对路径别名出现两次；按规范绝对路径去重。
      if (!seen_files.insert(normalized).second) {
        continue;
      }
      const auto loaded = load_task_from_file(normalized, library, *memberships);
      if (loaded) {
        records.push_back(loaded->record);
      }
    }
  }
  return records;
}

/// 通过任务 ID 在所有工作区中查找原始 JSON，避免依赖全局唯一路径索引。
std::expected<LoadedTask, TaskError> find_task_raw(TaskId task_id, const DatabasePaths& paths) {
  // 任务 ID 已是全局主键；按工作区顺序定位文件即可，无需构造全量任务表。
  if (!valid_identifier(task_id.value)) {
    return std::unexpected(TaskError::invalid_task_id);
  }
  const auto libraries = load_libraries(paths);
  if (!libraries) {
    return std::unexpected(libraries.error());
  }
  const auto memberships = load_memberships(paths);
  if (!memberships) {
    return std::unexpected(memberships.error());
  }
  for (const auto& library : *libraries) {
    // 采用 tasks/<id>.task 约定；不递归搜索，避免误命中 .trash 中的副本。
    const fs::path candidate = library / "tasks" / (task_id.value + ".task");
    if (fs::exists(candidate)) {
      return load_task_from_file(candidate, library, *memberships);
    }
  }
  return std::unexpected(TaskError::task_not_found);
}

/**
 * @brief 原子写回编辑后的任务，并重新读取规范化快照。
 * @note 时间戳在写前统一刷新；失败不会返回半更新的 TaskRecord。
 */
std::expected<TaskRecord, TaskError> save_task(TaskId task_id, json& raw,
                                               const DatabasePaths& paths) {
  // 写回前刷新 updated；随后重新读取，确保返回值反映磁盘上的最终字段。
  raw["updated"] = local_timestamp();
  const auto task = find_task_raw(task_id, paths);
  if (!task) {
    return std::unexpected(task.error());
  }
  if (!atomic_write_json(task->record.file, raw)) {
    return std::unexpected(TaskError::write_failure);
  }
  // 重新加载成员关系，避免调用方拿到只更新了一半的 TaskRecord。
  const auto libraries = load_libraries(paths);
  const auto memberships = load_memberships(paths);
  if (!libraries || !memberships) {
    return std::unexpected(!libraries ? libraries.error() : memberships.error());
  }
  return load_task_from_file(task->record.file, task->record.library_path, *memberships)
      .transform([](LoadedTask&& loaded) { return std::move(loaded.record); });
}

/**
 * @brief 判断 needle 是否为 haystack 的有序子序列。
 * @note 用于用户漏打中间字符的场景，例如 `prjone` 匹配 `Project One`。
 */
bool fuzzy_subsequence(std::string_view haystack, std::string_view needle) {
  if (needle.empty()) {
    return true;
  }
  std::size_t cursor = 0;
  for (const char character : haystack) {
    if (cursor < needle.size() && character == needle[cursor]) {
      ++cursor;
    }
  }
  return cursor == needle.size();
}

/**
 * @brief 计算标题/描述模糊匹配得分，分值越高越靠前。
 *
 * 排序优先级固定为：
 * 1. 标题完全等于关键词；
 * 2. 标题前缀、子串、子序列；
 * 3. 描述完全等于关键词；
 * 4. 描述前缀、子串、子序列。
 *
 * 因此“标题命中”始终排在“仅描述命中”之前；返回 0 表示不匹配。
 */
int task_match_score(const TaskRecord& task, std::string_view query) {
  if (query.empty()) {
    return 1;
  }
  const std::string needle = lower_ascii(trim(std::string(query)));
  if (needle.empty()) {
    return 1;
  }
  const std::string title = lower_ascii(task.title);
  const std::string description = lower_ascii(task.description);

  if (title == needle) {
    return 800;
  }
  if (title.starts_with(needle)) {
    return 700;
  }
  if (title.find(needle) != std::string::npos) {
    return 600;
  }
  if (fuzzy_subsequence(title, needle)) {
    return 500;
  }
  if (description == needle) {
    return 400;
  }
  if (description.starts_with(needle)) {
    return 300;
  }
  if (description.find(needle) != std::string::npos) {
    return 200;
  }
  if (fuzzy_subsequence(description, needle)) {
    return 100;
  }
  return 0;
}

}  // namespace

TaskId generate_id() {
  // 64 字符字母表恰好让每个字符占用 6 bit；ID 可安全用于 Alfred token 和文件名。
  static constexpr std::string_view kAlphabet =
      "-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz";
  static_assert(kAlphabet.size() == 64);

  const auto now_ns =
      static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  std::string id(kTaskIdLength, ' ');

  // 前 8 字符编码 steady_clock 低位：提供时间局部性，但不承担唯一性保证。
  auto time_bits = now_ns;
  for (std::size_t index = 0; index < 8; ++index) {
    id[index] = kAlphabet[static_cast<std::size_t>(time_bits & 63U)];
    time_bits >>= 6;
  }

  // 后 12 字符使用 seed_seq 混合时间、进程内序号和 random_device。
  // seed_seq 消除多个低熵来源之间的相关性，降低并发碰撞概率。
  const std::uint64_t sequence = g_sequence.fetch_add(1, std::memory_order_relaxed);
  std::random_device random;
  const auto mix = [](std::uint64_t value) {
    return static_cast<std::uint32_t>(value ^ (value >> 32));
  };
  std::seed_seq seed{mix(now_ns), mix(sequence), random()};
  std::mt19937_64 generator(seed);
  std::uniform_int_distribution<int> distribution(0, 63);
  for (std::size_t index = 8; index < id.size(); ++index) {
    id[index] = kAlphabet[static_cast<std::size_t>(distribution(generator))];
  }
  return TaskId(std::move(id));
}

std::expected<TaskResult, TaskError> create_task(TargetId target_id, TaskInput input,
                                                 const CreateTaskOptions& options) {
  // 先冻结输入摘要；后续 raw JSON 可能移动/修改局部值，不影响日志可追踪性。
  const std::string input_log = std::format(
      "target={} title={} start={} due={} priority={}", target_id.value, input.title.value,
      input.start_date.value_or(""), input.due_date.value_or(""), input.priority);
  const auto operation = [&]() -> std::expected<TaskResult, TaskError> {
    // 校验阶段：ID、标题、优先级、目标看板和项目文件逐项失败即返回。
    if (!valid_identifier(target_id.value)) {
      return std::unexpected(TaskError::invalid_target_id);
    }
    const std::string title = trim(input.title.value);
    if (title.empty()) {
      return std::unexpected(TaskError::empty_title);
    }
    const auto priority = normalize_priority_value(input.priority);
    if (!priority) {
      // 未知优先级直接拒绝，不能静默降级为“无优先级”。
      return std::unexpected(TaskError::invalid_priority);
    }
    const auto board = resolve_board(target_id.value, options.databases);
    if (!board) {
      return std::unexpected(board.error());
    }
    if (!fs::exists(board->project_file)) {
      return std::unexpected(TaskError::project_file_not_found);
    }
    const auto project_json = load_json(board->project_file, TaskError::project_file_unreadable);
    if (!project_json || !project_json->is_object()) {
      return std::unexpected(TaskError::project_file_unreadable);
    }

    // 日期统一在领域层归一化；UI/CLI 只传原始表达式。
    std::optional<std::string> start;
    std::optional<std::string> due;
    if (input.start_date) {
      auto normalized = normalize_date(*input.start_date);
      if (!normalized) {
        return std::unexpected(normalized.error());
      }
      start = *normalized;
    }
    if (input.due_date) {
      auto normalized = normalize_date(*input.due_date);
      if (!normalized) {
        return std::unexpected(normalized.error());
      }
      due = *normalized;
    }
    if (start && due && *start > *due) {
      return std::unexpected(TaskError::invalid_date_range);
    }

    // 所有跨文件写操作先取得同一把锁，避免任务与项目索引交叉损坏。
    ScopedFileLock lock(options.databases.timer_db.parent_path() / ".stacks-tools.lock");
    if (!lock.ok()) {
      return std::unexpected(TaskError::lock_failure);
    }

    json project = *project_json;
    const TaskId task_id = generate_id();
    const std::string created = utc_timestamp();
    // 项目级新任务头插 backlog；Stack 级新任务头插对应 stack.tasks。
    if (board->kind == "project") {
      // 项目节点代表 backlog；数组缺失时创建空数组，不假设旧文件结构。
      if (!project.contains("backlog") || !project["backlog"].is_array()) {
        project["backlog"] = json::array();
      }
      project["backlog"].insert(project["backlog"].begin(), task_id.value);
    } else {
      // Stack 任务只写对应 stack.tasks；缺失目标时保持文件不变并报错。
      bool inserted = false;
      if (project.contains("stacks") && project["stacks"].is_array()) {
        for (auto& stack : project["stacks"]) {
          if (string_value(stack, "id") != target_id.value) {
            continue;
          }
          if (!stack.contains("tasks") || !stack["tasks"].is_array()) {
            stack["tasks"] = json::array();
          }
          stack["tasks"].insert(stack["tasks"].begin(), task_id.value);
          inserted = true;
          break;
        }
      }
      if (!inserted) {
        return std::unexpected(TaskError::target_not_found);
      }
    }
    project["updated"] = local_timestamp();

    // 任务 JSON 保留 Stacks 已知公共字段，并把插件字段控制为向后兼容的增量。
    json task = {
        {"title", title},
        {"description", input.description.value},
        {"id", task_id.value},
        {"project", board->project_id},
        {"created", created},
        {"updated", created},
        {"archived", ""},
        {"public", true},
        {"owner", ""},
        {"visibleUsers", json::array()},
        {"visibleRoles", json::array()},
        {"tags", json::array()},
        {"assignees", json::array()},
    };
    if (start) {
      task["startdate"] = *start;
    }
    if (due) {
      task["duedate"] = *due;
    }
    // 只有非空优先级才写字段；空值表示沿用 Stacks 默认。
    if (!priority->empty()) {
      task["priority"] = *priority;
    }

    const fs::path task_directory = board->library_path / "tasks";
    const fs::path task_file = task_directory / (task_id.value + ".task");
    std::error_code error;
    fs::create_directories(task_directory, error);

    // 两阶段提交：先写 .task，再写 .project；第二步失败删除第一步文件。
    if (error || !atomic_write_json(task_file, task)) {
      return std::unexpected(TaskError::write_failure);
    }
    if (!atomic_write_json(board->project_file, project)) {
      fs::remove(task_file, error);
      return std::unexpected(TaskError::write_failure);
    }
    return TaskResult{task_id, task_file, board->kind, board->id};
  }();

  // 只在操作结束后记录一次输入/输出，避免错误分支重复刷日志。
  const bool ok = operation.has_value();
  const std::string output =
      ok ? std::format("id={} kind={} member={} file={}", operation->task_id.value,
                       operation->target_kind,
                       operation->target_kind == "project" ? "backlog" : "stack.tasks",
                       operation->task_file.string())
         : std::string(task_error_message(operation.error()));
  interface_log::record(interface_log::ApiName("TaskManager::create_task"),
                        interface_log::LogText(input_log), interface_log::LogText(output),
                        ok ? interface_log::ApiStatus::ok : interface_log::ApiStatus::failed);
  return operation;
}

std::expected<TaskResult, TaskError> create_inbox_task(TaskInput input,
                                                       const CreateTaskOptions& options) {
  // Inbox 不依赖项目索引；它直接在选定工作区 tasks/ 下创建 project=inbox 的任务。
  const std::string input_log =
      std::format("target=inbox title={} start={} due={} priority={}", input.title.value,
                  input.start_date.value_or(""), input.due_date.value_or(""), input.priority);
  const auto operation = [&]() -> std::expected<TaskResult, TaskError> {
    const std::string title = trim(input.title.value);
    if (title.empty()) {
      return std::unexpected(TaskError::empty_title);
    }
    const auto priority = normalize_priority_value(input.priority);
    if (!priority) {
      return std::unexpected(TaskError::invalid_priority);
    }
    fs::path library;
    // 显式工作区优先；未指定时沿用已保存路径集合的第一个路径。
    if (options.inbox_library && !options.inbox_library->empty()) {
      library = normalized_path(*options.inbox_library);
    } else {
      const auto libraries = load_libraries(options.databases);
      if (!libraries) {
        return std::unexpected(libraries.error());
      }
      if (libraries->empty()) {
        return std::unexpected(TaskError::no_inbox_library);
      }
      library = *libraries->begin();
    }
    if (!fs::exists(library) || !fs::is_directory(library)) {
      return std::unexpected(TaskError::invalid_library_path);
    }

    // Inbox 与看板任务共享同一日期/优先级验证规则。
    std::optional<std::string> start;
    std::optional<std::string> due;
    if (input.start_date) {
      auto normalized = normalize_date(*input.start_date);
      if (!normalized) {
        return std::unexpected(normalized.error());
      }
      start = *normalized;
    }
    if (input.due_date) {
      auto normalized = normalize_date(*input.due_date);
      if (!normalized) {
        return std::unexpected(normalized.error());
      }
      due = *normalized;
    }
    if (start && due && *start > *due) {
      return std::unexpected(TaskError::invalid_date_range);
    }

    ScopedFileLock lock(options.databases.timer_db.parent_path() / ".stacks-tools.lock");
    if (!lock.ok()) {
      return std::unexpected(TaskError::lock_failure);
    }
    const TaskId task_id = generate_id();
    const std::string created = utc_timestamp();
    // project=inbox 是 Stacks 识别全局收件箱任务的关键语义，不写 backlog。
    json task = {
        {"title", title},
        {"description", input.description.value},
        {"id", task_id.value},
        {"project", "inbox"},
        {"created", created},
        {"updated", created},
        {"archived", ""},
        {"public", true},
        {"owner", ""},
        {"visibleUsers", json::array()},
        {"visibleRoles", json::array()},
        {"tags", json::array()},
        {"assignees", json::array()},
    };
    if (start) {
      task["startdate"] = *start;
    }
    if (due) {
      task["duedate"] = *due;
    }
    // Inbox 文件与其他任务文件结构一致，仅 project 固定为 inbox、无 board 引用。
    if (!priority->empty()) {
      task["priority"] = *priority;
    }

    // Inbox 只需要写入单个任务文件，但仍受统一排他锁保护。
    const fs::path task_file = library / "tasks" / (task_id.value + ".task");
    std::error_code error;
    fs::create_directories(task_file.parent_path(), error);
    if (error || !atomic_write_json(task_file, task)) {
      return std::unexpected(TaskError::write_failure);
    }
    return TaskResult{task_id, task_file, "inbox", "inbox"};
  }();

  const bool ok = operation.has_value();
  const std::string output =
      ok ? std::format("id={} file={}", operation->task_id.value, operation->task_file.string())
         : std::string(task_error_message(operation.error()));
  interface_log::record(interface_log::ApiName("TaskManager::create_inbox_task"),
                        interface_log::LogText(input_log), interface_log::LogText(output),
                        ok ? interface_log::ApiStatus::ok : interface_log::ApiStatus::failed);
  return operation;
}

std::expected<std::vector<BoardTarget>, TaskError> list_boards(const DatabasePaths& paths) {
  // 查询成功与失败都记录数量/分类，方便 Alfred Debugger 对齐 UI 空列表原因。
  auto result = load_boards_impl(paths);
  interface_log::record(
      interface_log::ApiName("TaskManager::list_boards"),
      interface_log::LogText(std::format("project_db={}", paths.project_db.string())),
      interface_log::LogText(result ? std::format("boards={}", result->size())
                                    : std::string(task_error_message(result.error()))),
      result ? interface_log::ApiStatus::ok : interface_log::ApiStatus::failed);
  return result;
}

std::expected<std::vector<TaskRecord>, TaskError> list_tasks(const DatabasePaths& paths,
                                                             const TaskListOptions& options) {
  // 输入中只记录过滤条件，不记录全量任务正文，控制日志体积与隐私暴露。
  const std::string input =
      std::format("target={} query={} include_done={} include_archived={}",
                  options.target ? options.target->value : std::string{}, options.query,
                  options.include_done, options.include_archived);
  const auto operation = [&]() -> std::expected<std::vector<TaskRecord>, TaskError> {
    // 全量扫描后使用纯内存过滤；数据规模适合个人 Stacks 工作区。
    auto records = scan_tasks(paths);
    if (!records) {
      return std::unexpected(records.error());
    }

    std::optional<BoardTarget> target;
    if (options.target) {
      // inbox 是虚拟看板，不需要在 Project.json 中出现。
      if (options.target->value == "inbox") {
        target = BoardTarget{.id = "inbox",
                             .project_id = "inbox",
                             .title = "Inbox",
                             .project_title = "Inbox",
                             .kind = "inbox",
                             .project_file = {},
                             .library_path = {}};
      } else {
        auto resolved = resolve_board(options.target->value, paths);
        if (!resolved) {
          return std::unexpected(resolved.error());
        }
        target = std::move(*resolved);
      }
    }

    // 过滤顺序：完成态 → 归档态 → 目标层级 → 标题/描述模糊匹配。
    std::unordered_map<std::string, int> match_scores;
    std::erase_if(*records, [&](const TaskRecord& task) {
      if (!options.include_done && task.done) {
        return true;
      }
      if (!options.include_archived && !task.archived.empty()) {
        return true;
      }
      if (target) {
        if (target->kind == "inbox" && task.project_id != "inbox") {
          return true;
        }
        if (target->kind == "project" && task.project_id != target->id) {
          return true;
        }
        if (target->kind == "stack" && task.board_id != target->id) {
          return true;
        }
      }
      const int score = task_match_score(task, options.query);
      if (score == 0) {
        return true;
      }
      match_scores[task.id] = score;
      return false;
    });

    // 有搜索词时先按标题/描述命中层级排序；同层级再沿用截止日期与标题的稳定顺序。
    std::ranges::sort(*records, [&](const TaskRecord& left, const TaskRecord& right) {
      const int left_score = match_scores[left.id];
      const int right_score = match_scores[right.id];
      if (left_score != right_score) {
        return left_score > right_score;
      }
      return std::tuple(left.due_date.empty(), left.due_date, left.title) <
             std::tuple(right.due_date.empty(), right.due_date, right.title);
    });
    return records;
  }();

  const bool ok = operation.has_value();
  interface_log::record(
      interface_log::ApiName("TaskManager::list_tasks"), interface_log::LogText(input),
      interface_log::LogText(
          ok ? std::format("tasks={} rank=title_then_description", operation->size())
             : std::string(task_error_message(operation.error()))),
      ok ? interface_log::ApiStatus::ok : interface_log::ApiStatus::failed);
  return operation;
}

std::expected<std::vector<TaskRecord>, TaskError> list_week_due(
    const DatabasePaths& paths, std::optional<std::string> anchor_date) {
  // 本周边界固定为 ISO 周一至周日；先复用通用列表过滤排除完成/归档。
  const WeekBounds bounds = week_bounds(anchor_date);
  TaskListOptions options;
  options.include_done = false;
  options.include_archived = false;
  auto tasks = list_tasks(paths, options);
  if (!tasks) {
    return std::unexpected(tasks.error());
  }

  // 日期是规范化 YYYY-MM-DD，可直接按字典序比较，无需逐项构造 time_point。
  std::erase_if(*tasks, [&](const TaskRecord& task) {
    return task.due_date.empty() || task.due_date < bounds.first || task.due_date > bounds.last;
  });
  std::ranges::sort(*tasks, {}, &TaskRecord::due_date);
  interface_log::record(
      interface_log::ApiName("TaskManager::list_week_due"),
      interface_log::LogText(std::format("first={} last={}", bounds.first, bounds.last)),
      interface_log::LogText(std::format("tasks={}", tasks->size())), interface_log::ApiStatus::ok);
  return tasks;
}

std::expected<TaskRecord, TaskError> get_task(TaskId task_id, const DatabasePaths& paths) {
  // 查询命中返回标题摘要；未命中返回稳定错误分类，不向 UI 抛异常。
  auto task = find_task_raw(task_id, paths);
  interface_log::record(
      interface_log::ApiName("TaskManager::get_task"),
      interface_log::LogText(std::format("task_id={}", task_id.value)),
      interface_log::LogText(task ? std::format("title={}", task->record.title)
                                  : std::string(task_error_message(task.error()))),
      task ? interface_log::ApiStatus::ok : interface_log::ApiStatus::failed);
  if (!task) {
    return std::unexpected(task.error());
  }
  return task->record;
}

std::expected<TaskRecord, TaskError> update_task(TaskId task_id, const TaskPatch& patch,
                                                 const DatabasePaths& paths) {
  const auto operation = [&]() -> std::expected<TaskRecord, TaskError> {
    // 读改写全过程持锁，避免两个 Alfred action 交错覆盖同一任务。
    ScopedFileLock lock(paths.timer_db.parent_path() / ".stacks-tools.lock");
    if (!lock.ok()) {
      return std::unexpected(TaskError::lock_failure);
    }
    auto loaded = find_task_raw(task_id, paths);
    if (!loaded) {
      return std::unexpected(loaded.error());
    }
    json& task = loaded->raw;

    // 标题/描述按字段存在性应用；缺失 optional 表示“不修改”。
    if (patch.title) {
      const std::string title = trim(*patch.title);
      if (title.empty()) {
        return std::unexpected(TaskError::empty_title);
      }
      task["title"] = title;
    }
    if (patch.description) {
      task["description"] = *patch.description;
    }

    // clear_* 优先于新值：显式清空是独立语义，不能用空字符串代替。
    if (patch.clear_start_date) {
      task.erase("startdate");
    } else if (patch.start_date) {
      auto value = normalize_date(*patch.start_date);
      if (!value) {
        return std::unexpected(value.error());
      }
      task["startdate"] = *value;
    }
    if (patch.clear_due_date) {
      task.erase("duedate");
    } else if (patch.due_date) {
      auto value = normalize_date(*patch.due_date);
      if (!value) {
        return std::unexpected(value.error());
      }
      task["duedate"] = *value;
    }
    if (patch.clear_priority) {
      task.erase("priority");
    } else if (patch.priority) {
      const auto value = normalize_priority_value(*patch.priority);
      if (!value) {
        return std::unexpected(TaskError::invalid_priority);
      }
      if (value->empty()) {
        task.erase("priority");
      } else {
        task["priority"] = *value;
      }
    }

    // 所有字段应用后再做跨字段日期区间校验，支持一次 patch 同时改两端。
    const std::string start = string_value(task, "startdate");
    const std::string due = string_value(task, "duedate");
    if (!start.empty() && !due.empty() && start > due) {
      return std::unexpected(TaskError::invalid_date_range);
    }
    return save_task(task_id, task, paths);
  }();

  const bool ok = operation.has_value();
  std::string changed_fields;
  const auto append_change = [&](std::string_view field) {
    if (!changed_fields.empty()) {
      changed_fields += ',';
    }
    changed_fields += field;
  };
  if (patch.title) {
    append_change("title");
  }
  if (patch.description) {
    append_change("description");
  }
  if (patch.clear_start_date || patch.start_date) {
    append_change("startdate");
  }
  if (patch.clear_due_date || patch.due_date) {
    append_change("duedate");
  }
  if (patch.clear_priority || patch.priority) {
    append_change("priority");
  }
  interface_log::record(
      interface_log::ApiName("TaskManager::update_task"),
      interface_log::LogText(std::format(
          "task_id={} title={} description={} start={} due={} priority={}", task_id.value,
          patch.title.value_or(""), patch.description.value_or(""), patch.start_date.value_or(""),
          patch.due_date.value_or(""), patch.priority.value_or(""))),
      interface_log::LogText(ok ? std::format("changed={} record_title={} updated=set",
                                              changed_fields, operation->title)
                                : std::string(task_error_message(operation.error()))),
      ok ? interface_log::ApiStatus::ok : interface_log::ApiStatus::failed);
  return operation;
}

std::expected<TaskRecord, TaskError> set_due_date(TaskId task_id, std::string due_date,
                                                  const DatabasePaths& paths) {
  // 单字段 setter 只构造最小 patch，日期校验与落盘仍由 update_task 统一负责。
  TaskPatch patch;
  patch.due_date = std::move(due_date);
  auto result = update_task(task_id, patch, paths);
  interface_log::record(
      interface_log::ApiName("TaskManager::set_due_date"),
      interface_log::LogText(std::format("task_id={} value={}", task_id.value, *patch.due_date)),
      interface_log::LogText(result ? std::format("record_duedate={}", result->due_date)
                                    : std::string(task_error_message(result.error()))),
      result ? interface_log::ApiStatus::ok : interface_log::ApiStatus::failed);
  return result;
}

std::expected<TaskRecord, TaskError> set_start_date(TaskId task_id, std::string start_date,
                                                    const DatabasePaths& paths) {
  // 单字段 setter 不绕过锁、日期规范化和原子写入。
  TaskPatch patch;
  patch.start_date = std::move(start_date);
  auto result = update_task(task_id, patch, paths);
  interface_log::record(
      interface_log::ApiName("TaskManager::set_start_date"),
      interface_log::LogText(std::format("task_id={} value={}", task_id.value, *patch.start_date)),
      interface_log::LogText(result ? std::format("record_startdate={}", result->start_date)
                                    : std::string(task_error_message(result.error()))),
      result ? interface_log::ApiStatus::ok : interface_log::ApiStatus::failed);
  return result;
}

std::expected<TaskRecord, TaskError> set_schedule(TaskId task_id, std::string start_date,
                                                  std::string due_date,
                                                  const DatabasePaths& paths) {
  // 两端在同一 patch 中提交，避免分别更新时短暂出现非法区间。
  TaskPatch patch;
  patch.start_date = std::move(start_date);
  patch.due_date = std::move(due_date);
  auto result = update_task(task_id, patch, paths);
  interface_log::record(
      interface_log::ApiName("TaskManager::set_schedule"),
      interface_log::LogText(std::format("task_id={} start={} due={}", task_id.value,
                                         *patch.start_date, *patch.due_date)),
      interface_log::LogText(
          result ? std::format("record={}->{}", result->start_date, result->due_date)
                 : std::string(task_error_message(result.error()))),
      result ? interface_log::ApiStatus::ok : interface_log::ApiStatus::failed);
  return result;
}

std::expected<TaskRecord, TaskError> set_task_done(TaskId task_id, bool done,
                                                   const DatabasePaths& paths) {
  const auto operation = [&]() -> std::expected<TaskRecord, TaskError> {
    // 完成状态与时间戳必须一次原子写入，避免 done=true 但 completed 为空。
    ScopedFileLock lock(paths.timer_db.parent_path() / ".stacks-tools.lock");
    if (!lock.ok()) {
      return std::unexpected(TaskError::lock_failure);
    }
    auto loaded = find_task_raw(task_id, paths);
    if (!loaded) {
      return std::unexpected(loaded.error());
    }
    json& task = loaded->raw;
    if (done) {
      // Stacks 以 done/progress/completed 三字段共同表达完成态。
      const std::string completed = local_timestamp();
      task["done"] = true;
      task["progress"] = 100;
      task["completed"] = completed;
    } else {
      // 重开时清除 completed，防止旧完成时间被 UI 误认为仍然完成。
      task["done"] = false;
      task["progress"] = 0;
      task.erase("completed");
    }
    return save_task(task_id, task, paths);
  }();

  const bool ok = operation.has_value();
  interface_log::record(
      interface_log::ApiName("TaskManager::set_task_done"),
      interface_log::LogText(std::format("task_id={} done={}", task_id.value, done)),
      interface_log::LogText(ok ? std::format("done={} progress={} completed={}", operation->done,
                                              operation->progress,
                                              operation->completed.empty() ? "clear" : "set")
                                : std::string(task_error_message(operation.error()))),
      ok ? interface_log::ApiStatus::ok : interface_log::ApiStatus::failed);
  return operation;
}

std::expected<TaskRecord, TaskError> archive_task(TaskId task_id, const DatabasePaths& paths) {
  const auto operation = [&]() -> std::expected<TaskRecord, TaskError> {
    // 归档只设置 archived 时间，不移动文件；后续仍可通过全局 ID 恢复编辑。
    ScopedFileLock lock(paths.timer_db.parent_path() / ".stacks-tools.lock");
    if (!lock.ok()) {
      return std::unexpected(TaskError::lock_failure);
    }
    auto loaded = find_task_raw(task_id, paths);
    if (!loaded) {
      return std::unexpected(loaded.error());
    }
    loaded->raw["archived"] = local_timestamp();
    return save_task(task_id, loaded->raw, paths);
  }();
  const bool ok = operation.has_value();
  interface_log::record(
      interface_log::ApiName("TaskManager::archive_task"),
      interface_log::LogText(std::format("task_id={}", task_id.value)),
      interface_log::LogText(ok ? std::format("archived={} file_kept=true", operation->archived)
                                : std::string(task_error_message(operation.error()))),
      ok ? interface_log::ApiStatus::ok : interface_log::ApiStatus::failed);
  return operation;
}

std::expected<DeleteResult, TaskError> delete_task(TaskId task_id, const DatabasePaths& paths) {
  const auto operation = [&]() -> std::expected<DeleteResult, TaskError> {
    // 删除涉及多个项目引用和任务文件移动，整个事务在工作区锁内执行。
    ScopedFileLock lock(paths.timer_db.parent_path() / ".stacks-tools.lock");
    if (!lock.ok()) {
      return std::unexpected(TaskError::lock_failure);
    }
    const auto task = find_task_raw(task_id, paths);
    if (!task) {
      return std::unexpected(task.error());
    }
    const auto projects = load_projects(paths);
    if (!projects) {
      return std::unexpected(projects.error());
    }

    struct PendingProject {
      fs::path path;
      json before;
      json after;
    };
    std::vector<PendingProject> pending;

    // 阶段 1：只构造“修改前/修改后”快照，不立刻写文件。
    for (const auto& project : *projects) {
      const auto content = load_json(project.path, TaskError::project_file_unreadable);
      if (!content || !content->is_object()) {
        continue;
      }
      json before = *content;
      json after = before;
      bool changed = false;
      if (after.contains("backlog") && after["backlog"].is_array()) {
        const std::size_t old_size = after["backlog"].size();
        erase_json_id(after["backlog"], task_id.value);
        changed = changed || old_size != after["backlog"].size();
      }
      if (after.contains("stacks") && after["stacks"].is_array()) {
        for (auto& stack : after["stacks"]) {
          if (!stack.is_object() || !stack.contains("tasks") || !stack["tasks"].is_array()) {
            continue;
          }
          const std::size_t old_size = stack["tasks"].size();
          erase_json_id(stack["tasks"], task_id.value);
          changed = changed || old_size != stack["tasks"].size();
        }
      }
      if (changed) {
        after["updated"] = local_timestamp();
        pending.push_back({normalized_path(project.path), std::move(before), std::move(after)});
      }
    }

    // 阶段 2：逐个项目原子写入；任一步失败就恢复此前成功写入的快照。
    std::vector<fs::path> written;
    for (auto& project : pending) {
      if (!atomic_write_json(project.path, project.after)) {
        for (const auto& path : written) {
          const auto original = std::ranges::find_if(
              pending, [&](const PendingProject& item) { return item.path == path; });
          if (original != pending.end()) {
            atomic_write_json(original->path, original->before);
          }
        }
        return std::unexpected(TaskError::delete_failure);
      }
      written.push_back(project.path);
    }

    const fs::path trash_directory = task->record.library_path / "tasks" / ".trash";
    std::error_code error;
    fs::create_directories(trash_directory, error);
    if (error) {
      // 无法创建垃圾箱时恢复所有项目引用，保持任务仍可见。
      for (const auto& project : pending) {
        atomic_write_json(project.path, project.before);
      }
      return std::unexpected(TaskError::delete_failure);
    }
    const fs::path trash_file =
        trash_directory / std::format("{}-{}.task", local_timestamp(), task_id.value);
    fs::rename(task->record.file, trash_file, error);
    if (error) {
      // 阶段 3 失败（rename）同样回滚项目文件，避免产生悬空任务 ID。
      for (const auto& project : pending) {
        atomic_write_json(project.path, project.before);
      }
      return std::unexpected(TaskError::delete_failure);
    }
    return DeleteResult{task_id, trash_file, pending.size()};
  }();
  const bool ok = operation.has_value();
  interface_log::record(interface_log::ApiName("TaskManager::delete_task"),
                        interface_log::LogText(std::format("task_id={}", task_id.value)),
                        interface_log::LogText(
                            ok ? std::format("refs={} file_to={}", operation->project_files_updated,
                                             operation->trash_file.string())
                               : std::string(task_error_message(operation.error()))),
                        ok ? interface_log::ApiStatus::ok : interface_log::ApiStatus::failed);
  return operation;
}

std::expected<TimerRecord, TaskError> start_timer(TaskId task_id, const DatabasePaths& paths) {
  const auto operation = [&]() -> std::expected<TimerRecord, TaskError> {
    // Timers.json 是插件私有状态；启动前确认任务仍存在于某个工作区。
    ScopedFileLock lock(paths.timer_db.parent_path() / ".stacks-tools.lock");
    if (!lock.ok()) {
      return std::unexpected(TaskError::lock_failure);
    }
    const auto task = find_task_raw(task_id, paths);
    if (!task) {
      return std::unexpected(task.error());
    }
    json timers = {{"timers", json::object()}};
    // 首次使用创建空结构；已有文件损坏时不自动覆盖，返回分类错误。
    if (fs::exists(paths.timer_db)) {
      const auto loaded = load_json(paths.timer_db, TaskError::timer_db_unreadable);
      if (!loaded || !loaded->is_object()) {
        return std::unexpected(TaskError::timer_db_unreadable);
      }
      timers = *loaded;
      if (!timers.contains("timers") || !timers["timers"].is_object()) {
        timers["timers"] = json::object();
      }
    }
    if (timers["timers"].contains(task_id.value)) {
      return std::unexpected(TaskError::timer_already_running);
    }

    // Unix 秒是可跨进程重启稳定比较的时间基准；本地时间仅用于显示。
    const auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    const TimerRecord record{
        .task_id = task_id.value,
        .title = task->record.title,
        .started_at = local_timestamp(),
        .start_epoch = epoch,
        .task_file = task->record.file,
    };
    timers["timers"][task_id.value] = {
        {"title", record.title},
        {"started_at", record.started_at},
        {"start_epoch", record.start_epoch},
        {"task_file", record.task_file.string()},
    };
    if (!atomic_write_json(paths.timer_db, timers)) {
      return std::unexpected(TaskError::write_failure);
    }
    return record;
  }();
  const bool ok = operation.has_value();
  interface_log::record(
      interface_log::ApiName("TaskManager::start_timer"),
      interface_log::LogText(std::format("task_id={}", task_id.value)),
      interface_log::LogText(ok ? std::format("started_at={}", operation->started_at)
                                : std::string(task_error_message(operation.error()))),
      ok ? interface_log::ApiStatus::ok : interface_log::ApiStatus::failed);
  return operation;
}

std::expected<StopTimerResult, TaskError> stop_timer(TaskId task_id, const DatabasePaths& paths) {
  const auto operation = [&]() -> std::expected<StopTimerResult, TaskError> {
    // 停止计时同时修改任务文件与 Timers.json，必须放在同一排他锁内。
    ScopedFileLock lock(paths.timer_db.parent_path() / ".stacks-tools.lock");
    if (!lock.ok()) {
      return std::unexpected(TaskError::lock_failure);
    }
    const auto timers_json = load_json(paths.timer_db, TaskError::timer_db_unreadable);
    if (!timers_json || !timers_json->is_object() || !timers_json->contains("timers") ||
        !(*timers_json)["timers"].is_object() ||
        !(*timers_json)["timers"].contains(task_id.value)) {
      return std::unexpected(TaskError::timer_not_running);
    }
    json timers = *timers_json;
    auto task = find_task_raw(task_id, paths);
    if (!task) {
      return std::unexpected(task.error());
    }
    // 保存原始任务 JSON，以便第二步写入 Timers.json 失败时整体回滚。
    const json original_task = task->raw;

    // 无有效 start_epoch 时按 0 处理，最终 duration 仍保证至少 1 秒。
    const json& timer = timers["timers"][task_id.value];
    const std::int64_t start_epoch =
        timer.contains("start_epoch") && timer["start_epoch"].is_number_integer()
            ? timer["start_epoch"].get<std::int64_t>()
            : 0;
    const std::int64_t now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count();
    const std::int64_t duration = std::max<std::int64_t>(1, now_epoch - start_epoch);
    const std::string timelog_id = generate_id().value;

    // people 是 Stacks 工作区级文件；缺失或损坏时用空 person，不阻断计时保存。
    const std::string person = [&]() {
      std::ifstream people_file(task->record.library_path / "people");
      try {
        json people;
        if (people_file >> people && people.is_array() && !people.empty() &&
            people[0].is_object()) {
          return string_value(people[0], "id");
        }
      } catch (const std::exception&) {
      }
      return std::string{};
    }();

    json& raw_task = task->raw;
    if (!raw_task.contains("timelogs") || !raw_task["timelogs"].is_array()) {
      raw_task["timelogs"] = json::array();
    }
    // 字段名对齐 Stacks timelog；duration 使用秒，billable/billed 固定为 false。
    raw_task["timelogs"].push_back({
        {"date", string_value(timer, "started_at", local_timestamp())},
        {"duration", duration},
        {"billable", false},
        {"billed", false},
        {"description", ""},
        {"person", person},
        {"project", task->record.project_id},
        {"id", timelog_id},
    });
    raw_task["updated"] = local_timestamp();

    if (!atomic_write_json(task->record.file, raw_task)) {
      return std::unexpected(TaskError::write_failure);
    }
    timers["timers"].erase(task_id.value);
    if (!atomic_write_json(paths.timer_db, timers)) {
      // Timers.json 写失败时撤掉已追加的 timelog，保证跨文件状态一致。
      atomic_write_json(task->record.file, original_task);
      return std::unexpected(TaskError::write_failure);
    }
    return StopTimerResult{task_id, duration, timelog_id, task->record.file};
  }();
  const bool ok = operation.has_value();
  interface_log::record(
      interface_log::ApiName("TaskManager::stop_timer"),
      interface_log::LogText(std::format("task_id={}", task_id.value)),
      interface_log::LogText(ok ? std::format("duration={} log_id={}", operation->duration_seconds,
                                              operation->timelog_id)
                                : std::string(task_error_message(operation.error()))),
      ok ? interface_log::ApiStatus::ok : interface_log::ApiStatus::failed);
  return operation;
}

std::expected<std::vector<TimerRecord>, TaskError> list_timers(const DatabasePaths& paths) {
  const auto operation = [&]() -> std::expected<std::vector<TimerRecord>, TaskError> {
    std::vector<TimerRecord> result;
    // 首次运行没有 Timers.json 是合法空状态，不创建文件。
    if (!fs::exists(paths.timer_db)) {
      return result;
    }
    const auto timers = load_json(paths.timer_db, TaskError::timer_db_unreadable);
    if (!timers || !timers->is_object() || !timers->contains("timers") ||
        !(*timers)["timers"].is_object()) {
      return std::unexpected(TaskError::timer_db_unreadable);
    }
    for (const auto& [task_id, value] : (*timers)["timers"].items()) {
      if (!value.is_object()) {
        continue;
      }
      result.push_back({
          .task_id = task_id,
          .title = string_value(value, "title"),
          .started_at = string_value(value, "started_at"),
          .start_epoch = value.contains("start_epoch") && value["start_epoch"].is_number_integer()
                             ? value["start_epoch"].get<std::int64_t>()
                             : 0,
          .task_file = string_value(value, "task_file"),
      });
    }
    // 按 started_at 排序，使 Alfred 列表与用户开始计时的顺序一致。
    std::ranges::sort(result, {}, &TimerRecord::started_at);
    return result;
  }();
  interface_log::record(
      interface_log::ApiName("TaskManager::list_timers"),
      interface_log::LogText(std::format("timer_db={}", paths.timer_db.string())),
      interface_log::LogText(operation ? std::format("timers={}", operation->size())
                                       : std::string(task_error_message(operation.error()))),
      operation ? interface_log::ApiStatus::ok : interface_log::ApiStatus::failed);
  return operation;
}

std::expected<std::string, TaskError> normalize_date(std::string_view value,
                                                     std::optional<std::string> anchor_date) {
  // 所有输入先 trim + ASCII 小写化，再区分 ISO 日期与相对表达式。
  std::string normalized = lower_ascii(trim(std::string(value)));
  if (normalized.empty()) {
    return std::unexpected(TaskError::invalid_date);
  }
  if (parse_iso_date(normalized)) {
    return normalized;
  }

  const std::string anchor = effective_anchor(anchor_date);
  const auto anchor_parsed = parse_iso_date(anchor);
  if (!anchor_parsed) {
    return std::unexpected(TaskError::invalid_date);
  }
  std::chrono::sys_days day = *anchor_parsed;
  if (normalized == "today") {
    return format_date(*anchor_parsed);
  }
  if (normalized == "tomorrow") {
    return format_date(std::chrono::year_month_day{day + std::chrono::days{1}});
  }
  if (normalized == "yesterday") {
    return format_date(std::chrono::year_month_day{day - std::chrono::days{1}});
  }
  if (normalized.starts_with('+') && normalized.size() > 1) {
    // 支持 +3 与 +3d；上限 3650 天，避免极端输入制造明显错误的日期。
    std::string digits = normalized.substr(1);
    if (digits.ends_with('d')) {
      digits.pop_back();
    }
    int days = 0;
    const auto [pointer, error] =
        std::from_chars(digits.data(), digits.data() + digits.size(), days);
    if (error == std::errc{} && pointer == digits.data() + digits.size() && days >= 0 &&
        days <= 3650) {
      return format_date(std::chrono::year_month_day{day + std::chrono::days{days}});
    }
  }

  constexpr std::array<std::string_view, 7> weekdays{"monday", "tuesday",  "wednesday", "thursday",
                                                     "friday", "saturday", "sunday"};
  // 英文星期表示“下一次该星期几”；与周一为 0 的内部索引对齐。
  const auto weekday_it = std::ranges::find(weekdays, normalized);
  if (weekday_it != weekdays.end()) {
    const int current = static_cast<int>(std::chrono::weekday{*anchor_parsed}.c_encoding());
    const int current_monday_based = (current + 6) % 7;
    const int target = static_cast<int>(weekday_it - weekdays.begin());
    int delta = target - current_monday_based;
    if (delta < 0) {
      delta += 7;
    }
    return format_date(std::chrono::year_month_day{day + std::chrono::days{delta}});
  }
  return std::unexpected(TaskError::invalid_date);
}

WeekBounds week_bounds(std::optional<std::string> anchor_date) {
  // weekday.c_encoding: Sunday=0；先换算为 Monday=0，再回退到本周一。
  const std::string anchor = effective_anchor(anchor_date);
  const auto parsed = parse_iso_date(anchor);
  if (!parsed) {
    return {};
  }
  const int weekday_value = static_cast<int>(std::chrono::weekday{*parsed}.c_encoding());
  const int monday_based = (weekday_value + 6) % 7;
  const std::chrono::sys_days monday =
      std::chrono::sys_days{*parsed} - std::chrono::days{monday_based};
  return {
      .first = format_date(std::chrono::year_month_day{monday}),
      .last = format_date(std::chrono::year_month_day{monday + std::chrono::days{6}}),
  };
}

std::string_view task_error_message(TaskError error) noexcept {
  // 所有可恢复错误都有稳定文本；UI 不解析枚举，只显示这里的消息。
  switch (error) {
    case TaskError::invalid_target_id:
      return "target id is invalid";
    case TaskError::invalid_task_id:
      return "task id is invalid";
    case TaskError::empty_title:
      return "title is empty";
    case TaskError::invalid_date:
      return "date must be YYYY-MM-DD, today, tomorrow, +Nd or a weekday";
    case TaskError::invalid_date_range:
      return "start date must not be later than due date";
    case TaskError::invalid_priority:
      return "priority must be low, medium, high or critical";
    case TaskError::project_db_unreadable:
      return "cannot parse project database";
    case TaskError::stack_db_unreadable:
      return "cannot parse stack database";
    case TaskError::target_not_found:
      return "board target does not exist";
    case TaskError::target_kind_unknown:
      return "board target kind is unknown";
    case TaskError::project_file_not_found:
      return "project file does not exist";
    case TaskError::project_file_unreadable:
      return "cannot parse project file";
    case TaskError::task_not_found:
      return "task does not exist";
    case TaskError::task_file_unreadable:
      return "cannot parse task file";
    case TaskError::timer_not_running:
      return "timer is not running";
    case TaskError::timer_already_running:
      return "timer is already running";
    case TaskError::timer_db_unreadable:
      return "cannot parse timer database";
    case TaskError::no_inbox_library:
      return "no saved Stacks library is available for Inbox";
    case TaskError::invalid_library_path:
      return "Stacks library path is invalid";
    case TaskError::lock_failure:
      return "another Stacks Tools write is in progress";
    case TaskError::write_failure:
      return "failed to write Stacks data";
    case TaskError::delete_failure:
      return "failed to move task to trash";
  }
  return "unknown task error";
}

}  // namespace TaskManager
