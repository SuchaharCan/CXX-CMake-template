/**
 * @file EigenAlgorithms.cppm
 * @module EigenAlgorithms
 * @brief Eigen（Conan）模块化封装：矩阵运算示例。
 */
module;

#include <Eigen/Dense>

export module EigenAlgorithms;

export namespace EigenAlgorithms {

using MatrixXd = Eigen::MatrixXd;
using VectorXd = Eigen::VectorXd;

inline double trace(const MatrixXd& m) {
  return m.trace();
}

inline MatrixXd multiply(const MatrixXd& a, const MatrixXd& b) {
  return a * b;
}

inline VectorXd solve(const MatrixXd& a, const VectorXd& b) {
  return a.colPivHouseholderQr().solve(b);
}

}  // namespace EigenAlgorithms
