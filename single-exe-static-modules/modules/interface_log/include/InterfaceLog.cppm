/**
 * @file InterfaceLog.cppm
 * @module InterfaceLog
 * @brief 接口输入/输出日志模块（STATIC 模块库）。
 *
 * 设计依据（详见模板 README 的参考资料）：
 * - 类型即注释：接口名、输入/输出文本都使用强类型（ApiName / LogText），
 *   避免把多个 std::string 混在一张参数表里传错顺序。
 * - 错误分类：可恢复错误（后端不可用、路径非法）用 std::expected 返回，
 *   不抛异常；内存环形记录始终可用，作为 bqlog 后端的降级缓冲。
 * - 函数对象设计：for_each_recent 接受任意可调用对象（模板参数），
 *   不引入 std::function 的类型擦除开销。
 * - 无 UB：所有共享状态由 std::mutex 保护；环形队列只在锁内访问。
 *
 * 日志分工：
 * - bqlog（C++ 模块 import bq.log）：TextFileAppender 落盘，max_file_size 限制文件大小；
 * - 内存环形缓冲：最多保留 kMaxLines = 1000 条，超出淘汰最旧记录。
 */
export module InterfaceLog;

import std;
import bq.log;

export namespace interface_log {

// ---- 公共常量与状态 -------------------------------------------------------

/// 环形缓冲最大记录条数（= 1000）。
inline constexpr std::size_t kMaxLines = 1000;

/// 开发者网站水印；每次初始化 bqlog 与 CLI 调用日志都会写出。
inline constexpr std::string_view kDeveloperWebsite = "https://suchaharcan.github.io/";

/// 接口调用结果状态。
enum class ApiStatus {
  ok,      ///< 调用成功
  failed,  ///< 调用失败（错误已记录在 output 文本中）
};

/// 可恢复的日志初始化错误。
enum class LogError {
  invalid_path,         ///< 日志路径为空
  backend_unavailable,  ///< bqlog 后端初始化失败（环形缓冲仍可用）
};

/**
 * @brief 接口名强类型。
 * 语义：调用方必须显式构造 ApiName，避免把任意字符串误当接口名。
 */
struct ApiName {
  std::string value;
  explicit ApiName(std::string name) : value(std::move(name)) {}
};

/**
 * @brief 接口输入/输出文本强类型。
 * 语义：与 ApiName 一样显式构造，参数顺序在编译期被类型约束。
 */
struct LogText {
  std::string value;
  explicit LogText(std::string text) : value(std::move(text)) {}
};

/**
 * @brief 初始化 bqlog TextFileAppender（可重复调用；路径变化会重建后端）。
 * @param log_path 文本日志路径，默认 log/interface.log
 * @return 成功为 std::monostate；失败返回 LogError（环形缓冲不受影响）
 * @note 调用 create_log 前会先创建父目录；日志文件由 BqLog 附加日期序号。
 */
std::expected<std::monostate, LogError> init(
    const std::filesystem::path& log_path = "log/interface.log");

/**
 * @brief 删除超过保留期的接口日志与每日 trace 日志。
 * @param log_path 当前接口日志基准路径（通常为 log/interface.log）
 * @param months_back 保留月数，默认 3
 * @return 被删除的文件数量
 */
std::size_t cleanup_old_logs(const std::filesystem::path& log_path, int months_back = 3);

/// 启用/禁用接口记录（内存与文件同时生效）。
void set_enabled(bool enabled);

/// 当前是否启用接口记录。
bool enabled();

/**
 * @brief 记录一次接口调用。
 * @param api 接口名（强类型）
 * @param input 输入摘要
 * @param output 输出摘要（失败时为错误信息）
 * @param status 调用状态
 * @post 环形缓冲 <= kMaxLines；bqlog 文件追加一行（后端可用时）
 * @note 线程安全；耗时操作（force_flush）在锁内执行，调用方勿在高频路径使用。
 */
void record(ApiName api, LogText input, LogText output, ApiStatus status);

/**
 * @brief 取出最近 <= kMaxLines 条记录（最旧在前）。
 * @return 快照副本，调用方无需持锁
 */
std::vector<std::string> recent();

/// 当前环形缓冲条数（<= kMaxLines）。
std::size_t count();

/// 清空内存环形缓冲（不动 bqlog 文件）。
void clear();

/// 当前配置的日志路径。
std::filesystem::path log_path();

/**
 * @brief 在锁内逐条访问环形缓冲（函数对象设计）。
 * @param visitor 满足 std::invocable<std::string_view> 的可调用对象
 * @note 模板参数避免 std::function 的类型擦除开销；visitor 抛出异常会向上传播。
 */
template <std::invocable<std::string_view> Visitor>
void for_each_recent(Visitor&& visitor);

/// 状态枚举到可读名称（供日志/测试使用）。
std::string_view status_name(ApiStatus status) noexcept;

/// LogError 到可读消息（供 CLI / 测试使用）。
std::string_view log_error_message(LogError error) noexcept;

}  // namespace interface_log

// ---- 非导出内部状态 -------------------------------------------------------
// 内部状态放在非导出的模块作用域（module linkage，非 TU-local），
// 保证导出模板 for_each_recent 在其他 TU 实例化时合法可达。
// 所有可变状态都由 g_mutex 串行化；bqlog 只是可选落盘后端，内存环形缓冲
// 在 bqlog 未初始化或运行期异常时仍能继续工作。
namespace interface_log {

std::mutex g_mutex;                ///< 保护下列全部可变状态。
std::deque<std::string> g_lines;   ///< 最近调用记录；队首最旧。
std::optional<bq::log> g_logger;   ///< 可选 bqlog 文件后端。
std::filesystem::path g_log_path;  ///< 当前后端日志基准路径。
bool g_initialized = false;        ///< bqlog 是否已成功初始化。
bool g_recording_enabled = true;   ///< Alfred 配置控制的全局日志开关。

/**
 * @brief 当前时间字符串（本地时间，秒级精度）。
 * @note 使用 C++20 chrono + format，避免依赖平台 localtime。
 */
std::string now_string() {
  return std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::system_clock::now());
}

}  // namespace interface_log

namespace interface_log {

std::expected<std::monostate, LogError> init(const std::filesystem::path& log_path_value) {
  // 空路径属于可恢复配置错误；调用方仍可依赖内存日志继续运行。
  if (log_path_value.empty()) {
    return std::unexpected(LogError::invalid_path);
  }

  std::lock_guard lock(g_mutex);
  g_recording_enabled = true;
  // 同一路径重复初始化保持幂等，避免每个测试/Fixture 重建文件后端。
  if (g_initialized && g_log_path == log_path_value) {
    return {};
  }

  try {
    // 先创建父目录，再交给 bqlog 创建带日期序号的文本文件。
    if (log_path_value.has_parent_path()) {
      std::filesystem::create_directories(log_path_value.parent_path());
    }

    // 单文件上限 1 MiB；超过后由 bqlog 滚动，项目启动时再做三个月清理。
    const std::string config = std::format(
        "appenders_config.appender_interface.type=text_file\n"
        "appenders_config.appender_interface.file_name={}\n"
        "appenders_config.appender_interface.levels=[all]\n"
        "appenders_config.appender_interface.max_file_size=1048576\n",
        log_path_value.string());

    // 日志名附加路径哈希，避免并行测试进程共享同名日志对象。
    const std::string log_name =
        std::format("stacks_tools_interface_{}", std::hash<std::string>{}(log_path_value.string()));
    g_logger.emplace(bq::log::create_log(log_name, config));
    g_log_path = log_path_value;
    g_initialized = true;
    // 水印属于后端初始化记录，便于从用户日志直接识别插件来源。
    g_logger->info("Omni-Stacks developer={} interface log initialized path:{}", kDeveloperWebsite,
                   log_path_value.string());
    // 立即 flush，保证 CLI 崩溃或 Alfred 终止进程时仍能看到最近一次调用。
    g_logger->force_flush();
    return {};
  } catch (const std::exception&) {
    // bqlog 失败不销毁进程内环形缓冲；只记录后端不可用状态。
    g_initialized = false;
    g_log_path = log_path_value;
    return std::unexpected(LogError::backend_unavailable);
  }
}

std::size_t cleanup_old_logs(const std::filesystem::path& log_path_value, int months_back) {
  // 负保留期和空路径视为禁用清理，不做任何删除。
  if (log_path_value.empty() || months_back < 0) {
    return 0;
  }
  const std::filesystem::path directory =
      log_path_value.has_parent_path() ? log_path_value.parent_path() : std::filesystem::path(".");
  std::error_code error;
  if (!std::filesystem::is_directory(directory, error)) {
    return 0;
  }

  // 以“当天”而非精确时刻作为截止点；日志按自然日命名，便于稳定比较。
  const std::chrono::sys_days cutoff = std::chrono::floor<std::chrono::days>(
      std::chrono::system_clock::now() - std::chrono::months{months_back});
  const std::chrono::year_month_day cutoff_date{cutoff};

  // 将 YYYY-MM-DD 压缩成 YYYYMMDD 无符号整数，与文件名中的日期段同构。
  const unsigned cutoff_number =
      static_cast<unsigned>(static_cast<int>(cutoff_date.year())) * 10000U +
      static_cast<unsigned>(cutoff_date.month()) * 100U + static_cast<unsigned>(cutoff_date.day());

  // 只匹配本插件管理的命名空间，绝不按目录批量删除其他 Alfred 日志。
  const std::regex dated_log(R"((?:interface\.log_|trace-)([0-9]{8}))");

  std::size_t removed = 0;
  for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
    // 遍历错误时跳过当前项并继续，清理属于尽力而为的维护操作。
    if (error || !entry.is_regular_file(error)) {
      continue;
    }
    const std::string filename = entry.path().filename().string();
    std::smatch match;
    bool expired = false;
    if (std::regex_search(filename, match, dated_log) && match.size() > 1) {
      const auto stamp = static_cast<unsigned>(std::stoul(match[1].str()));
      expired = stamp < cutoff_number;
    } else if (filename == "trace.log" || filename == "trace.log.binary-stderr") {
      // 无日期后缀的旧格式 trace 按文件 mtime 判断；统一转换到 system_clock。
      const auto write_time = entry.last_write_time(error);
      if (!error) {
        expired = std::filesystem::file_time_type::clock::to_sys(write_time) < cutoff;
      }
    }
    if (expired) {
      std::filesystem::remove(entry.path(), error);
      if (!error) {
        ++removed;
      }
    }
    error.clear();
  }
  return removed;
}

void record(ApiName api, LogText input, LogText output, ApiStatus status) {
  // 先把一次调用压成单行文本，使内存缓冲与 bqlog 文件拥有完全一致的记录格式。
  const std::string line = std::format("[{}] api={} input={} output={} status={}", now_string(),
                                       api.value, input.value, output.value, status_name(status));

  std::lock_guard lock(g_mutex);
  // 关闭日志时尽早返回，不消耗环形缓冲，也不触发文件 flush。
  if (!g_recording_enabled) {
    return;
  }
  g_lines.push_back(line);
  // 环形缓冲只淘汰队首，保持“最近 kMaxLines 条”的时间顺序。
  while (g_lines.size() > kMaxLines) {
    g_lines.pop_front();
  }
  if (g_initialized) {
    try {
      g_logger->info("{}", line);
      g_logger->force_flush();
    } catch (const std::exception&) {
      // 环形缓冲仍保留记录；bqlog 异常不破坏业务接口。
    }
  }
}

void set_enabled(bool value) {
  // 启停是进程级状态，切换时与 record 使用同一把锁，避免部分线程继续写文件。
  std::lock_guard lock(g_mutex);
  g_recording_enabled = value;
}

bool enabled() {
  std::lock_guard lock(g_mutex);
  return g_recording_enabled;
}

std::vector<std::string> recent() {
  std::lock_guard lock(g_mutex);
  return {g_lines.begin(), g_lines.end()};
}

std::size_t count() {
  std::lock_guard lock(g_mutex);
  return g_lines.size();
}

void clear() {
  std::lock_guard lock(g_mutex);
  g_lines.clear();
}

std::filesystem::path log_path() {
  std::lock_guard lock(g_mutex);
  return g_log_path;
}

template <std::invocable<std::string_view> Visitor>
void for_each_recent(Visitor&& visitor) {
  std::lock_guard lock(g_mutex);
  for (const auto& line : g_lines) {
    std::invoke(visitor, std::string_view(line));
  }
}

std::string_view status_name(ApiStatus status) noexcept {
  // 显式穷举状态；后续新增枚举值时编译器可配合警告提示遗漏分支。
  switch (status) {
    case ApiStatus::ok:
      return "ok";
    case ApiStatus::failed:
      return "fail";
  }
  return "unknown";
}

std::string_view log_error_message(LogError error) noexcept {
  // 业务层只消费稳定字符串，不直接暴露 bqlog 的平台错误文本。
  switch (error) {
    case LogError::invalid_path:
      return "log path is empty";
    case LogError::backend_unavailable:
      return "bqlog backend unavailable";
  }
  return "unknown log error";
}

}  // namespace interface_log
