/**
 * @file SqliteAlgorithms.cppm
 * @module SqliteAlgorithms
 * @brief sqlite_orm（Conan）模块化封装：内存库 CRUD 示例。
 */
module;

#include <sqlite_orm/sqlite_orm.h>

#include <string>

export module SqliteAlgorithms;

export namespace SqliteAlgorithms {

struct Note {
  int id = 0;
  std::string text;
};

/// 在内存数据库中插入一条记录并返回当前行数。
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
