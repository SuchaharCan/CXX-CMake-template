/**
 * @file ScenarioLog.cxx
 * @brief 【不可删除】ScenarioLog.hxx 的实现：spdlog 双 sink 与日志裁剪。
 */

#include "ScenarioLog.hxx"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdlib>
#include <fstream>
#include <memory>
#include <vector>

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace scenario_log {
namespace {

constexpr std::string_view kSep = "----- ----- ----- ----- ----- ----- ----- -----";

}  // namespace

/// 读入全文后只保留末尾 CXX_LOG_MAX_LINES 行，防止 log 无限增长
void trim_log_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    return;
  }
  std::vector<std::string> lines;
  for (std::string line; std::getline(in, line);) {
    lines.push_back(std::move(line));
  }
  in.close();
  if (static_cast<int>(lines.size()) <= CXX_LOG_MAX_LINES) {
    return;
  }
  std::ofstream out(path, std::ios::trunc);
  const auto start = lines.size() - static_cast<std::size_t>(CXX_LOG_MAX_LINES);
  for (std::size_t i = start; i < lines.size(); ++i) {
    out << lines[i] << '\n';
  }
}

/// CXX_LLDB 非空且非 "0"，或 macOS 上进程被 tracer 附加
bool under_lldb() {
  if (const char* e = std::getenv("CXX_LLDB"); e && *e && std::string_view(e) != "0") {
    return true;
  }
#ifdef __APPLE__
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
  struct kinfo_proc info{};
  std::size_t size = sizeof(info);
  if (sysctl(mib, 4, &info, &size, nullptr, 0) == 0) {
    return (info.kp_proc.p_flag & P_TRACED) != 0;
  }
#endif
  return false;
}

/// 懒初始化：LLDB 下写 lldb_debug.log，否则 exe.log
spdlog::logger& exe_logger() {
  static std::shared_ptr<spdlog::logger> logger = [] {
    const bool lldb = under_lldb();
    const char* name = lldb ? "LLDB" : "EXE";
    const char* file = lldb ? "lldb_debug.log" : "exe.log";
    const std::string path = std::string(CXX_LOG_DIR) + "/" + file;
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console->set_pattern("%^%v%$");
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path, false);
    file_sink->set_pattern("[%Y-%m-%d %H:%M:%S] [%n] [%l] %v");
    auto lg = std::make_shared<spdlog::logger>(name, spdlog::sinks_init_list{console, file_sink});
    lg->set_level(spdlog::level::info);
    lg->flush_on(spdlog::level::info);
    return lg;
  }();
  return *logger;
}

void log_info(std::string_view msg) {
  exe_logger().info("{}", msg);
  const bool lldb = under_lldb();
  trim_log_file(std::string(CXX_LOG_DIR) + "/" + (lldb ? "lldb_debug.log" : "exe.log"));
}

void log_error(std::string_view msg) {
  exe_logger().error("{}", msg);
  const bool lldb = under_lldb();
  trim_log_file(std::string(CXX_LOG_DIR) + "/" + (lldb ? "lldb_debug.log" : "exe.log"));
}

void log_sep() {
  log_info(kSep);
}

/// GTest 专用 logger，文件名固定 gtest.log
spdlog::logger& gtest_logger() {
  static std::shared_ptr<spdlog::logger> logger = [] {
    const std::string path = std::string(CXX_LOG_DIR) + "/gtest.log";
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console->set_pattern("%^%v%$");
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path, false);
    file_sink->set_pattern("[%Y-%m-%d %H:%M:%S] [%n] [%l] %v");
    auto lg =
        std::make_shared<spdlog::logger>("GTEST", spdlog::sinks_init_list{console, file_sink});
    lg->set_level(spdlog::level::info);
    lg->flush_on(spdlog::level::info);
    return lg;
  }();
  return *logger;
}

void gtest_info(std::string_view msg) {
  gtest_logger().info("{}", msg);
  trim_log_file(std::string(CXX_LOG_DIR) + "/gtest.log");
}

}  // namespace scenario_log
