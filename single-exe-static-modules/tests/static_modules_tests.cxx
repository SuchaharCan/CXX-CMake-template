/**
 * @file static_modules_tests.cxx
 * @brief Stacks_Tools 静态模块接口测试（边界 / 并行 / 日志容量 / 函数对象遍历）。
 *
 * 覆盖策略（详见 docs/DESIGN_DOC.md）：
 * - 边界：空输入、缺失/非法 JSON、缺失搜索路径、未知目标、兜底插入、
 *   任务文件元数据、环形缓冲容量边界；
 * - 并行：8 线程并发生成唯一 ID、8 线程并发写接口日志且容量仍为 1000；
 * - 函数对象：for_each_backlog_id / for_each_recent 的访问顺序与计数；
 * - 时间：ProjectSync::format_file_time 输出本地 "YYYY-MM-DD HH:MM:SS"。
 */
#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <vector>

import std;
import InterfaceLog;
import ProjectSync;
import TaskManager;

namespace {

namespace fs = std::filesystem;

class TempDir {
 public:
  TempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::random_device random;
    root_ = fs::temp_directory_path() /
            ("stacks_tools_test_" + std::to_string(stamp) + "_" + std::to_string(random()));
    fs::create_directories(root_);
  }
  ~TempDir() {
    std::error_code error;
    fs::remove_all(root_, error);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const fs::path& root() const { return root_; }

 private:
  fs::path root_;
};

void write_file(const fs::path& path, std::string_view content) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path);
  output << content;
}

std::string read_file(const fs::path& path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

struct Fixture {
  TempDir dir;
  fs::path project_file;
  fs::path project_db;
  fs::path stack_db;
  ProjectSync::DbPaths db_paths;
  TaskManager::CreateTaskOptions task_options;

  Fixture() {
    project_file = dir.root() / "library" / "p1.project";
    project_db = dir.root() / "data" / "Project.json";
    stack_db = dir.root() / "data" / "Stack.json";
    db_paths = {project_db, stack_db};
    task_options = {project_db, stack_db};
    write_file(project_file,
               R"({"id":"p1","title":"Project One","stacks":[{"id":"s1","title":"Stack One"}]})");
    write_file(project_db, std::string(R"({"nodes":[{"id":"p1","title":"Project One","path":")") +
                               project_file.string() + R"("}]})");
    write_file(
        stack_db,
        R"({"nodes":[{"id":"p1","projectid":"p1","title":"Project One"},{"id":"s1","projectid":"p1","title":"Stack One"}]})");
  }
};

}  // namespace

// ---------------------------------------------------------------------------
// 边界测试：ID 生成
// ---------------------------------------------------------------------------

TEST(Boundary, GenerateIdHasFixedLengthAndAlphabet) {
  constexpr std::string_view kAlphabet =
      "-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz";
  for (int i = 0; i < 200; ++i) {
    const std::string id = TaskManager::generate_id().value;
    ASSERT_EQ(id.size(), 20U);
    for (const char character : id) {
      EXPECT_NE(kAlphabet.find(character), std::string_view::npos);
    }
  }
}

// ---------------------------------------------------------------------------
// 边界测试：ProjectSync
// ---------------------------------------------------------------------------

TEST(Boundary, SyncEmptySearchPathCreatesEmptyDatabases) {
  TempDir dir;
  const ProjectSync::DbPaths db_paths{dir.root() / "data" / "Project.json",
                                      dir.root() / "data" / "Stack.json"};
  const auto result = ProjectSync::sync_all("", db_paths);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->project_count, 0U);
  EXPECT_EQ(result->stack_count, 0U);
  EXPECT_TRUE(fs::exists(db_paths.project_db));
  EXPECT_TRUE(fs::exists(db_paths.stack_db));
}

TEST(Boundary, SyncMissingSearchPathIsReported) {
  TempDir dir;
  const ProjectSync::DbPaths db_paths{dir.root() / "data" / "Project.json",
                                      dir.root() / "data" / "Stack.json"};
  const auto result = ProjectSync::sync_all(dir.root() / "does-not-exist", db_paths);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ProjectSync::SyncError::search_path_missing);
  EXPECT_NE(std::string(ProjectSync::sync_error_message(result.error())).find("does not exist"),
            std::string::npos);
}

TEST(Boundary, SyncInvalidProjectFileFailsButKeepsScanning) {
  TempDir dir;
  const fs::path library = dir.root() / "library";
  write_file(library / "bad.project", "{not-json");
  const ProjectSync::DbPaths db_paths{dir.root() / "data" / "Project.json",
                                      dir.root() / "data" / "Stack.json"};
  const auto result = ProjectSync::sync_all(library, db_paths);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ProjectSync::SyncError::project_file_unreadable);
}

TEST(Boundary, SyncValidProjectAndNestedStackWritesMtime) {
  Fixture fixture;
  const auto result = ProjectSync::sync_all(fixture.project_file.parent_path(), fixture.db_paths);
  ASSERT_TRUE(result.has_value()) << ProjectSync::sync_error_message(result.error());
  EXPECT_EQ(result->project_count, 1U);
  EXPECT_EQ(result->stack_count, 2U);
  const std::string stack_db = read_file(fixture.stack_db);
  EXPECT_NE(stack_db.find("s1"), std::string::npos);
  const std::string project_db = read_file(fixture.project_db);
  const std::regex mtime_pattern(R"("mtime": "\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}")");
  EXPECT_TRUE(std::regex_search(project_db, mtime_pattern));
}

TEST(Boundary, FormatFileTimeProducesLocalTimestampPattern) {
  const fs::file_time_type now = fs::file_time_type::clock::now();
  const std::string formatted = ProjectSync::format_file_time(now);
  const std::regex pattern(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})");
  EXPECT_TRUE(std::regex_match(formatted, pattern)) << formatted;
}

// ---------------------------------------------------------------------------
// 边界测试：TaskManager
// ---------------------------------------------------------------------------

TEST(Boundary, CreateTaskRejectsEmptyTargetId) {
  Fixture fixture;
  const auto result =
      TaskManager::create_task(TaskManager::ProjectId(""), TaskManager::TaskTitle("title"),
                               TaskManager::TaskDescription("description"), fixture.task_options);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TaskManager::TaskError::empty_target_id);
}

TEST(Boundary, CreateTaskRejectsEmptyTitle) {
  Fixture fixture;
  const auto result =
      TaskManager::create_task(TaskManager::ProjectId("p1"), TaskManager::TaskTitle(""),
                               TaskManager::TaskDescription("description"), fixture.task_options);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TaskManager::TaskError::empty_title);
}

TEST(Boundary, CreateTaskMissingStackDbFails) {
  Fixture fixture;
  const TaskManager::CreateTaskOptions options{fixture.project_db,
                                               fixture.dir.root() / "missing.json"};
  const auto result =
      TaskManager::create_task(TaskManager::ProjectId("p1"), TaskManager::TaskTitle("title"),
                               TaskManager::TaskDescription("description"), options);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TaskManager::TaskError::stack_db_missing);
}

TEST(Boundary, CreateTaskUnknownTargetFails) {
  Fixture fixture;
  const auto result =
      TaskManager::create_task(TaskManager::ProjectId("nope"), TaskManager::TaskTitle("title"),
                               TaskManager::TaskDescription("description"), fixture.task_options);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TaskManager::TaskError::target_not_found);
}

TEST(Boundary, CreateTaskForProjectGoesToBacklogHead) {
  Fixture fixture;
  const auto first =
      TaskManager::create_task(TaskManager::ProjectId("p1"), TaskManager::TaskTitle("first"),
                               TaskManager::TaskDescription("description"), fixture.task_options);
  ASSERT_TRUE(first.has_value()) << TaskManager::task_error_message(first.error());
  EXPECT_EQ(first->task_id.value.size(), 20U);
  EXPECT_EQ(first->target_kind, "project");
  EXPECT_TRUE(
      fs::exists(fixture.project_file.parent_path() / "tasks" / (first->task_id.value + ".task")));

  const auto second =
      TaskManager::create_task(TaskManager::ProjectId("p1"), TaskManager::TaskTitle("second"),
                               TaskManager::TaskDescription("description"), fixture.task_options);
  ASSERT_TRUE(second.has_value());
  EXPECT_NE(second->task_id.value, first->task_id.value);

  const std::string project = read_file(fixture.project_file);
  const auto first_pos = project.find(first->task_id.value);
  const auto second_pos = project.find(second->task_id.value);
  EXPECT_NE(first_pos, std::string::npos);
  EXPECT_NE(second_pos, std::string::npos);
  EXPECT_LT(second_pos, first_pos);  // 新任务插入 backlog 头部
}

TEST(Boundary, CreateTaskForStackGoesToStackTasks) {
  Fixture fixture;
  const auto result =
      TaskManager::create_task(TaskManager::ProjectId("s1"), TaskManager::TaskTitle("stack task"),
                               TaskManager::TaskDescription("description"), fixture.task_options);
  ASSERT_TRUE(result.has_value()) << TaskManager::task_error_message(result.error());
  EXPECT_EQ(result->target_kind, "stack");
  const std::string project = read_file(fixture.project_file);
  EXPECT_NE(project.find(result->task_id.value), std::string::npos);
  EXPECT_NE(project.find("\"tasks\""), std::string::npos);
}

TEST(Boundary, CreateTaskWritesTaskMetadataFile) {
  Fixture fixture;
  const auto result = TaskManager::create_task(
      TaskManager::ProjectId("p1"), TaskManager::TaskTitle("meta title"),
      TaskManager::TaskDescription("meta description"), fixture.task_options);
  ASSERT_TRUE(result.has_value());
  const std::string task_file = read_file(result->task_file);
  EXPECT_NE(task_file.find("\"title\": \"meta title\""), std::string::npos);
  EXPECT_NE(task_file.find("\"description\": \"meta description\""), std::string::npos);
  EXPECT_NE(task_file.find("\"id\": \"" + result->task_id.value + "\""), std::string::npos);
  EXPECT_NE(task_file.find("\"project\": \"p1\""), std::string::npos);
  EXPECT_NE(task_file.find("\"created\""), std::string::npos);
  EXPECT_NE(task_file.find("\"updated\""), std::string::npos);
}

TEST(Boundary, ForEachBacklogVisitsNewestFirst) {
  Fixture fixture;
  const auto first =
      TaskManager::create_task(TaskManager::ProjectId("p1"), TaskManager::TaskTitle("first"),
                               TaskManager::TaskDescription(""), fixture.task_options);
  const auto second =
      TaskManager::create_task(TaskManager::ProjectId("p1"), TaskManager::TaskTitle("second"),
                               TaskManager::TaskDescription(""), fixture.task_options);
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());

  std::vector<std::string> visited;
  const auto count = TaskManager::for_each_backlog_id(
      fixture.project_file,
      [&visited](std::string_view task_id) { visited.emplace_back(task_id); });
  ASSERT_TRUE(count.has_value());
  EXPECT_EQ(*count, 2U);
  ASSERT_EQ(visited.size(), 2U);
  EXPECT_EQ(visited.front(), second->task_id.value);  // 最新在前
  EXPECT_EQ(visited.back(), first->task_id.value);
}

TEST(Boundary, ForEachBacklogOnMissingFileFails) {
  TempDir dir;
  const auto count = TaskManager::for_each_backlog_id(dir.root() / "missing.project",
                                                      [](std::string_view) { /* 不应被调用 */ });
  ASSERT_FALSE(count.has_value());
  EXPECT_EQ(count.error(), TaskManager::TaskError::project_file_unreadable);
}

TEST(Boundary, TaskErrorMessageCoversAllEnums) {
  using TaskManager::TaskError;
  const std::array<TaskError, 10> all = {
      TaskError::empty_target_id,         TaskError::empty_title,
      TaskError::stack_db_missing,        TaskError::stack_db_unreadable,
      TaskError::stack_db_no_nodes,       TaskError::target_not_found,
      TaskError::project_db_unreadable,   TaskError::project_file_not_found,
      TaskError::project_file_unreadable, TaskError::write_failure,
  };
  for (const TaskError error : all) {
    EXPECT_FALSE(TaskManager::task_error_message(error).empty());
  }
}

// ---------------------------------------------------------------------------
// 边界测试：InterfaceLog
// ---------------------------------------------------------------------------

TEST(Boundary, InterfaceLogRejectsEmptyPath) {
  const auto result = interface_log::init("");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), interface_log::LogError::invalid_path);
  EXPECT_NE(std::string(interface_log::log_error_message(result.error())).find("empty"),
            std::string::npos);
}

TEST(Boundary, InterfaceLogKeepsOnlyLast1000Lines) {
  TempDir dir;
  const fs::path log_path = dir.root() / "logs" / "interface.log";
  ASSERT_TRUE(interface_log::init(log_path).has_value());
  interface_log::clear();
  for (int i = 0; i < 1200; ++i) {
    interface_log::record(interface_log::ApiName("api"), interface_log::LogText(std::to_string(i)),
                          interface_log::LogText("value"), interface_log::ApiStatus::ok);
  }
  EXPECT_EQ(interface_log::count(), interface_log::kMaxLines);
  const auto lines = interface_log::recent();
  ASSERT_EQ(lines.size(), interface_log::kMaxLines);
  EXPECT_NE(lines.front().find("200"), std::string::npos);
  EXPECT_NE(lines.back().find("1199"), std::string::npos);

  bool found_log_file = false;
  for (const auto& entry : fs::directory_iterator(log_path.parent_path())) {
    if (entry.is_regular_file() && entry.path().filename().string().find("interface.log") == 0) {
      found_log_file = true;
      EXPECT_GT(fs::file_size(entry.path()), 0U);
    }
  }
  EXPECT_TRUE(found_log_file);
}

TEST(Boundary, InterfaceLogForEachVisitorSeesAllLines) {
  TempDir dir;
  ASSERT_TRUE(interface_log::init(dir.root() / "for-each.log").has_value());
  interface_log::clear();
  interface_log::record(interface_log::ApiName("api-a"), interface_log::LogText("1"),
                        interface_log::LogText("ok"), interface_log::ApiStatus::ok);
  interface_log::record(interface_log::ApiName("api-b"), interface_log::LogText("2"),
                        interface_log::LogText("err"), interface_log::ApiStatus::failed);

  std::vector<std::string> seen;
  interface_log::for_each_recent([&seen](std::string_view line) { seen.emplace_back(line); });
  ASSERT_EQ(seen.size(), 2U);
  EXPECT_NE(seen.front().find("api-a"), std::string::npos);
  EXPECT_NE(seen.back().find("status=fail"), std::string::npos);
  EXPECT_EQ(interface_log::log_path(), dir.root() / "for-each.log");
}

// ---------------------------------------------------------------------------
// 并行测试
// ---------------------------------------------------------------------------

TEST(Parallel, GenerateIdsAreUniqueAcrossThreads) {
  constexpr int kThreads = 8;
  constexpr int kPerThread = 300;
  std::vector<std::string> ids;
  std::mutex mutex;
  std::vector<std::thread> workers;
  workers.reserve(kThreads);
  for (int thread = 0; thread < kThreads; ++thread) {
    workers.emplace_back([&] {
      std::vector<std::string> local;
      local.reserve(kPerThread);
      for (int index = 0; index < kPerThread; ++index) {
        local.push_back(TaskManager::generate_id().value);
      }
      std::lock_guard lock(mutex);
      ids.insert(ids.end(), local.begin(), local.end());
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }
  ASSERT_EQ(ids.size(), static_cast<std::size_t>(kThreads) * kPerThread);
  const std::set<std::string> unique(ids.begin(), ids.end());
  EXPECT_EQ(unique.size(), ids.size());
}

TEST(Parallel, InterfaceLogIsThreadSafeAndCapped) {
  TempDir dir;
  ASSERT_TRUE(interface_log::init(dir.root() / "parallel.log").has_value());
  interface_log::clear();
  constexpr int kThreads = 8;
  constexpr int kPerThread = 200;
  std::vector<std::thread> workers;
  workers.reserve(kThreads);
  for (int thread = 0; thread < kThreads; ++thread) {
    workers.emplace_back([thread] {
      for (int index = 0; index < kPerThread; ++index) {
        interface_log::record(
            interface_log::ApiName("parallel"),
            interface_log::LogText(std::to_string(thread) + ":" + std::to_string(index)),
            interface_log::LogText("ok"), interface_log::ApiStatus::ok);
      }
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }
  EXPECT_EQ(interface_log::count(), interface_log::kMaxLines);
  EXPECT_EQ(interface_log::recent().size(), interface_log::kMaxLines);
}
