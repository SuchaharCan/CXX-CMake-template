/**
 * @file AlgorithmsStudy.cppm
 * @module AlgorithmsStudy
 * @brief 独立学习模块：聚合 Conan 依赖库的模块化封装（仅 Debug 编译）。
 *
 * 每个 wrapper 是否可用的宏（CXX_HAS_*）由 CMake 按已探测到的 Conan 库注入；
 * 缺失的重型库不会影响其余模块与本模块的编译。
 */
export module AlgorithmsStudy;

import std;

#ifdef CXX_HAS_JSON_ALGORITHMS
export import JsonAlgorithms;
#endif
#ifdef CXX_HAS_EIGEN_ALGORITHMS
export import EigenAlgorithms;
#endif
#ifdef CXX_HAS_ASIO_ALGORITHMS
export import AsioAlgorithms;
#endif
#ifdef CXX_HAS_SQLITE_ALGORITHMS
export import SqliteAlgorithms;
#endif
#ifdef CXX_HAS_ABSEIL_ALGORITHMS
export import AbseilAlgorithms;
#endif
#ifdef CXX_HAS_BOOST_ALGORITHMS
export import BoostAlgorithms;
#endif
#ifdef CXX_HAS_OPENCV_ALGORITHMS
export import OpenCvAlgorithms;
#endif

export namespace AlgorithmsStudy {

/// 学习模块版本标识。
inline constexpr int kVersion = 1;

/// 聚合自检：逐项调用当前可用的 Conan 依赖封装，全部成功返回 true。
inline bool selfcheck() {
  bool ok = true;
#ifdef CXX_HAS_JSON_ALGORITHMS
  const auto json = JsonAlgorithms::parse(R"({"ok": true})");
  ok = ok && JsonAlgorithms::dump(json).find("true") != std::string::npos;
#endif
#ifdef CXX_HAS_EIGEN_ALGORITHMS
  ok = ok && EigenAlgorithms::trace(EigenAlgorithms::MatrixXd::Identity(2, 2)) == 2.0;
#endif
#ifdef CXX_HAS_ASIO_ALGORITHMS
  ok = ok && AsioAlgorithms::wait_for(std::chrono::milliseconds{1}) == 0;
#endif
#ifdef CXX_HAS_SQLITE_ALGORITHMS
  ok = ok && SqliteAlgorithms::insert_and_count("note") == 1;
#endif
#ifdef CXX_HAS_ABSEIL_ALGORITHMS
  ok = ok && AbseilAlgorithms::join_words("hello", "algorithms") == "hello algorithms";
#endif
#ifdef CXX_HAS_BOOST_ALGORITHMS
  ok = ok && BoostAlgorithms::to_upper("stacks") == "STACKS";
#endif
#ifdef CXX_HAS_OPENCV_ALGORITHMS
  ok = ok && OpenCvAlgorithms::zero_matrix_elements(3, 3) == 9;
#endif
  return ok;
}

}  // namespace AlgorithmsStudy
