
.. _program_listing_file_modules_algorithms_study_include_AlgorithmsStudy.cppm:

Program Listing for File AlgorithmsStudy.cppm
=============================================

|exhale_lsh| :ref:`Return to documentation for file <file_modules_algorithms_study_include_AlgorithmsStudy.cppm>` (``modules/algorithms_study/include/AlgorithmsStudy.cppm``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   
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
   
   inline constexpr int kVersion = 1;
   
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
