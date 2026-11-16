
.. _program_listing_file_modules_algorithms_study_include_EigenAlgorithms.cppm:

Program Listing for File EigenAlgorithms.cppm
=============================================

|exhale_lsh| :ref:`Return to documentation for file <file_modules_algorithms_study_include_EigenAlgorithms.cppm>` (``modules/algorithms_study/include/EigenAlgorithms.cppm``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   
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
