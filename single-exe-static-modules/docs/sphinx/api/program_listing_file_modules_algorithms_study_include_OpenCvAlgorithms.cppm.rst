
.. _program_listing_file_modules_algorithms_study_include_OpenCvAlgorithms.cppm:

Program Listing for File OpenCvAlgorithms.cppm
==============================================

|exhale_lsh| :ref:`Return to documentation for file <file_modules_algorithms_study_include_OpenCvAlgorithms.cppm>` (``modules/algorithms_study/include/OpenCvAlgorithms.cppm``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   
   module;
   
   #include <cstddef>
   #include <opencv2/core.hpp>
   
   export module OpenCvAlgorithms;
   
   export namespace OpenCvAlgorithms {
   
   inline std::size_t zero_matrix_elements(int rows, int cols) {
     const cv::Mat matrix = cv::Mat::zeros(rows, cols, CV_32FC1);
     return matrix.total();
   }
   
   }  // namespace OpenCvAlgorithms
