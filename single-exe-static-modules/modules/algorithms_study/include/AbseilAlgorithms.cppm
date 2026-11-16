/**
 * @file AbseilAlgorithms.cppm
 * @module AbseilAlgorithms
 * @brief Abseil（Conan）模块化封装：字符串拼接/切分示例。
 */
module;

#include <absl/strings/str_cat.h>
#include <absl/strings/str_split.h>

#include <string>
#include <string_view>
#include <vector>

export module AbseilAlgorithms;

export namespace AbseilAlgorithms {

inline std::string join_words(std::string_view a, std::string_view b) {
  return absl::StrCat(a, " ", b);
}

inline std::vector<std::string> split(std::string_view text) {
  std::vector<std::string> out;
  for (auto part : absl::StrSplit(text, ',')) {
    out.emplace_back(part);
  }
  return out;
}

}  // namespace AbseilAlgorithms
