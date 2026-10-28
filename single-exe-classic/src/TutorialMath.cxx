/**
 * @file TutorialMath.cxx
 * @brief TutorialMath.hxx 的实现：有限域平方根与 argc 校验。
 */

#include "TutorialMath.hxx"

#include <cmath>

namespace tutorial {

/// 域检查：非有限、负数或超过 kComputeSqrtMaxInput 时返回 -1.0
double compute_sqrt(double value) {
  if (!std::isfinite(value) || value < 0.0 || value > kComputeSqrtMaxInput) {
    return -1.0;
  }
  return std::sqrt(value);
}

/// argc >= 2 即认为用户提供了待计算的数值参数
bool has_numeric_argument(int argc) {
  return argc >= 2;
}

}  // namespace tutorial
