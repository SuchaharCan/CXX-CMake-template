
.. _program_listing_file_modules_algorithms_study_include_AbseilAlgorithms.cppm:

Program Listing for File AbseilAlgorithms.cppm
==============================================

|exhale_lsh| :ref:`Return to documentation for file <file_modules_algorithms_study_include_AbseilAlgorithms.cppm>` (``modules/algorithms_study/include/AbseilAlgorithms.cppm``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   
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
