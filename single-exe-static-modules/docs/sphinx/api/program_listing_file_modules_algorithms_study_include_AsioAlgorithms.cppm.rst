
.. _program_listing_file_modules_algorithms_study_include_AsioAlgorithms.cppm:

Program Listing for File AsioAlgorithms.cppm
============================================

|exhale_lsh| :ref:`Return to documentation for file <file_modules_algorithms_study_include_AsioAlgorithms.cppm>` (``modules/algorithms_study/include/AsioAlgorithms.cppm``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   
   module;
   
   #include <asio.hpp>
   #include <chrono>
   #include <cstddef>
   
   export module AsioAlgorithms;
   
   export namespace AsioAlgorithms {
   
   inline std::size_t wait_for(std::chrono::milliseconds duration) {
     asio::io_context io;
     asio::steady_timer timer(io, duration);
     timer.wait();
     return io.run();
   }
   
   }  // namespace AsioAlgorithms
