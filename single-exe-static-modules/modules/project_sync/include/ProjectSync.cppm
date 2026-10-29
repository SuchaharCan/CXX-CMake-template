/**
 * @file ProjectSync.cppm
 * @module ProjectSync
 * @brief 扫描 Stacks 项目文件并生成项目/Stack 索引数据库（STATIC 模块库）。
 *
 * 设计依据：
 * - 类型即注释：数据库路径用 DbPaths 结构体一次传参，避免两个 string 顺序错乱；
 * - 错误分类：可恢复错误（路径缺失、JSON 不可读）用 std::expected<SyncSummary,
 *   SyncError> 返回，提前 return，不抛异常；
 * - 函数对象设计：for_each_project_file 接受任意可调用对象，扫描逻辑与业务解耦；
 * - 无 UB：迭代前先判断目录存在，JSON 访问全部走 contains()/value() 防御；
 * - C++26 可移植时间：文件 mtime 先把 file_clock 平移到 system_clock（时长差换算），
 *   本地时区拆分在无 tzdb 的 Homebrew libc++ 上回退 localtime_r + strftime，
 *   写入节点 "mtime"。
 *
 * 模块独立性：本模块只依赖 std 与 InterfaceLog，不依赖 TaskManager。
 */
module;

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <system_error>

#include "json.hpp"

export module ProjectSync;

import std;
import InterfaceLog;

export namespace ProjectSync {

/**
 * @brief 数据库路径结构体（类型即注释：一次传参，避免两个 string 顺序颠倒）。
 */
struct DbPaths {
  std::filesystem::path project_db{"data/Project.json"};  ///< 项目索引库
  std::filesystem::path stack_db{"data/Stack.json"};      ///< Stack 索引库
};

/// 同步可恢复错误码。
enum class SyncError {
  search_path_missing,      ///< 搜索路径不存在
  project_db_unreadable,    ///< 项目索引库不可解析
  project_file_unreadable,  ///< 某个 .project 文件不可解析
  io_failure,               ///< 写库失败等 IO 错误
};

/// 同步结果摘要（返回结构体而非一堆出参）。
struct SyncSummary {
  std::size_t project_count = 0;  ///< 项目节点数
  std::size_t stack_count = 0;    ///< Stack 节点数（含项目自身首条）
};

/**
 * @brief 同步全部已记忆路径 + 新搜索路径。
 * @param search_path 新增库路径；为空表示只扫描已记忆路径
 * @param paths 数据库路径
 * @return 成功返回 SyncSummary；失败返回 SyncError
 * @note 单个坏 .project 文件会被记录为 project_file_unreadable 并继续扫描，
 *       最终返回第一个可恢复错误。
 */
std::expected<SyncSummary, SyncError> sync_all(const std::filesystem::path& search_path,
                                               const DbPaths& paths = {});

/**
 * @brief 遍历目录下所有 .project 文件（函数对象设计）。
 * @param directory 库目录（不存在时直接返回）
 * @param visitor 满足 std::invocable<const std::filesystem::directory_entry&> 的可调用对象
 * @note 模板参数避免 std::function 开销；目录迭代异常由调用方处理。
 */
template <std::invocable<const std::filesystem::directory_entry&> Visitor>
void for_each_project_file(const std::filesystem::path& directory, Visitor&& visitor);

/// SyncError 到可读消息（供 CLI / 测试使用）。
std::string_view sync_error_message(SyncError error) noexcept;

/**
 * @brief 可移植地把文件时间点格式化为 "YYYY-MM-DD HH:MM:SS"（本地时区）。
 * @param file_time filesystem::last_write_time 的返回类型
 * @return 格式化字符串；时钟转换失败时返回空串
 * @note 参考 C++26 时间处理：先转到 system_clock，再用 C++20 日历类型拆分。
 */
std::string format_file_time(std::filesystem::file_time_type file_time);

}  // namespace ProjectSync

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

/// 当前本地时间（秒级）——数据库 updateTime 字段。
std::string now_timestamp() {
  return std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::system_clock::now());
}

/**
 * @brief 读取项目索引库的 library_paths 为有序集合。
 * @return 为空表示无历史路径或文件不可读（由调用方决定是否报错）
 */
std::expected<std::set<std::string>, ProjectSync::SyncError> load_library_paths(
    const fs::path& project_db) {
  std::set<std::string> result;
  if (!fs::exists(project_db)) {
    return result;  // 首次同步没有历史库
  }
  std::ifstream input_file(project_db);
  json db;
  try {
    if (!(input_file >> db)) {
      return std::unexpected(ProjectSync::SyncError::project_db_unreadable);
    }
  } catch (const std::exception&) {
    // nlohmann 对空文件/坏内容会抛 parse_error，与读取失败同等处理
    return std::unexpected(ProjectSync::SyncError::project_db_unreadable);
  }
  if (db.contains("library_paths") && db["library_paths"].is_array()) {
    for (const auto& entry : db["library_paths"]) {
      if (entry.is_string()) {
        result.insert(entry.get<std::string>());
      }
    }
  }
  return result;
}

}  // namespace

namespace ProjectSync {

std::string format_file_time(fs::file_time_type file_time) {
  // 参考 C++26 可移植时间处理：filesystem 时钟 → system_clock 再取日历字段。
  const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      file_time - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
  // Homebrew libc++ 以 _LIBCPP_HAS_TIME_ZONE_DATABASE=0 构建（无 tzdb / zoned_time），
  // 本地时间拆分回退 POSIX localtime_r（可重入）+ strftime。tm 先零初始化，
  // 避免读取未初始化字段（C++26 起读未初始化变量属 erroneous behaviour）。
  const std::time_t seconds = std::chrono::system_clock::to_time_t(system_time);
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
  // 输入摘要（类型即注释：把业务信息先拼成可读文本再记日志）
  const std::string input =
      std::format("search_path={} project_db={} stack_db={}", search_path.string(),
                  paths.project_db.string(), paths.stack_db.string());

  SyncSummary summary;
  std::optional<SyncError> first_error;

  // 可恢复错误一律提前 return；这里把整段 IO 包在 lambda 里逐项判断。
  const auto run = [&]() -> std::expected<std::monostate, SyncError> {
    if (paths.project_db.has_parent_path()) {
      fs::create_directories(paths.project_db.parent_path());
    }
    if (paths.stack_db.has_parent_path()) {
      fs::create_directories(paths.stack_db.parent_path());
    }

    auto library_paths = load_library_paths(paths.project_db);
    if (!library_paths) {
      return std::unexpected(library_paths.error());
    }

    if (!search_path.empty()) {
      if (!fs::exists(search_path)) {
        return std::unexpected(SyncError::search_path_missing);
      }
      library_paths->insert(fs::absolute(search_path).string());
    }

    json project_nodes = json::array();
    json stack_nodes = json::array();

    for (const auto& path_string : *library_paths) {
      const fs::path library_path(path_string);
      if (!fs::exists(library_path) || !fs::is_directory(library_path)) {
        continue;
      }
      for_each_project_file(library_path, [&](const fs::directory_entry& entry) {
        try {
          std::ifstream project_file(entry.path());
          json content;
          if (!(project_file >> content)) {
            if (!first_error) {
              first_error = SyncError::project_file_unreadable;
            }
            return;
          }
          const std::string project_id = content.value("id", "");
          const std::string project_title = content.value("title", std::string("Untitled"));

          project_nodes.push_back({
              {"path", entry.path().string()},
              {"id", project_id},
              {"title", project_title},
              {"mtime", format_file_time(entry.last_write_time())},
          });
          stack_nodes.push_back({
              {"id", project_id},
              {"projectid", project_id},
              {"title", project_title},
          });

          if (content.contains("stacks") && content["stacks"].is_array()) {
            for (const auto& stack : content["stacks"]) {
              const std::string stack_id = stack.value("id", "");
              if (stack_id.empty()) {
                continue;  // 空 ID 直接跳过，避免写出无效节点
              }
              stack_nodes.push_back({
                  {"id", stack_id},
                  {"projectid", project_id},
                  {"title", stack.value("title", std::string("Untitled"))},
              });
            }
          }
        } catch (const std::exception&) {
          if (!first_error) {
            first_error = SyncError::project_file_unreadable;
          }
        }
      });
    }

    const json project_db_json = {
        {"library_paths", json(*library_paths)},
        {"nodes", project_nodes},
        {"updateTime", now_timestamp()},
    };
    const json stack_db_json = {
        {"nodes", stack_nodes},
        {"updateTime", now_timestamp()},
    };

    std::ofstream project_out(paths.project_db);
    std::ofstream stack_out(paths.stack_db);
    if (!project_out || !stack_out) {
      return std::unexpected(SyncError::io_failure);
    }
    project_out << project_db_json.dump(4);
    stack_out << stack_db_json.dump(4);

    summary.project_count = project_nodes.size();
    summary.stack_count = stack_nodes.size();
    // 单个坏 .project 文件被记录后继续扫描，最终返回第一个可恢复错误
    if (first_error) {
      return std::unexpected(*first_error);
    }
    return {};
  }();

  const bool ok = run.has_value();
  const std::string output =
      std::format("ok={} projects={} stacks={} error={}", ok, summary.project_count,
                  summary.stack_count, run ? std::string{} : sync_error_message(run.error()));
  interface_log::record(interface_log::ApiName("ProjectSync::sync_all"),
                        interface_log::LogText(input), interface_log::LogText(output),
                        ok ? interface_log::ApiStatus::ok : interface_log::ApiStatus::failed);
  if (!run) {
    return std::unexpected(run.error());
  }
  return summary;
}

template <std::invocable<const fs::directory_entry&> Visitor>
void for_each_project_file(const fs::path& directory, Visitor&& visitor) {
  if (!fs::exists(directory) || !fs::is_directory(directory)) {
    return;
  }
  for (const auto& entry : fs::directory_iterator(directory)) {
    if (entry.path().extension() == ".project") {
      std::invoke(visitor, entry);
    }
  }
}

std::string_view sync_error_message(SyncError error) noexcept {
  switch (error) {
    case SyncError::search_path_missing:
      return "search path does not exist";
    case SyncError::project_db_unreadable:
      return "cannot parse project database";
    case SyncError::project_file_unreadable:
      return "cannot parse project file";
    case SyncError::io_failure:
      return "failed to write database";
  }
  return "unknown sync error";
}

}  // namespace ProjectSync
