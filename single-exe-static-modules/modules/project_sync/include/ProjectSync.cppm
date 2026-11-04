/**
 * @file ProjectSync.cppm
 * @module ProjectSync
 * @brief Stacks 2 项目/看板索引模块。
 *
 * 本模块负责工作区结构有效性检测、已保存路径查询、路径索引移除，以及扫描
 * `.project` 文件并原子重建 `data/Project.json` 与 `data/Stack.json`。
 * 业务任务的增删改查在 TaskManager 中完成。
 *
 * 设计约束：
 * - 路径先规范化去重，避免同一工作区以不同相对路径重复扫描；
 * - 有效工作区固定检查 companies/documents.tree/people/tasks/timeboxes 类型；
 * - `.project` 按绝对路径排序，保证索引顺序与文件系统枚举顺序无关；
 * - 单个坏项目只记录到 failed_projects，不阻断其他项目的同步；
 * - 两个索引文件都通过同目录临时文件 + rename 原子替换；
 * - 路径移除只改三个插件 JSON，绝不删除工作区目录或任务文件；
 * - 模块只依赖 std 与 InterfaceLog，不反向 import TaskManager。
 */
module;

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "json.hpp"

export module ProjectSync;

import std;
import InterfaceLog;

export namespace ProjectSync {

// ---- 公共数据契约 ---------------------------------------------------------

/**
 * @brief 项目索引数据库路径。
 * @note 两个路径一次传入，避免裸 string 参数顺序颠倒。
 */
struct DbPaths {
  std::filesystem::path project_db{"data/Project.json"};  ///< Project 索引。
  std::filesystem::path stack_db{"data/Stack.json"};      ///< Project/Stack 扁平索引。
};

/// 路径移除操作涉及的三个运行时 JSON 与跨进程锁。
struct WorkspaceDbPaths {
  std::filesystem::path project_db{"data/Project.json"};       ///< 工作区/项目索引。
  std::filesystem::path stack_db{"data/Stack.json"};           ///< 项目/Stack 扁平索引。
  std::filesystem::path timer_db{"data/Timers.json"};          ///< 运行中计时状态。
  std::filesystem::path lock_file{"data/.stacks-tools.lock"};  ///< 跨进程写锁。
};

/**
 * @brief 一个已保存工作区的有效性与项目发现结果。
 *
 * 有效 Stacks 工作区的固定结构：
 * @code{.text}
 * workspace/
 * ├── companies       # Regular File
 * ├── documents.tree  # Regular File
 * ├── people          # Regular File
 * ├── tasks/          # Directory
 * └── timeboxes/      # Directory
 * @endcode
 *
 * `.project` 文件是可选的：任意层级存在时表示该工作区已经创建过项目；没有项目
 * 仍是有效工作区，可用于创建第一个项目或 Inbox 任务。
 */
struct WorkspaceStatus {
  std::filesystem::path path;                ///< 规范化后的工作区路径。
  bool exists = false;                       ///< 根路径是否存在。
  bool is_directory = false;                 ///< 根路径是否为目录。
  bool structure_valid = false;              ///< 五个固定成员是否全部存在且类型正确。
  bool has_projects = false;                 ///< 是否递归发现任意 `.project` 文件。
  std::size_t project_file_count = 0;        ///< `.project` 文件数量。
  std::vector<std::string> missing_entries;  ///< 缺失或类型错误的固定成员。
};

/// `remove_workspace_index()` 的移除统计；不包含任何文件系统删除操作。
struct WorkspaceRemoveResult {
  std::filesystem::path path;             ///< 被移除的规范化工作区路径。
  std::size_t library_paths_removed = 0;  ///< Project.library_paths 移除数量。
  std::size_t project_nodes_removed = 0;  ///< Project.nodes 移除数量。
  std::size_t stack_nodes_removed = 0;    ///< Stack.nodes 移除数量。
  std::size_t timers_removed = 0;         ///< Timers.timers 移除数量。
};

/// 工作区同步的可恢复错误分类。
enum class SyncError {
  search_path_missing,        ///< 新路径不存在。
  search_path_not_directory,  ///< 新路径是文件而不是目录。
  workspace_invalid,          ///< 新路径缺少 Stacks 工作区固定成员。
  workspace_not_registered,   ///< 待移除路径不在插件索引中。
  project_db_unreadable,      ///< 现有 Project.json 无法解析。
  stack_db_unreadable,        ///< 现有 Stack.json 无法解析。
  timer_db_unreadable,        ///< 现有 Timers.json 无法解析。
  project_file_unreadable,    ///< 一个或多个 .project 文件损坏。
  lock_failure,               ///< 另一个写操作持锁。
  io_failure,                 ///< 目录遍历或索引写入失败。
};

/// 同步结果摘要；用于 CLI、Alfred 反馈和接口日志。
struct SyncSummary {
  std::size_t project_count = 0;                         ///< 有效项目数。
  std::size_t stack_count = 0;                           ///< 项目 + 清单节点数。
  std::vector<std::filesystem::path> library_paths;      ///< 实际扫描的工作区。
  std::vector<std::filesystem::path> invalid_libraries;  ///< 已保存但当前无效的工作区。
  std::vector<std::filesystem::path> failed_projects;    ///< 无法解析的项目文件。
};

/**
 * @brief 检查一个路径是否符合 Stacks 2 工作区固定结构。
 * @param path 待检查路径；函数只读，不创建任何内容。
 * @return 规范化路径、结构和可选项目文件状态。
 */
WorkspaceStatus inspect_workspace(const std::filesystem::path& path);

/**
 * @brief 列出 Project.json 中已保存路径及其实时有效性。
 * @param paths Project/Stack 索引路径。
 * @return 状态列表；索引损坏时返回 project_db_unreadable。
 */
std::expected<std::vector<WorkspaceStatus>, SyncError> list_workspaces(const DbPaths& paths);

/**
 * @brief 从插件三个 JSON 中移除一个工作区的索引，不删除工作区文件。
 * @param path 待移除工作区；路径失效也可以移除。
 * @param paths Project、Stack、Timers 和锁文件路径。
 * @return 各 JSON 的移除统计。
 * @note 写入顺序为 Project → Stack → Timers；任一写入失败会恢复已写文件快照。
 */
std::expected<WorkspaceRemoveResult, SyncError> remove_workspace_index(
    const std::filesystem::path& path, const WorkspaceDbPaths& paths = {});

/**
 * @brief 同步新增路径与全部已记忆路径，并原子重建两个索引。
 * @param search_path 新增工作区；空路径表示仅重载已保存路径。
 * @param paths Project/Stack 索引路径。
 * @return 成功返回统计摘要；失败返回第一个可恢复错误分类。
 * @note 单文件损坏采用“继续扫描 + 最终返回错误”的部分成功语义。
 */
std::expected<SyncSummary, SyncError> sync_all(const std::filesystem::path& search_path,
                                               const DbPaths& paths = {});

/**
 * @brief 递归遍历工作区中的 `.project` 文件。
 * @param directory 工作区根目录；不存在或不是目录时直接返回。
 * @param visitor 接收 `directory_entry` 的函数对象。
 * @note 跳过无权限目录；其他迭代错误由调用方后续的文件读取路径兜底。
 */
template <std::invocable<const std::filesystem::directory_entry&> Visitor>
void for_each_project_file(const std::filesystem::path& directory, Visitor&& visitor) {
  if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
    return;
  }

  std::error_code error;
  std::filesystem::recursive_directory_iterator iterator(
      directory, std::filesystem::directory_options::skip_permission_denied, error);
  const std::filesystem::recursive_directory_iterator end;
  while (!error && iterator != end) {
    if (iterator->is_regular_file(error) && iterator->path().extension() == ".project") {
      std::invoke(visitor, *iterator);
    }
    iterator.increment(error);
  }
}

/// SyncError 到稳定、可读的 CLI/Alfred 错误文本。
std::string_view sync_error_message(SyncError error) noexcept;

/// 把 filesystem 写入时间转换为本地 `YYYY-MM-DD HH:MM:SS`。
std::string format_file_time(std::filesystem::file_time_type file_time);

}  // namespace ProjectSync

namespace ProjectSync {

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

std::string now_timestamp() {
  // Project/Stack 索引的 updateTime 使用本地秒级时间，和 Stacks 文本界面一致。
  return std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::system_clock::now());
}

/**
 * @brief 解析为绝对规范路径；canonical 失败时退化为 lexically_normal。
 * @note 该函数不会因为不存在路径而失败，便于把待创建目录也稳定归一化。
 */
fs::path normalized_path(const fs::path& value) {
  std::error_code error;
  const fs::path canonical = fs::weakly_canonical(value, error);
  if (!error) {
    return canonical;
  }
  return fs::absolute(value, error).lexically_normal();
}

/**
 * @brief 从现有 Project.json 读取并规范化 library_paths。
 * @return 首次运行或文件不存在时返回空集合；损坏 JSON 返回 unreadable。
 */
std::expected<std::set<std::string>, SyncError> load_library_paths(const fs::path& project_db) {
  std::set<std::string> result;
  if (!fs::exists(project_db)) {
    return result;
  }

  std::ifstream input(project_db);
  try {
    json db;
    if (!(input >> db) || !db.is_object()) {
      return std::unexpected(SyncError::project_db_unreadable);
    }
    if (db.contains("library_paths") && db["library_paths"].is_array()) {
      for (const auto& item : db["library_paths"]) {
        if (item.is_string() && !item.get<std::string>().empty()) {
          result.insert(normalized_path(item.get<std::string>()).string());
        }
      }
    }
  } catch (const std::exception&) {
    return std::unexpected(SyncError::project_db_unreadable);
  }
  return result;
}

/**
 * @brief 通过同目录临时文件 + rename 原子写入 JSON。
 * @return 目录创建、写入、flush 或 rename 任一步失败均返回 false。
 * @note 临时名包含 PID 与 steady_clock 计数，避免并发/快速重试互相覆盖。
 */
bool atomic_write_json(const fs::path& target, const json& value) {
  std::error_code error;
  if (target.has_parent_path()) {
    fs::create_directories(target.parent_path(), error);
    if (error) {
      return false;
    }
  }

  const fs::path temporary =
      target.parent_path() /
      std::format(".{}.tmp.{}.{}", target.filename().string(), ::getpid(),
                  std::chrono::steady_clock::now().time_since_epoch().count());
  {
    // 关闭 ofstream 后再 rename；保证目标文件看到的是完整 JSON。
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

/**
 * @brief 防御性读取 JSON 对象中的字符串字段。
 * @param object 待读取对象。
 * @param key 字段名。
 * @param fallback 类型不匹配或字段缺失时的值。
 */
std::string safe_string(const json& object, std::string_view key, std::string fallback = {}) {
  if (object.is_object() && object.contains(key) && object[key].is_string()) {
    return object[key].get<std::string>();
  }
  return fallback;
}

/// 把固定成员缺失/类型错误列表压成紧凑日志文本。
std::string missing_entries_summary(const std::vector<std::string>& entries) {
  std::string result;
  for (const auto& entry : entries) {
    if (!result.empty()) {
      result += ',';
    }
    result += entry;
  }
  return result;
}

/// Stacks 工作区固定成员；类型决定必需的是普通文件还是目录。
struct RequiredWorkspaceEntry {
  std::string_view name;
  bool directory = false;
};

inline constexpr std::array<RequiredWorkspaceEntry, 5> kRequiredWorkspaceEntries{{
    {"companies", false},
    {"documents.tree", false},
    {"people", false},
    {"tasks", true},
    {"timeboxes", true},
}};

/// 判断 child 是否位于 parent 目录中；失效路径也可用词法比较。
bool path_is_within(const fs::path& child, const fs::path& parent) {
  const fs::path normalized_child = normalized_path(child);
  const fs::path normalized_parent = normalized_path(parent);
  const fs::path relative = normalized_child.lexically_relative(normalized_parent);
  return !relative.empty() && relative != "." && *relative.begin() != "..";
}

/**
 * @brief ProjectSync 跨 JSON 移除事务使用的非阻塞文件锁。
 * @note 与 TaskManager 使用同一 `.stacks-tools.lock`，避免路径维护与任务写入交错。
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

}  // namespace

std::string format_file_time(fs::file_time_type file_time) {
  // file_clock 的纪元没有跨平台保证，通过“当前时钟差值”平移到 system_clock。
  const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      file_time - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
  const std::time_t seconds = std::chrono::system_clock::to_time_t(system_time);

  // Homebrew libc++ 可能没有 tzdb；localtime_r + strftime 是当前平台的稳定回退。
  std::tm local{};
  if (localtime_r(&seconds, &local) == nullptr) {
    return {};
  }
  char buffer[32]{};
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local) == 0) {
    return {};
  }
  return buffer;
}

WorkspaceStatus inspect_workspace(const fs::path& path) {
  WorkspaceStatus status;
  status.path = normalized_path(path);
  const auto finish = [&] {
    interface_log::record(
        interface_log::ApiName("ProjectSync::inspect_workspace"),
        interface_log::LogText(std::format("path={}", status.path.string())),
        interface_log::LogText(std::format("exists={} directory={} valid={} projects={} missing={}",
                                           status.exists, status.is_directory,
                                           status.structure_valid, status.project_file_count,
                                           missing_entries_summary(status.missing_entries))),
        interface_log::ApiStatus::ok);
    return status;
  };

  std::error_code error;
  status.exists = fs::exists(status.path, error);
  if (error || !status.exists) {
    return finish();
  }
  status.is_directory = fs::is_directory(status.path, error);
  if (error || !status.is_directory) {
    return finish();
  }

  for (const auto& required : kRequiredWorkspaceEntries) {
    const fs::path candidate = status.path / required.name;
    const bool type_ok = required.directory ? fs::is_directory(candidate, error)
                                            : fs::is_regular_file(candidate, error);
    if (error || !type_ok) {
      status.missing_entries.emplace_back(required.name);
      error.clear();
    }
  }
  status.structure_valid = status.missing_entries.empty();

  for_each_project_file(status.path,
                        [&](const fs::directory_entry&) { ++status.project_file_count; });
  status.has_projects = status.project_file_count > 0;
  return finish();
}

std::expected<std::vector<WorkspaceStatus>, SyncError> list_workspaces(const DbPaths& paths) {
  const auto library_paths = load_library_paths(paths.project_db);
  if (!library_paths) {
    interface_log::record(
        interface_log::ApiName("ProjectSync::list_workspaces"),
        interface_log::LogText(std::format("project_db={}", paths.project_db.string())),
        interface_log::LogText(std::string(sync_error_message(library_paths.error()))),
        interface_log::ApiStatus::failed);
    return std::unexpected(library_paths.error());
  }

  std::vector<WorkspaceStatus> result;
  for (const auto& value : *library_paths) {
    result.push_back(inspect_workspace(value));
  }
  const auto valid_count = std::ranges::count_if(
      result, [](const WorkspaceStatus& item) { return item.structure_valid; });
  interface_log::record(
      interface_log::ApiName("ProjectSync::list_workspaces"),
      interface_log::LogText(std::format("project_db={}", paths.project_db.string())),
      interface_log::LogText(std::format("total={} valid={} invalid={}", result.size(), valid_count,
                                         result.size() - static_cast<std::size_t>(valid_count))),
      interface_log::ApiStatus::ok);
  return result;
}

std::expected<WorkspaceRemoveResult, SyncError> remove_workspace_index(
    const fs::path& path, const WorkspaceDbPaths& paths) {
  const fs::path target = normalized_path(path);
  const auto fail = [&](SyncError error) -> std::expected<WorkspaceRemoveResult, SyncError> {
    interface_log::record(interface_log::ApiName("ProjectSync::remove_workspace_index"),
                          interface_log::LogText(std::format("path={}", target.string())),
                          interface_log::LogText(std::string(sync_error_message(error))),
                          interface_log::ApiStatus::failed);
    return std::unexpected(error);
  };

  ScopedFileLock lock(paths.lock_file);
  if (!lock.ok()) {
    return fail(SyncError::lock_failure);
  }

  if (!fs::exists(paths.project_db)) {
    return fail(SyncError::workspace_not_registered);
  }
  const auto project_loaded = [&]() -> std::expected<json, SyncError> {
    std::ifstream input(paths.project_db, std::ios::binary);
    try {
      json value;
      if (!(input >> value) || !value.is_object()) {
        return std::unexpected(SyncError::project_db_unreadable);
      }
      return value;
    } catch (const std::exception&) {
      return std::unexpected(SyncError::project_db_unreadable);
    }
  }();
  if (!project_loaded) {
    return fail(project_loaded.error());
  }

  json project_before = *project_loaded;
  json project_after = project_before;
  WorkspaceRemoveResult result{.path = target};

  if (project_after.contains("library_paths") && project_after["library_paths"].is_array()) {
    auto& entries = project_after["library_paths"];
    const auto old_size = entries.size();
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const json& item) {
                                   return item.is_string() &&
                                          normalized_path(item.get<std::string>()) == target;
                                 }),
                  entries.end());
    result.library_paths_removed = old_size - entries.size();
  }
  if (project_after.contains("nodes") && project_after["nodes"].is_array()) {
    auto& nodes = project_after["nodes"];
    const auto old_size = nodes.size();
    nodes.erase(
        std::remove_if(nodes.begin(), nodes.end(),
                       [&](const json& node) {
                         const std::string library = safe_string(node, "library_path", "");
                         const std::string project_path = safe_string(node, "path", "");
                         return (!library.empty() && normalized_path(library) == target) ||
                                (!project_path.empty() && path_is_within(project_path, target));
                       }),
        nodes.end());
    result.project_nodes_removed = old_size - nodes.size();
  }
  const bool project_changed = result.library_paths_removed > 0 || result.project_nodes_removed > 0;
  if (project_changed) {
    project_after["updateTime"] = now_timestamp();
  }

  json stack_after = json::object();
  json stack_before = json::object();
  bool stack_changed = false;
  if (fs::exists(paths.stack_db)) {
    std::ifstream input(paths.stack_db, std::ios::binary);
    try {
      if (!(input >> stack_before) || !stack_before.is_object()) {
        return fail(SyncError::stack_db_unreadable);
      }
    } catch (const std::exception&) {
      return fail(SyncError::stack_db_unreadable);
    }
    stack_after = stack_before;
    if (stack_after.contains("nodes") && stack_after["nodes"].is_array()) {
      auto& nodes = stack_after["nodes"];
      const auto old_size = nodes.size();
      nodes.erase(
          std::remove_if(nodes.begin(), nodes.end(),
                         [&](const json& node) {
                           const std::string library = safe_string(node, "library_path", "");
                           const std::string project_path = safe_string(node, "project_path", "");
                           return (!library.empty() && normalized_path(library) == target) ||
                                  (!project_path.empty() && path_is_within(project_path, target));
                         }),
          nodes.end());
      result.stack_nodes_removed = old_size - nodes.size();
      stack_changed = result.stack_nodes_removed > 0;
      if (stack_changed) {
        stack_after["updateTime"] = now_timestamp();
      }
    }
  }

  json timers_after = json::object();
  json timers_before = json::object();
  bool timers_changed = false;
  std::set<std::string> workspace_task_ids;
  const fs::path task_directory = target / "tasks";
  std::error_code error;
  if (fs::is_directory(task_directory, error)) {
    for (const auto& entry : fs::directory_iterator(task_directory, error)) {
      if (!error && entry.is_regular_file(error) && entry.path().extension() == ".task") {
        workspace_task_ids.insert(entry.path().stem().string());
      }
    }
  }
  if (fs::exists(paths.timer_db)) {
    std::ifstream input(paths.timer_db, std::ios::binary);
    try {
      if (!(input >> timers_before) || !timers_before.is_object()) {
        return fail(SyncError::timer_db_unreadable);
      }
    } catch (const std::exception&) {
      return fail(SyncError::timer_db_unreadable);
    }
    timers_after = timers_before;
    if (timers_after.contains("timers") && timers_after["timers"].is_object()) {
      auto& timers = timers_after["timers"];
      for (auto iterator = timers.begin(); iterator != timers.end();) {
        const std::string task_file = safe_string(iterator.value(), "task_file", "");
        const bool path_match = !task_file.empty() && path_is_within(task_file, target);
        const bool id_match = workspace_task_ids.contains(iterator.key());
        if (path_match || id_match) {
          iterator = timers.erase(iterator);
          ++result.timers_removed;
        } else {
          ++iterator;
        }
      }
      timers_changed = result.timers_removed > 0;
    }
  }

  if (result.library_paths_removed == 0 && result.project_nodes_removed == 0 &&
      result.stack_nodes_removed == 0 && result.timers_removed == 0) {
    return fail(SyncError::workspace_not_registered);
  }

  struct PendingWrite {
    fs::path path;
    const json* before = nullptr;
    const json* after = nullptr;
  };
  std::vector<PendingWrite> writes;
  if (project_changed) {
    writes.push_back({paths.project_db, &project_before, &project_after});
  }
  if (stack_changed) {
    writes.push_back({paths.stack_db, &stack_before, &stack_after});
  }
  if (timers_changed) {
    writes.push_back({paths.timer_db, &timers_before, &timers_after});
  }

  std::vector<std::size_t> written;
  for (std::size_t index = 0; index < writes.size(); ++index) {
    if (!atomic_write_json(writes[index].path, *writes[index].after)) {
      for (auto iterator = written.rbegin(); iterator != written.rend(); ++iterator) {
        atomic_write_json(writes[*iterator].path, *writes[*iterator].before);
      }
      return fail(SyncError::io_failure);
    }
    written.push_back(index);
  }

  interface_log::record(
      interface_log::ApiName("ProjectSync::remove_workspace_index"),
      interface_log::LogText(std::format("path={}", target.string())),
      interface_log::LogText(std::format(
          "changes=paths:{} project_nodes:{} stack_nodes:{} timers:{} filesystem=untouched",
          result.library_paths_removed, result.project_nodes_removed, result.stack_nodes_removed,
          result.timers_removed)),
      interface_log::ApiStatus::ok);
  return result;
}

std::expected<SyncSummary, SyncError> sync_all(const fs::path& search_path, const DbPaths& paths) {
  // 接口日志先保存业务入参，输出在 operation 完成后统一记录。
  const std::string input =
      std::format("search_path={} project_db={} stack_db={}", search_path.string(),
                  paths.project_db.string(), paths.stack_db.string());
  SyncSummary summary;
  std::optional<SyncError> first_error;

  // 整段同步收进立即调用 lambda：每个错误分支直接 return，避免深嵌套。
  const auto operation = [&]() -> std::expected<std::monostate, SyncError> {
    // 阶段 1：恢复历史路径，并校验/追加本次新路径。
    auto library_paths = load_library_paths(paths.project_db);
    if (!library_paths) {
      return std::unexpected(library_paths.error());
    }
    if (!search_path.empty()) {
      if (!fs::exists(search_path)) {
        return std::unexpected(SyncError::search_path_missing);
      }
      if (!fs::is_directory(search_path)) {
        return std::unexpected(SyncError::search_path_not_directory);
      }
      const WorkspaceStatus workspace = inspect_workspace(search_path);
      if (!workspace.structure_valid) {
        summary.invalid_libraries.push_back(workspace.path);
        return std::unexpected(SyncError::workspace_invalid);
      }
      library_paths->insert(workspace.path.string());
    }

    // 阶段 2：遍历工作区，生成 Project 索引与扁平 Project/Stack 索引。
    json project_nodes = json::array();
    json stack_nodes = json::array();
    std::set<std::string> project_ids;

    for (const auto& library_path_string : *library_paths) {
      const fs::path library_path(library_path_string);
      const WorkspaceStatus workspace = inspect_workspace(library_path);
      if (!workspace.structure_valid) {
        summary.invalid_libraries.push_back(workspace.path);
        continue;
      }
      summary.library_paths.push_back(workspace.path);

      // 先收集后排序，消除 recursive_directory_iterator 的平台相关顺序。
      std::vector<fs::directory_entry> project_files;
      try {
        for_each_project_file(library_path, [&](const fs::directory_entry& entry) {
          project_files.push_back(entry);
        });
      } catch (const std::exception&) {
        if (!first_error) {
          first_error = SyncError::io_failure;
        }
        continue;
      }
      std::ranges::sort(project_files, {},
                        [](const auto& entry) { return entry.path().lexically_normal().string(); });

      for (const auto& entry : project_files) {
        try {
          // 单个项目损坏时记录路径并继续，不牺牲同一工作区的其他项目。
          std::ifstream project_file(entry.path(), std::ios::binary);
          json content;
          if (!(project_file >> content) || !content.is_object()) {
            summary.failed_projects.push_back(entry.path());
            if (!first_error) {
              first_error = SyncError::project_file_unreadable;
            }
            continue;
          }

          const std::string project_id = safe_string(content, "id");
          // 空 ID/重复 ID 不能进入索引，否则 Alfred action token 无法稳定寻址。
          if (project_id.empty() || !project_ids.insert(project_id).second) {
            summary.failed_projects.push_back(entry.path());
            if (!first_error) {
              first_error = SyncError::project_file_unreadable;
            }
            continue;
          }
          const std::string project_title = safe_string(content, "title", "Untitled");
          const std::string project_path = normalized_path(entry.path()).string();
          const std::string modified = format_file_time(entry.last_write_time());

          // 项目自身同时出现在 Project 与 Stack 索引，允许项目级直接创建任务。
          project_nodes.push_back({
              {"id", project_id},
              {"title", project_title},
              {"path", project_path},
              {"library_path", library_path.string()},
              {"mtime", modified},
          });
          stack_nodes.push_back({
              {"id", project_id},
              {"projectid", project_id},
              {"title", project_title},
              {"kind", "project"},
              {"project_path", project_path},
              {"library_path", library_path.string()},
          });

          if (!content.contains("stacks") || !content["stacks"].is_array()) {
            continue;
          }
          // 清单节点继承 projectid，WorkflowUI 可据此显示“项目 → 清单”三级路径。
          for (const auto& stack : content["stacks"]) {
            if (!stack.is_object()) {
              continue;
            }
            const std::string stack_id = safe_string(stack, "id");
            if (stack_id.empty()) {
              continue;
            }
            stack_nodes.push_back({
                {"id", stack_id},
                {"projectid", project_id},
                {"title", safe_string(stack, "title", "Untitled")},
                {"kind", "stack"},
                {"project_path", project_path},
                {"library_path", library_path.string()},
            });
          }
        } catch (const std::exception&) {
          summary.failed_projects.push_back(entry.path());
          if (!first_error) {
            first_error = SyncError::project_file_unreadable;
          }
        }
      }
    }

    summary.project_count = project_nodes.size();
    summary.stack_count = stack_nodes.size();

    // 阶段 3：先完整构造两个 JSON，再分别做原子替换。
    const json project_db = {
        {"library_paths", json(*library_paths)},
        {"nodes", std::move(project_nodes)},
        {"updateTime", now_timestamp()},
    };
    const json stack_db = {
        {"nodes", std::move(stack_nodes)},
        {"updateTime", now_timestamp()},
    };
    if (!atomic_write_json(paths.project_db, project_db) ||
        !atomic_write_json(paths.stack_db, stack_db)) {
      return std::unexpected(SyncError::io_failure);
    }
    // 部分项目损坏仍完成索引刷新，但调用方会收到明确的分类错误。
    if (first_error) {
      return std::unexpected(*first_error);
    }
    return {};
  }();

  // 所有可恢复错误都通过 expected 返回；日志记录稳定的摘要而非异常栈。
  const bool ok = operation.has_value();
  const std::string output = std::format(
      "ok={} projects={} stacks={} valid_libs={} invalid_libs={} failed_projects={} error={}", ok,
      summary.project_count, summary.stack_count, summary.library_paths.size(),
      summary.invalid_libraries.size(), summary.failed_projects.size(),
      ok ? std::string{} : sync_error_message(operation.error()));
  interface_log::record(interface_log::ApiName("ProjectSync::sync_all"),
                        interface_log::LogText(input), interface_log::LogText(output),
                        ok ? interface_log::ApiStatus::ok : interface_log::ApiStatus::failed);
  if (!operation) {
    return std::unexpected(operation.error());
  }
  return summary;
}

std::string_view sync_error_message(SyncError error) noexcept {
  // 错误文案是 Alfred/Debug trace 的对外契约，集中在此维护。
  switch (error) {
    case SyncError::search_path_missing:
      return "search path does not exist";
    case SyncError::search_path_not_directory:
      return "search path is not a directory";
    case SyncError::workspace_invalid:
      return "path is not a valid Stacks workspace";
    case SyncError::workspace_not_registered:
      return "workspace path is not registered";
    case SyncError::project_db_unreadable:
      return "cannot parse project database";
    case SyncError::stack_db_unreadable:
      return "cannot parse stack database";
    case SyncError::timer_db_unreadable:
      return "cannot parse timer database";
    case SyncError::project_file_unreadable:
      return "one or more project files cannot be parsed";
    case SyncError::lock_failure:
      return "another Stacks Tools write is in progress";
    case SyncError::io_failure:
      return "failed to write database";
  }
  return "unknown sync error";
}

}  // namespace ProjectSync
