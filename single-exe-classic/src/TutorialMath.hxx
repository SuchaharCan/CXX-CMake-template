/*
 * CodeReviewTime:2026-10-14
 * 头文件 仅声明 变量 和 函数
 */

#pragma once
/**
 * @file TutorialMath.hxx
 * @brief 教程数学工具声明：平方根与命令行参数校验（经典 .hxx/.cxx，无模块）。
 *
 * 纯函数设计便于 gtest 覆盖；实现见 TutorialMath.cxx。
 */

namespace tutorial {

/// 允许的最大输入；超出（或负数 / 非有限）时 compute_sqrt 返回 -1.0
inline constexpr double kComputeSqrtMaxInput = 1.0e12;

/**
 * @brief 计算非负且不超过 kComputeSqrtMaxInput 的平方根。
 * @param value 待开平方的输入
 * @return sqrt(value)；非法输入（负数 / 非有限 / 超上限）返回 -1.0
 */
double compute_sqrt(double value);

/**
 * @brief 判断命令行是否提供了数值参数。
 * @param argc main() 的参数个数
 * @return argc >= 2 时为 true
 */
bool has_numeric_argument(int argc);

}  // namespace tutorial
