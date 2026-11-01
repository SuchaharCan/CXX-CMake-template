/**
 * @file ProjectSync.cppm
 * @module ProjectSync
 * @brief Stacks 2 项目/看板索引模块。
 *
 * 本模块只负责读取已保存的工作区路径、扫描 `.project` 文件并原子重建
 * `data/Project.json` 与 `data/Stack.json`。业务任务的增删改查在
 * TaskManager 中完成。
 *
 * 设计约束：
 * - 路径先规范化去重，避免同一工作区以不同相对路径重复扫描；
 * - `.project` 按绝对路径排序，保证索引顺序与文件系统枚举顺序无关；
 * - 单个坏项目只记录到 failed_projects，不阻断其他项目的同步；
 * - 两个索引文件都通过同目录临时文件 + rename 原子替换；
 * - 模块只依赖 std 与 InterfaceLog，不反向 import TaskManager。
 */
module;

#include <unistd.h>

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

/// 工作区同步的可恢复错误分类。
enum class SyncError {
  search_path_missing,        ///< 新路径不存在。
  search_path_not_directory,  ///< 新路径是文件而不是目录。
  project_db_unreadable,      ///< 现有 Project.json 无法解析。
  project_file_unreadable,    ///< 一个或多个 .project 文件损坏。
  io_failure,                 ///< 目录遍历或索引写入失败。
};

/// 同步结果摘要；用于 CLI、Alfred 反馈和接口日志。
struct SyncSummary {
  std::size_t project_count = 0;                       ///< 有效项目数。
  std::size_t stack_count = 0;                         ///< 项目 + 清单节点数。
  std::vector<std::filesystem::path> library_paths;    ///< 实际扫描的工作区。
  std::vector<std::filesystem::path> failed_projects;  ///< 无法解析的项目文件。
};

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
      library_paths->insert(normalized_path(search_path).string());
    }

    // 阶段 2：遍历工作区，生成 Project 索引与扁平 Project/Stack 索引。
    json project_nodes = json::array();
    json stack_nodes = json::array();
    std::set<std::string> project_ids;

    for (const auto& library_path_string : *library_paths) {
      const fs::path library_path(library_path_string);
      if (!fs::exists(library_path) || !fs::is_directory(library_path)) {
        continue;
      }
      summary.library_paths.push_back(library_path);

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
  const std::string output =
      std::format("ok={} projects={} stacks={} failed_projects={} error={}", ok,
                  summary.project_count, summary.stack_count, summary.failed_projects.size(),
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
    case SyncError::project_db_unreadable:
      return "cannot parse project database";
    case SyncError::project_file_unreadable:
      return "one or more project files cannot be parsed";
    case SyncError::io_failure:
      return "failed to write database";
  }
  return "unknown sync error";
}

}  // namespace ProjectSync
