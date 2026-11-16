
.. _program_listing_file_modules_algorithms_study_include_SqliteAlgorithms.cppm:

Program Listing for File SqliteAlgorithms.cppm
==============================================

|exhale_lsh| :ref:`Return to documentation for file <file_modules_algorithms_study_include_SqliteAlgorithms.cppm>` (``modules/algorithms_study/include/SqliteAlgorithms.cppm``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   
   module;
   
   #include <sqlite_orm/sqlite_orm.h>
   
   #include <string>
   
   export module SqliteAlgorithms;
   
   export namespace SqliteAlgorithms {
   
   struct Note {
     int id = 0;
     std::string text;
   };
   
   inline int insert_and_count(std::string_view text) {
     using namespace sqlite_orm;
     auto storage =
         make_storage(":memory:", make_table("notes", make_column("id", &Note::id, primary_key()),
                                             make_column("text", &Note::text)));
     storage.sync_schema();
     storage.insert(Note{-1, std::string(text)});
     return static_cast<int>(storage.count<Note>());
   }
   
   }  // namespace SqliteAlgorithms
