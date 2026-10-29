/**
 * @file TaskManager.cppm
 * @module TaskManager
 * @brief 任务创建模块：为 Project / Stack 目标创建 .task 文件并生成稳定任务 ID（STATIC 模块库）。
 *
 * 设计依据（详见 docs/DESIGN_DOC.md）：
 * - 类型即注释：ProjectId / TaskId / TaskTitle / TaskDescription 全部强类型，
 *   杜绝把"目标 ID、标题、描述"三个 string 传错顺序；数据库路径用
 *   CreateTaskOptions 结构体一次传参。
 * - 错误分类：所有可恢复错误用 std::expected<TaskResult, TaskError> 返回，
 *   每个错误分支立即 return（提前返回），API 边界不抛异常。
 * - 函数对象设计：for_each_backlog_id 接受任意可调用对象（模板参数），
 *   不引入 std::function 的类型擦除开销；调用方可传入带状态的 lambda。
 * - 无 UB：
 *   - ID 生成只对 uint64_t 做无符号右移 / 按位与（无符号运算保证回绕，
 *     不触发有符号溢出 UB）；
 *   - 随机数使用 std::mt19937_64 + std::uniform_int_distribution，
 *     并用 std::seed_seq 混合时间戳、进程内原子序号与 random_device；
 *   - JSON 访问一律先 contains() 再 is_array() 再 value()，
 *     只对"写入新元素"使用 operator[]，绝不隐式创建节点；
 *   - 进程内共享计数器用 std::atomic 保护（无数据竞争）。
 * - C++26 可读 assert：不变式检查使用 assert(cond && "message") 形式，
 *   关闭 NDEBUG 时提供可读诊断。
 * - 初始化纪律：结构体成员全部给默认初始化器，杜绝 default-init 留下
 *   indeterminate 值（C++26 起读未初始化变量属 erroneous behaviour）。
 *
 * 模块独立性：本模块只依赖 std 与 InterfaceLog，不依赖 ProjectSync。
 */
module;

#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "json.hpp"

export module TaskManager;

import std;
import InterfaceLog;

export namespace TaskManager {

/// 任务 ID 固定长度（20 字符：8 个时间字符 + 12 个随机字符）。
inline constexpr std::size_t kTaskIdLength = 20;

/**
 * @brief 目标（Project 或 Stack）ID 强类型。
 * 语义：调用方必须显式构造 ProjectId，避免把标题/描述等任意字符串
 * 误当目标 ID 传入 create_task。
 */
struct ProjectId {
  std::string value;
  explicit ProjectId(std::string id) : value(std::move(id)) {}
};

/**
 * @brief 任务 ID 强类型（generate_id / create_task 的返回载体）。
 * 语义：拿到 TaskId 即代表"这是一个合法任务标识"，而不是任意 string。
 */
struct TaskId {
  std::string value;
  explicit TaskId(std::string id) : value(std::move(id)) {}
};

/**
 * @brief 任务标题强类型。
 * 语义：与 ProjectId 一样显式构造，参数顺序在编译期被类型约束。
 */
struct TaskTitle {
  std::string value;
  explicit TaskTitle(std::string title) : value(std::move(title)) {}
};

/**
 * @brief 任务描述强类型（允许为空串）。
 */
struct TaskDescription {
  std::string value;
  explicit TaskDescription(std::string description) : value(std::move(description)) {}
};

/**
 * @brief create_task 的数据库路径参数（类型即注释：一次传参，避免两个
 * string 顺序颠倒）。
 */
struct CreateTaskOptions {
  std::filesystem::path project_db{"data/Project.json"};  ///< 项目索引库
  std::filesystem::path stack_db{"data/Stack.json"};      ///< Stack 索引库
};

/// 任务创建可恢复错误码。
enum class TaskError {
  empty_target_id,          ///< 目标 ID 为空
  empty_title,              ///< 标题为空
  stack_db_missing,         ///< Stack 索引库不存在
  stack_db_unreadable,      ///< Stack 索引库无法解析
  stack_db_no_nodes,        ///< Stack 索引库缺少 nodes 数组
  target_not_found,         ///< 目标 ID 在索引库中不存在
  project_db_unreadable,    ///< 项目索引库无法解析
  project_file_not_found,   ///< .project 文件路径缺失或不存在
  project_file_unreadable,  ///< .project 文件无法解析
  write_failure,            ///< 写 .task / .project 文件失败
};

/**
 * @brief 任务创建成功结果（返回结构体而非一堆出参 / pair）。
 */
struct TaskResult {
  TaskId task_id{std::string{}};       ///< 新任务 ID
  std::filesystem::path task_file;     ///< .task 文件路径
  std::string target_kind{"project"};  ///< "project" | "stack"
};

/**
 * @brief 生成 20 字符任务 ID（强类型 TaskId 返回）。
 * @return 8 个时间字符 + 12 个随机字符；字母表为
 *         -0123456789A-Z_a-z（64 字符）
 * @note 线程安全；随机部分使用 seed_seq 混合时间戳与原子序号，
 *       进程内并发调用几乎不可能碰撞。
 */
TaskId generate_id();

/**
 * @brief 在 Project 或 Stack 目标下创建任务，落盘 .task 文件并更新项目文件。
 * @param target_id 目标 ID（Project 或 Stack，由 Stack 索引库判定类型）
 * @param title 任务标题（不可为空）
 * @param description 任务描述（可为空串）
 * @param options 数据库路径（默认 data/Project.json + data/Stack.json）
 * @return 成功返回 TaskResult；失败返回 TaskError
 * @pre options.stack_db 存在且含 nodes 数组；target_id 在其中出现
 * @post tasks/<task_id>.task 已写入；项目文件 backlog / stack tasks 头部
 *       插入新 ID；项目文件 updated 字段已刷新
 * @note Stack 目标在项目文件中缺失时会退回项目 backlog 兜底，任务不丢失。
 */
std::expected<TaskResult, TaskError> create_task(ProjectId target_id, TaskTitle title,
                                                 TaskDescription description,
                                                 const CreateTaskOptions& options = {});

/**
 * @brief 遍历项目文件 backlog 中的任务 ID（函数对象设计）。
 * @param project_file .project 文件路径
 * @param visitor 满足 std::invocable<std::string_view> 的可调用对象
 * @return 成功返回访问到的 ID 数量；文件不可读返回 project_file_unreadable
 * @note backlog 缺失或非数组视为空列表（返回 0），不隐式创建节点。
 */
template <std::invocable<std::string_view> Visitor>
std::expected<std::size_t, TaskError> for_each_backlog_id(const std::filesystem::path& project_file,
                                                          Visitor&& visitor);

/// TaskError 到可读消息（供 CLI / 日志 / 测试使用）。
std::string_view task_error_message(TaskError error) noexcept;

}  // namespace TaskManager

// 模块链接域（非导出）：load_json 先于匿名命名空间的 helper 声明，
// 且导出模板 for_each_backlog_id 会在其他 TU 实例化，其引用的实体必须是
// module linkage（可被导入方合法可达），不能放在匿名命名空间（TU-local）。
namespace TaskManager {

/**
 *  读取并解析 JSON 文件（读取失败与解析异常统一映射为 on_failure）。
 *  nlohmann 对空文件/坏内容抛 parse_error，必须接住，
 *       API 边界不向调用方抛异常。
 */
std::expected<nlohmann::json, TaskManager::TaskError> load_json(const std::filesystem::path& file,
                                                                TaskManager::TaskError on_failure) {
  std::ifstream input_file(file);
  nlohmann::json content;
  try {
    if (!(input_file >> content)) {
      return std::unexpected(on_failure);
    }
  } catch (const std::exception&) {
    return std::unexpected(on_failure);
  }
  return content;
}

}  // namespace TaskManager

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

/**
 * @brief Stack 索引库快速索引条目：节点 ID → (所属项目 ID, 是否为项目节点)。
 */
struct TargetLink {
  std::string project_id;   ///< 所属项目 ID
  bool is_project = false;  ///< 节点自身是否为项目节点
};

/// 进程内单调递增序号（原子，无数据竞争），用于 ID 随机种子的去重混合。
std::atomic<std::uint64_t> g_sequence{0};

/// 当前本地时间（秒级）ISO 格式：YYYY-MM-DDTHH:MM:SS。
std::string now_iso() {
  return std::format("{:%Y-%m-%dT%H:%M:%S}", std::chrono::system_clock::now());
}

/**
 * @brief 读取 Stack 索引库并构建快速索引（错误用 expected 返回）。
 * @return 成功返回 map；失败返回 TaskError
 */
std::expected<std::unordered_map<std::string, TargetLink>, TaskManager::TaskError> load_quick_index(
    const fs::path& stack_db) {
  if (!fs::exists(stack_db)) {
    return std::unexpected(TaskManager::TaskError::stack_db_missing);
  }
  const auto data = load_json(stack_db, TaskManager::TaskError::stack_db_unreadable);
  if (!data) {
    return std::unexpected(data.error());
  }
  if (!data->contains("nodes") || !(*data)["nodes"].is_array()) {
    return std::unexpected(TaskManager::TaskError::stack_db_no_nodes);
  }
  std::unordered_map<std::string, TargetLink> index;
  for (const auto& item : (*data)["nodes"]) {
    const std::string id = item.value("id", "");
    if (id.empty()) {
      continue;  // 空 ID 节点直接跳过
    }
    const std::string project_id = item.value("projectid", id);
    // operator[] 只用于"写入新元素"（符合其语义），查询一律走 find/at
    index[id] = {project_id, id == project_id};
  }
  return index;
}

}  // namespace

// 模块链接域（非导出）：导出模板 for_each_backlog_id 会在其他 TU 实例化，
// 其引用的实体必须是 module linkage（可被导入方合法可达），
// 不能放在匿名命名空间（TU-local）。
namespace TaskManager {

TaskId generate_id() {
  // 字母表 64 字符：每字符编码 6 bit。
  static constexpr std::string_view kAlphabet =
      "-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz";
  static_assert(kAlphabet.size() == 64);
  assert(kTaskIdLength >= 8 && "time part needs at least 8 chars");

  const auto now_ns =
      static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  std::string id(kTaskIdLength, ' ');

  // 前 8 字符取时间戳低位：无符号右移 / 按位与，不触发有符号溢出 UB。
  auto time_bits = now_ns;
  for (std::size_t i = 0; i < 8; ++i) {
    id[i] = kAlphabet[static_cast<std::size_t>(time_bits & 63U)];
    time_bits >>= 6;
  }

  // 后 12 字符随机：seed_seq 混合时间戳高/低位、原子序号与 random_device。
  const std::uint64_t sequence = g_sequence.fetch_add(1, std::memory_order_relaxed);
  const auto mix = [](std::uint64_t value) {
    return static_cast<std::uint32_t>(value ^ (value >> 32));
  };
  std::random_device random_device;
  std::seed_seq seed{mix(now_ns), mix(sequence), random_device()};
  std::mt19937_64 generator(seed);
  std::uniform_int_distribution<int> distribution(0, 63);
  for (std::size_t i = 8; i < id.size(); ++i) {
    id[i] = kAlphabet[static_cast<std::size_t>(distribution(generator))];
  }
  return TaskId(std::move(id));
}

std::expected<TaskResult, TaskError> create_task(ProjectId target_id, TaskTitle title,
                                                 TaskDescription description,
                                                 const CreateTaskOptions& options) {
  const std::string input = std::format(
      "target_id={} title={} description={} project_db={} stack_db={}", target_id.value,
      title.value, description.value, options.project_db.string(), options.stack_db.string());

  // 提前返回风格：整段业务包进立即调用 lambda，每个可恢复错误立即 return，
  // 避免旧版多层 if-else 嵌套导致可读性下降。
  const auto run = [&]() -> std::expected<TaskResult, TaskError> {
    if (target_id.value.empty()) {
      return std::unexpected(TaskError::empty_target_id);
    }
    if (title.value.empty()) {
      return std::unexpected(TaskError::empty_title);
    }

    const auto index = load_quick_index(options.stack_db);
    if (!index) {
      return std::unexpected(index.error());
    }
    const auto link_it = index->find(target_id.value);
    if (link_it == index->end()) {
      return std::unexpected(TaskError::target_not_found);
    }
    const TargetLink link = link_it->second;  // 拷贝出值，避免迭代器失效

    // 从项目索引库解析 .project 文件路径（contains/is_array 防御访问）。
    const auto project_db_json = load_json(options.project_db, TaskError::project_db_unreadable);
    if (!project_db_json || !project_db_json->contains("nodes") ||
        !(*project_db_json)["nodes"].is_array()) {
      return std::unexpected(TaskError::project_db_unreadable);
    }
    std::string project_file_path;
    for (const auto& node : (*project_db_json)["nodes"]) {
      if (node.value("id", "") == link.project_id) {
        project_file_path = node.value("path", "");
        break;
      }
    }
    if (project_file_path.empty() || !fs::exists(project_file_path)) {
      return std::unexpected(TaskError::project_file_not_found);
    }

    auto project_json_parsed = load_json(project_file_path, TaskError::project_file_unreadable);
    if (!project_json_parsed) {
      return std::unexpected(project_json_parsed.error());
    }
    json project_json = *project_json_parsed;

    const TaskId task_id = generate_id();
    const std::string now = now_iso();

    // 插入位置：项目目标 → backlog 头部；Stack 目标 → 对应 stack.tasks 头部。
    bool inserted = false;
    std::string target_kind = "project";
    if (link.is_project) {
      if (!project_json.contains("backlog") || !project_json["backlog"].is_array()) {
        project_json["backlog"] = json::array();
      }
      project_json["backlog"].insert(project_json["backlog"].begin(), task_id.value);
      inserted = true;
    } else if (project_json.contains("stacks") && project_json["stacks"].is_array()) {
      for (auto& stack : project_json["stacks"]) {
        if (stack.value("id", "") == target_id.value) {
          if (!stack.contains("tasks") || !stack["tasks"].is_array()) {
            stack["tasks"] = json::array();
          }
          stack["tasks"].insert(stack["tasks"].begin(), task_id.value);
          target_kind = "stack";
          inserted = true;
          break;
        }
      }
    }
    if (!inserted) {
      // 兜底：Stack 目标在项目文件中缺失时退回项目 backlog，保证任务不丢失。
      if (!project_json.contains("backlog") || !project_json["backlog"].is_array()) {
        project_json["backlog"] = json::array();
      }
      project_json["backlog"].insert(project_json["backlog"].begin(), task_id.value);
    }

    project_json["updated"] = now;

    // .task 元数据沿用 Stacks 应用字段结构（新增字段向后兼容）。
    const fs::path task_dir = fs::path(project_file_path).parent_path() / "tasks";
    fs::create_directories(task_dir);
    const json task_json = {
        {"title", title.value},
        {"description", description.value},
        {"id", task_id.value},
        {"project", link.project_id},
        {"created", now},
        {"updated", now},
        {"archived", ""},
        {"public", true},
        {"owner", ""},
        {"visibleUsers", json::array()},
        {"visibleRoles", json::array()},
        {"tags", json::array()},
        {"assignees", json::array()},
    };
    const fs::path task_file = task_dir / (task_id.value + ".task");
    std::ofstream task_out(task_file);
    std::ofstream project_out(project_file_path);
    if (!task_out || !project_out) {
      return std::unexpected(TaskError::write_failure);
    }
    task_out << task_json.dump(4);
    project_out << project_json.dump(4);

    TaskResult result;
    result.task_id = task_id;
    result.task_file = task_file;
    result.target_kind = target_kind;
    return result;
  }();

  const bool ok = run.has_value();
  const std::string output = std::format(
      "ok={} task_id={} task_file={} target_kind={} error={}", ok,
      ok ? run->task_id.value : std::string{}, ok ? run->task_file.string() : std::string{},
      ok ? run->target_kind : std::string{}, ok ? std::string{} : task_error_message(run.error()));
  interface_log::record(interface_log::ApiName("TaskManager::create_task"),
                        interface_log::LogText(input), interface_log::LogText(output),
                        ok ? interface_log::ApiStatus::ok : interface_log::ApiStatus::failed);
  if (!run) {
    return std::unexpected(run.error());
  }
  return *run;
}

template <std::invocable<std::string_view> Visitor>
std::expected<std::size_t, TaskError> for_each_backlog_id(const fs::path& project_file,
                                                          Visitor&& visitor) {
  const auto content = load_json(project_file, TaskError::project_file_unreadable);
  if (!content) {
    return std::unexpected(content.error());
  }
  // 防御：backlog 缺失或非数组视为空列表（返回 0），不隐式创建节点。
  if (!content->contains("backlog") || !(*content)["backlog"].is_array()) {
    return std::size_t{0};
  }
  std::size_t count = 0;
  for (const auto& entry : (*content)["backlog"]) {
    if (entry.is_string()) {
      std::invoke(visitor, entry.get_ref<const std::string&>());
      ++count;
    }
  }
  return count;
}

std::string_view task_error_message(TaskError error) noexcept {
  switch (error) {
    case TaskError::empty_target_id:
      return "target id is empty";
    case TaskError::empty_title:
      return "title is empty";
    case TaskError::stack_db_missing:
      return "stack database does not exist";
    case TaskError::stack_db_unreadable:
      return "cannot parse stack database";
    case TaskError::stack_db_no_nodes:
      return "stack database missing nodes array";
    case TaskError::target_not_found:
      return "target id does not exist";
    case TaskError::project_db_unreadable:
      return "cannot parse project database";
    case TaskError::project_file_not_found:
      return "project file not found";
    case TaskError::project_file_unreadable:
      return "cannot parse project file";
    case TaskError::write_failure:
      return "failed to write task/project file";
  }
  return "unknown task error";
}

}  // namespace TaskManager
