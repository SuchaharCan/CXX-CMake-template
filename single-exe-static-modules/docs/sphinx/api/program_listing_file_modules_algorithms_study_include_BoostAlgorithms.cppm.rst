
.. _program_listing_file_modules_algorithms_study_include_BoostAlgorithms.cppm:

Program Listing for File BoostAlgorithms.cppm
=============================================

|exhale_lsh| :ref:`Return to documentation for file <file_modules_algorithms_study_include_BoostAlgorithms.cppm>` (``modules/algorithms_study/include/BoostAlgorithms.cppm``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   
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
