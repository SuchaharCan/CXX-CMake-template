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

/// 环形缓冲最大记录条数（= 1000）。
inline constexpr std::size_t kMaxLines = 1000;

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

// 内部状态放在非导出的模块作用域（module linkage，非 TU-local），
// 保证导出模板 for_each_recent 在其他 TU 实例化时合法可达。
namespace interface_log {

std::mutex g_mutex;
std::deque<std::string> g_lines;
std::optional<bq::log> g_logger;
std::filesystem::path g_log_path;
bool g_initialized = false;

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
  if (log_path_value.empty()) {
    return std::unexpected(LogError::invalid_path);
  }

  std::lock_guard lock(g_mutex);
  if (g_initialized && g_log_path == log_path_value) {
    return {};
  }

  try {
    if (log_path_value.has_parent_path()) {
      std::filesystem::create_directories(log_path_value.parent_path());
    }

    const std::string config = std::format(
        "appenders_config.appender_interface.type=text_file\n"
        "appenders_config.appender_interface.file_name={}\n"
        "appenders_config.appender_interface.levels=[all]\n"
        "appenders_config.appender_interface.max_file_size=1048576\n",
        log_path_value.string());

    // 日志名附加路径哈希，避免并行测试进程共享同名日志对象
    const std::string log_name =
        std::format("stacks_tools_interface_{}", std::hash<std::string>{}(log_path_value.string()));
    g_logger.emplace(bq::log::create_log(log_name, config));
    g_log_path = log_path_value;
    g_initialized = true;
    g_logger->info("interface log initialized path:{}", log_path_value.string());
    g_logger->force_flush();
    return {};
  } catch (const std::exception&) {
    g_initialized = false;
    g_log_path = log_path_value;
    return std::unexpected(LogError::backend_unavailable);
  }
}

void record(ApiName api, LogText input, LogText output, ApiStatus status) {
  const std::string line = std::format("[{}] api={} input={} output={} status={}", now_string(),
                                       api.value, input.value, output.value, status_name(status));

  std::lock_guard lock(g_mutex);
  g_lines.push_back(line);
  while (g_lines.size() > kMaxLines) {
    g_lines.pop_front();
  }
  if (g_initialized) {
    try {
      g_logger->info("{}", line);
      g_logger->force_flush();
    } catch (const std::exception&) {
      // 环形缓冲仍保留记录；bqlog 异常不破坏业务接口
    }
  }
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
  switch (status) {
    case ApiStatus::ok:
      return "ok";
    case ApiStatus::failed:
      return "fail";
  }
  return "unknown";
}

std::string_view log_error_message(LogError error) noexcept {
  switch (error) {
    case LogError::invalid_path:
      return "log path is empty";
    case LogError::backend_unavailable:
      return "bqlog backend unavailable";
  }
  return "unknown log error";
}

}  // namespace interface_log
