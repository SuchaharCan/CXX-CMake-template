#pragma once

/**
 * @file ScenarioLog.hxx
 * @brief 【不可删除】场景日志公共模块 — exe / gtest / lldb 共用。
 *
 * 用途：log/ 下 exe.log、lldb_debug.log、gtest.log 的 spdlog 封装与行数裁剪。
 * 修改每文件最大行数：只改下方 CXX_LOG_MAX_LINES（业务源文件勿再重复 #define）。
 * CMake 侧 cmake_configure.log / cmake_build.log 会从本宏同步，保持一致。
 */

#ifndef CXX_LOG_DIR
#define CXX_LOG_DIR "log"
#endif

// 每个 log 文件最多保留的行数（改这里；重新编译 + cmake .. 后生效）
// 提示：cmake_configure.log 镜像全部终端 STATUS，完整一次配置约 150+ 行
#ifndef CXX_LOG_MAX_LINES
#define CXX_LOG_MAX_LINES 300
#endif

#include <spdlog/spdlog.h>

#include <string>
#include <string_view>

namespace scenario_log {

/// 将日志文件裁剪为末尾 CXX_LOG_MAX_LINES 行
void trim_log_file(const std::string& path);

/// 是否在 LLDB 下调试（环境变量 CXX_LLDB 或 macOS P_TRACED）
bool under_lldb();

/// 可执行文件侧 spdlog 实例（控制台 + exe.log / lldb_debug.log）
spdlog::logger& exe_logger();

/// 写 INFO 级日志并触发裁剪
void log_info(std::string_view msg);

/// 写 ERROR 级日志并触发裁剪
void log_error(std::string_view msg);

/// 输出分隔线（经 log_info）
void log_sep();

/// GTest 侧 spdlog 实例（控制台 + gtest.log）
spdlog::logger& gtest_logger();

/// GTest 用 INFO 日志并裁剪 gtest.log
void gtest_info(std::string_view msg);

/// 终端 / gtest.log：记录函数名、输入、预期、实际
template <typename In, typename Out>
void gtest_case(std::string_view fn, const In& input, const Out& expected, const Out& actual) {
  gtest_logger().info("  {}  input={}  expected={}  actual={}", fn, input, expected, actual);
  trim_log_file(std::string(CXX_LOG_DIR) + "/gtest.log");
}

}  // namespace scenario_log
