/**
 * @file JsonAlgorithms.cppm
 * @module JsonAlgorithms
 * @brief nlohmann-json（Conan）模块化封装：JSON 解析/序列化/合并示例。
 */
module;

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

export module JsonAlgorithms;

export namespace JsonAlgorithms {

using json = nlohmann::json;

inline json parse(std::string_view text) {
  return json::parse(text);
}

inline std::string dump(const json& j) {
  return j.dump();
}

inline json merge(json lhs, const json& rhs) {
  lhs.update(rhs);
  return lhs;
}

}  // namespace JsonAlgorithms
