
.. _program_listing_file_modules_algorithms_study_include_JsonAlgorithms.cppm:

Program Listing for File JsonAlgorithms.cppm
============================================

|exhale_lsh| :ref:`Return to documentation for file <file_modules_algorithms_study_include_JsonAlgorithms.cppm>` (``modules/algorithms_study/include/JsonAlgorithms.cppm``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   
   module;
   
   #include <nlohmann/json.hpp>
   #include <string>
   #include <string_view>
   
   export module JsonAlgorithms;
   
   export namespace JsonAlgorithms {
   
   using json = nlohmann::json;
   
   inline json parse(std::string_view text) {
     return json::parse(text);
   }
   
   inline std::string dump(const json& j) {
     return j.dump();
   }
   
   inline json merge(json lhs, const json& rhs) {
     lhs.update(rhs);
     return lhs;
   }
   
   }  // namespace JsonAlgorithms
