/**
 * @file BoostAlgorithms.cppm
 * @module BoostAlgorithms
 * @brief Boost（Conan，header_only）模块化封装：字符串与数学常量示例。
 */
module;

#include <boost/algorithm/string.hpp>
#include <boost/math/constants/constants.hpp>
#include <string>

export module BoostAlgorithms;

export namespace BoostAlgorithms {

inline std::string to_upper(std::string text) {
  boost::algorithm::to_upper(text);
  return text;
}

inline double pi() {
  return boost::math::constants::pi<double>();
}

}  // namespace BoostAlgorithms
