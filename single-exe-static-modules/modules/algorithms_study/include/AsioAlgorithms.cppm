/**
 * @file AsioAlgorithms.cppm
 * @module AsioAlgorithms
 * @brief Asio（Conan，standalone）模块化封装：定时器示例。
 */
module;

#include <asio.hpp>
#include <chrono>
#include <cstddef>

export module AsioAlgorithms;

export namespace AsioAlgorithms {

/// 等待一个异步定时器到期后返回 io_context 处理的 handler 数量。
inline std::size_t wait_for(std::chrono::milliseconds duration) {
  asio::io_context io;
  asio::steady_timer timer(io, duration);
  timer.wait();
  return io.run();
}

}  // namespace AsioAlgorithms
