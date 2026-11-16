/**
 * @file OpenCvAlgorithms.cppm
 * @module OpenCvAlgorithms
 * @brief OpenCV（Conan）模块化封装：基础矩阵示例。
 */
module;

#include <cstddef>
#include <opencv2/core.hpp>

export module OpenCvAlgorithms;

export namespace OpenCvAlgorithms {

/// 构造 rows x cols 的零矩阵并返回元素个数。
inline std::size_t zero_matrix_elements(int rows, int cols) {
  const cv::Mat matrix = cv::Mat::zeros(rows, cols, CV_32FC1);
  return matrix.total();
}

}  // namespace OpenCvAlgorithms
