/**
 * @file static_modules_tests.cxx
 * @brief Stacks_Tools 单元、边界、并发与 UI 适配测试。
 *
 * 测试边界：
 * - 每个 Fixture 使用 `tests/.runtime/unit-*` 空白目录，不读取真实 Stacks 数据；
 * - 单元测试覆盖模块成功路径、错误枚举、Stacks 字段兼容与 Alfred JSON；
 * - 并发测试覆盖 ID 与接口日志的锁/原子性；
 * - 变异测试通过 `just mutation-test` 验证关键常量/语义确实被这些用例保护。
 */
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "json.hpp"

import std;
import InterfaceLog;
import ProjectSync;
import ReminderSync;
import TaskManager;
import WorkflowUI;

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

#ifndef STACKS_TEST_RUNTIME_DIR
#define STACKS_TEST_RUNTIME_DIR "tests/.runtime"
#endif

std::atomic<std::uint64_t> g_test_sequence{0};

/**
 * @brief 自动创建/清理空白测试工作区。
 * @note 设置 `KEEP_STACKS_TEST_WORKSPACE=1` 可保留目录用于调试。
 */
class TempDir {
 public:
  TempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = fs::path(STACKS_TEST_RUNTIME_DIR) /
            std::format("unit-{}-{}", stamp, g_test_sequence.fetch_add(1));
    fs::remove_all(root_);
    fs::create_directories(root_);
    std::println("[TEST WORKSPACE] {}", fs::absolute(root_).string());
  }
  ~TempDir() {
    if (std::getenv("KEEP_STACKS_TEST_WORKSPACE") == nullptr) {
      std::error_code error;
      fs::remove_all(root_, error);
    }
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  [[nodiscard]] const fs::path& root() const { return root_; }

 private:
  fs::path root_;
};

/// 写文本测试文件，并自动创建父目录。
void write_text(const fs::path& path, std::string_view content) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << content;
}

/// 以 Stacks 使用的四空格缩进写入测试 JSON。
void write_json(const fs::path& path, const json& value) {
  write_text(path, value.dump(4));
}

/// 读取测试 JSON；调用方在测试中负责断言结构。
json read_json(const fs::path& path) {
  std::ifstream input(path);
  json value;
  input >> value;
  return value;
}

/**
 * @brief 最小 Stacks 工作区 Fixture：一个项目、两个清单、三个索引。
 * @note create() 统一走真实 TaskManager::create_task，不直接构造任务文件。
 */
struct Fixture {
  TempDir dir;
  fs::path library;
  fs::path project_file;
  fs::path project_db;
  fs::path stack_db;
  fs::path timer_db;
  TaskManager::DatabasePaths databases;
  TaskManager::CreateTaskOptions create_options;

  Fixture() {
    // 路径全部位于 TempDir 根下，保证不同测试/并行进程相互隔离。
    library = dir.root() / "library-a";
    project_file = library / "p1.project";
    project_db = dir.root() / "data" / "Project.json";
    stack_db = dir.root() / "data" / "Stack.json";
    timer_db = dir.root() / "data" / "Timers.json";
    databases = {project_db, stack_db, timer_db};
    create_options = {databases, library};

    // `.project` 同时包含 backlog 和两个 Stack，覆盖三级任务目标。
    write_json(project_file,
               {
                   {"id", "p1"},
                   {"title", "Project One"},
                   {"backlog", json::array()},
                   {"stacks", json::array({
                                  {{"id", "s1"}, {"title", "Stack One"}, {"tasks", json::array()}},
                                  {{"id", "s2"}, {"title", "Stack Two"}, {"tasks", json::array()}},
                              })},
                   {"updated", "2026-11-01 00:00:00"},
               });
    // Project 索引保存工作区与项目文件路径；library_paths[0] 是默认 Inbox。
    write_json(project_db, {
                               {"library_paths", json::array({library.string()})},
                               {"nodes", json::array({{{"id", "p1"},
                                                       {"title", "Project One"},
                                                       {"path", project_file.string()},
                                                       {"library_path", library.string()}}})},
                           });
    // Stack 索引提供项目/清单的 kind，WorkflowUI 用它生成创建 action。
    write_json(
        stack_db,
        {
            {"nodes",
             json::array({
                 {{"id", "p1"}, {"projectid", "p1"}, {"title", "Project One"}, {"kind", "project"}},
                 {{"id", "s1"}, {"projectid", "p1"}, {"title", "Stack One"}, {"kind", "stack"}},
             })},
        });
    // 计时库初始为空；start/stop 测试会写入并清理。
    write_json(timer_db, {{"timers", json::object()}});
  }

  /// 创建任务并断言成功；失败时返回可安全继续的占位结果。
  TaskManager::TaskResult create(std::string target, std::string title,
                                 std::string description = {},
                                 std::optional<std::string> due = std::nullopt) {
    const auto result = TaskManager::create_task(
        TaskManager::TargetId(std::move(target)),
        TaskManager::TaskInput{.title = TaskManager::TaskTitle(std::move(title)),
                               .description = TaskManager::TaskDescription(std::move(description)),
                               .start_date = std::nullopt,
                               .due_date = std::move(due),
                               .priority = ""},
        create_options);
    EXPECT_TRUE(result.has_value())
        << (result ? "" : TaskManager::task_error_message(result.error()));
    return result.value_or(
        TaskManager::TaskResult{TaskManager::TaskId(""), dir.root() / "missing.task", "", ""});
  }
};

}  // namespace

// ---- 边界与业务不变量 -----------------------------------------------------

/// 固定 ID 长度/字母表，是 id-length 变异体的杀死测试。
TEST(Boundary, GenerateIdFixedLengthAndAlphabet) {
  constexpr std::string_view alphabet =
      "-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz";
  for (int index = 0; index < 200; ++index) {
    const std::string id = TaskManager::generate_id().value;
    ASSERT_EQ(id.size(), 20U);
    for (const char character : id) {
      EXPECT_NE(alphabet.find(character), std::string_view::npos);
    }
  }
}

/// 验证递归扫描、路径去重、library_path/mtime 元数据与项目首条 Stack 节点。
TEST(Boundary, ProjectSyncScansNestedProjectsAndWritesMetadata) {
  TempDir dir;
  const fs::path nested = dir.root() / "library" / "workspace" / "nested";
  fs::create_directories(nested);
  write_json(nested / "p1.project", {{"id", "p1"}, {"title", "Nested"}, {"stacks", json::array()}});
  const ProjectSync::DbPaths paths{dir.root() / "data" / "Project.json",
                                   dir.root() / "data" / "Stack.json"};
  write_json(paths.project_db,
             {{"library_paths", json::array({(dir.root() / "library").string(),
                                             (dir.root() / "library").string() + "/"})}});
  const auto result = ProjectSync::sync_all(dir.root() / "library", paths);
  ASSERT_TRUE(result.has_value()) << ProjectSync::sync_error_message(result.error());
  EXPECT_EQ(result->project_count, 1U);
  EXPECT_EQ(result->stack_count, 1U);
  const json project_db = read_json(paths.project_db);
  EXPECT_EQ(project_db["nodes"][0]["id"], "p1");
  EXPECT_TRUE(project_db["nodes"][0].contains("mtime"));
  EXPECT_TRUE(project_db["nodes"][0].contains("library_path"));
  EXPECT_EQ(project_db["library_paths"].size(), 1U);
}

/// 验证不存在路径与损坏 `.project` 都返回明确 SyncError，而不是抛异常。
TEST(Boundary, ProjectSyncRejectsMissingAndInvalidProjectFile) {
  TempDir dir;
  const ProjectSync::DbPaths paths{dir.root() / "data" / "Project.json",
                                   dir.root() / "data" / "Stack.json"};
  const auto missing = ProjectSync::sync_all(dir.root() / "missing", paths);
  ASSERT_FALSE(missing.has_value());
  EXPECT_EQ(missing.error(), ProjectSync::SyncError::search_path_missing);

  const fs::path library = dir.root() / "library";
  write_text(library / "bad.project", "{not-json");
  const auto invalid = ProjectSync::sync_all(library, paths);
  ASSERT_FALSE(invalid.has_value());
  EXPECT_EQ(invalid.error(), ProjectSync::SyncError::project_file_unreadable);
}

/// 验证项目级新任务头插 backlog（backlog 顺序变异测试依赖此断言）。
TEST(Boundary, CreateTaskForProjectGoesToBacklogHead) {
  Fixture fixture;
  const auto first = fixture.create("p1", "first", "description");
  const auto second = fixture.create("p1", "second");
  const json project = read_json(fixture.project_file);
  ASSERT_EQ(project["backlog"].size(), 2U);
  EXPECT_EQ(project["backlog"][0], second.task_id.value);
  EXPECT_EQ(project["backlog"][1], first.task_id.value);
}

/// 验证 Stack 目标只写入对应 stack.tasks，不误写项目 backlog。
TEST(Boundary, CreateTaskForStackWritesStackTasks) {
  Fixture fixture;
  const auto result = fixture.create("s1", "stack task");
  const json project = read_json(fixture.project_file);
  ASSERT_EQ(project["stacks"][0]["tasks"].size(), 1U);
  EXPECT_EQ(project["stacks"][0]["tasks"][0], result.task_id.value);
}

/// 验证 Inbox 使用 project=inbox，并落在指定工作区 tasks 目录。
TEST(Boundary, CreateInboxUsesInboxProject) {
  Fixture fixture;
  const auto result = TaskManager::create_inbox_task(
      TaskManager::TaskInput{.title = TaskManager::TaskTitle("Inbox task"),
                             .description = TaskManager::TaskDescription("body"),
                             .start_date = std::nullopt,
                             .due_date = std::nullopt,
                             .priority = ""},
      fixture.create_options);
  ASSERT_TRUE(result.has_value()) << TaskManager::task_error_message(result.error());
  const json task = read_json(result->task_file);
  EXPECT_EQ(task["project"], "inbox");
  EXPECT_EQ(result->task_file.parent_path(), fixture.library / "tasks");
  const auto inbox = TaskManager::list_tasks(
      fixture.databases, TaskManager::TaskListOptions{.target = TaskManager::TargetId("inbox"),
                                                      .query = "",
                                                      .include_done = true,
                                                      .include_archived = false});
  ASSERT_TRUE(inbox.has_value());
  ASSERT_EQ(inbox->size(), 1U);
  EXPECT_EQ(inbox->front().project_id, "inbox");
}

/// 验证项目、清单、Backlog 与任务的统一查询/过滤模型。
TEST(Boundary, ListBoardsAndTasksFiltersTargetAndText) {
  Fixture fixture;
  fixture.create("s1", "alpha task");
  fixture.create("s2", "beta task");
  const auto boards = TaskManager::list_boards(fixture.databases);
  ASSERT_TRUE(boards.has_value());
  EXPECT_EQ(boards->size(), 3U);
  const auto tasks = TaskManager::list_tasks(
      fixture.databases,
      TaskManager::TaskListOptions{.target = TaskManager::TargetId("s1"), .query = "alpha"});
  ASSERT_TRUE(tasks.has_value());
  ASSERT_EQ(tasks->size(), 1U);
  EXPECT_EQ(tasks->front().board_title, "Stack One");
}

/// 验证标题、描述、开始/截止日期写回 Stacks 原字段。
TEST(Boundary, UpdateTitleDescriptionAndDates) {
  Fixture fixture;
  const auto created = fixture.create("p1", "old", "old body");
  TaskManager::TaskPatch patch;
  patch.title = "new";
  patch.description = "new body";
  patch.start_date = "2026-11-02";
  patch.due_date = "2026-11-08";
  const auto updated = TaskManager::update_task(TaskManager::TaskId(created.task_id.value), patch,
                                                fixture.databases);
  ASSERT_TRUE(updated.has_value()) << TaskManager::task_error_message(updated.error());
  EXPECT_EQ(updated->title, "new");
  EXPECT_EQ(updated->description, "new body");
  EXPECT_EQ(updated->start_date, "2026-11-02");
  EXPECT_EQ(updated->due_date, "2026-11-08");
  const json raw = read_json(created.task_file);
  EXPECT_EQ(raw["startdate"], "2026-11-02");
  EXPECT_EQ(raw["duedate"], "2026-11-08");
}

/// 验证相对日期、英文星期和 ISO 周边界的固定锚点语义。
TEST(Boundary, NormalizeDateAndWeekBounds) {
  const auto tomorrow = TaskManager::normalize_date("tomorrow", "2026-11-01");
  ASSERT_TRUE(tomorrow.has_value());
  EXPECT_EQ(*tomorrow, "2026-11-02");
  const auto plus_three = TaskManager::normalize_date("+3d", "2026-11-01");
  ASSERT_TRUE(plus_three.has_value());
  EXPECT_EQ(*plus_three, "2026-11-04");
  const auto monday = TaskManager::normalize_date("monday", "2026-11-01");
  ASSERT_TRUE(monday.has_value());
  EXPECT_EQ(*monday, "2026-11-02");
  EXPECT_FALSE(TaskManager::normalize_date("2026-02-30").has_value());

  const auto bounds = TaskManager::week_bounds("2026-11-01");
  EXPECT_EQ(bounds.first, "2026-10-26");
  EXPECT_EQ(bounds.last, "2026-11-01");
}

/// 验证跨工作区聚合、排除完成项，并严格使用周一至周日边界。
TEST(Boundary, WeekDueAggregatesMultipleLibrariesAndExcludesDone) {
  Fixture fixture;
  fixture.create("p1", "monday", "", "2026-10-26");
  fixture.create("s1", "sunday", "", "2026-11-01");
  fixture.create("s2", "next-week", "", "2026-11-02");
  const auto completed = fixture.create("p1", "done", "", "2026-10-30");
  ASSERT_TRUE(TaskManager::set_task_done(TaskManager::TaskId(completed.task_id.value), true,
                                         fixture.databases)
                  .has_value());

  const fs::path library2 = fixture.dir.root() / "library-b";
  const fs::path project2 = library2 / "p2.project";
  write_json(project2, {{"id", "p2"},
                        {"title", "Project Two"},
                        {"backlog", json::array()},
                        {"stacks", json::array()}});
  json project_db = read_json(fixture.project_db);
  project_db["library_paths"].push_back(library2.string());
  project_db["nodes"].push_back({{"id", "p2"},
                                 {"title", "Project Two"},
                                 {"path", project2.string()},
                                 {"library_path", library2.string()}});
  write_json(fixture.project_db, project_db);
  const auto second =
      TaskManager::create_task(TaskManager::TargetId("p2"),
                               TaskManager::TaskInput{
                                   .title = TaskManager::TaskTitle("second library"),
                                   .description = TaskManager::TaskDescription(""),
                                   .start_date = std::nullopt,
                                   .due_date = "2026-10-31",
                                   .priority = "",
                               },
                               fixture.create_options);
  ASSERT_TRUE(second.has_value()) << TaskManager::task_error_message(second.error());

  const auto weekly = TaskManager::list_week_due(fixture.databases, "2026-11-01");
  ASSERT_TRUE(weekly.has_value()) << TaskManager::task_error_message(weekly.error());
  std::set<std::string> titles;
  for (const auto& task : *weekly) {
    titles.insert(task.title);
  }
  EXPECT_EQ(titles, (std::set<std::string>{"monday", "sunday", "second library"}));
}

/// 验证 done/progress/completed 三字段同写，以及重开时清除 completed。
TEST(Boundary, DoneWritesStacksCompletionFieldsAndReopenClearsThem) {
  Fixture fixture;
  const auto created = fixture.create("p1", "finish me");
  const auto done = TaskManager::set_task_done(TaskManager::TaskId(created.task_id.value), true,
                                               fixture.databases);
  ASSERT_TRUE(done.has_value()) << TaskManager::task_error_message(done.error());
  json raw = read_json(created.task_file);
  EXPECT_EQ(raw["done"], true);
  EXPECT_EQ(raw["progress"], 100);
  EXPECT_TRUE(raw.contains("completed"));

  const auto reopened = TaskManager::set_task_done(TaskManager::TaskId(created.task_id.value),
                                                   false, fixture.databases);
  ASSERT_TRUE(reopened.has_value());
  raw = read_json(created.task_file);
  EXPECT_EQ(raw["done"], false);
  EXPECT_EQ(raw["progress"], 0);
  EXPECT_FALSE(raw.contains("completed"));
}

/// 验证计时重复启动分类、timelog 字段和 Timers.json 清理。
TEST(Boundary, TimerStopWritesStacksTimelogAndClearsState) {
  Fixture fixture;
  const auto created = fixture.create("p1", "timed task");
  const auto started =
      TaskManager::start_timer(TaskManager::TaskId(created.task_id.value), fixture.databases);
  ASSERT_TRUE(started.has_value()) << TaskManager::task_error_message(started.error());
  const auto duplicate =
      TaskManager::start_timer(TaskManager::TaskId(created.task_id.value), fixture.databases);
  ASSERT_FALSE(duplicate.has_value());
  EXPECT_EQ(duplicate.error(), TaskManager::TaskError::timer_already_running);

  const auto stopped =
      TaskManager::stop_timer(TaskManager::TaskId(created.task_id.value), fixture.databases);
  ASSERT_TRUE(stopped.has_value()) << TaskManager::task_error_message(stopped.error());
  EXPECT_GE(stopped->duration_seconds, 1);
  const json raw = read_json(created.task_file);
  ASSERT_TRUE(raw.contains("timelogs"));
  ASSERT_EQ(raw["timelogs"].size(), 1U);
  EXPECT_EQ(raw["timelogs"][0]["duration"], stopped->duration_seconds);
  EXPECT_EQ(raw["timelogs"][0]["id"], stopped->timelog_id);
  EXPECT_TRUE(read_json(fixture.timer_db)["timers"].empty());
}

/// 验证归档可恢复语义，以及删除时项目引用移除和 `.trash` 文件落地。
TEST(Boundary, ArchiveAndDeleteAreObservableAndRecoverable) {
  Fixture fixture;
  const auto archived = fixture.create("p1", "archive me");
  ASSERT_TRUE(
      TaskManager::archive_task(TaskManager::TaskId(archived.task_id.value), fixture.databases)
          .has_value());
  const auto visible = TaskManager::list_tasks(fixture.databases);
  ASSERT_TRUE(visible.has_value());
  EXPECT_TRUE(std::ranges::none_of(
      *visible, [&](const auto& task) { return task.id == archived.task_id.value; }));

  const auto deleted = fixture.create("s1", "delete me");
  const auto result =
      TaskManager::delete_task(TaskManager::TaskId(deleted.task_id.value), fixture.databases);
  ASSERT_TRUE(result.has_value()) << TaskManager::task_error_message(result.error());
  EXPECT_FALSE(fs::exists(deleted.task_file));
  EXPECT_TRUE(fs::exists(result->trash_file));
  const json project = read_json(fixture.project_file);
  EXPECT_TRUE(std::ranges::none_of(project["stacks"][0]["tasks"],
                                   [&](const json& id) { return id == deleted.task_id.value; }));
}

/// 验证空标题/不存在目标/非法日期不会抛出，而是返回稳定的 TaskError。
TEST(Boundary, ErrorsAreClassifiedWithoutThrowing) {
  Fixture fixture;
  const auto empty = TaskManager::create_task(
      TaskManager::TargetId("p1"),
      TaskManager::TaskInput{.title = TaskManager::TaskTitle(" "),
                             .description = TaskManager::TaskDescription(""),
                             .start_date = std::nullopt,
                             .due_date = std::nullopt,
                             .priority = ""},
      fixture.create_options);
  ASSERT_FALSE(empty.has_value());
  EXPECT_EQ(empty.error(), TaskManager::TaskError::empty_title);
  const auto target = TaskManager::create_task(
      TaskManager::TargetId("missing"),
      TaskManager::TaskInput{.title = TaskManager::TaskTitle("title"),
                             .description = TaskManager::TaskDescription(""),
                             .start_date = std::nullopt,
                             .due_date = std::nullopt,
                             .priority = ""},
      fixture.create_options);
  ASSERT_FALSE(target.has_value());
  EXPECT_EQ(target.error(), TaskManager::TaskError::target_not_found);
  const auto bad_date = TaskManager::create_task(
      TaskManager::TargetId("p1"),
      TaskManager::TaskInput{.title = TaskManager::TaskTitle("title"),
                             .description = TaskManager::TaskDescription(""),
                             .start_date = std::nullopt,
                             .due_date = "2026-13-40",
                             .priority = ""},
      fixture.create_options);
  ASSERT_FALSE(bad_date.has_value());
  EXPECT_EQ(bad_date.error(), TaskManager::TaskError::invalid_date);
}

/// 验证 Script Filter JSON 与 action token 能端到端创建带完整参数的任务。
TEST(Boundary, WorkflowUiEmitsValidAlfredJsonAndExecutesCreateAction) {
  Fixture fixture;
  const WorkflowUI::Context context{
      .workflow_dir = fixture.dir.root(),
      .databases = fixture.databases,
      .inbox_library = fixture.library,
  };
  const std::string main_feedback = WorkflowUI::filter(context, "");
  const json main_json = json::parse(main_feedback);
  EXPECT_GT(main_json["items"].size(), 0U);
  EXPECT_EQ(main_json["skipknowledge"], true);

  const std::string payload = "UI task|UI body|2026-10-30|2026-11-01|high";
  const json create_json = json::parse(WorkflowUI::filter(context, "add:p1 " + payload));
  ASSERT_EQ(create_json["items"].size(), 1U);
  const auto action =
      WorkflowUI::action(context, create_json["items"][0]["arg"].get<std::string>());
  ASSERT_TRUE(action.has_value()) << action.error();
  EXPECT_NE(action->find("任务已创建"), std::string::npos);

  const auto tasks = TaskManager::list_tasks(fixture.databases);
  ASSERT_TRUE(tasks.has_value());
  ASSERT_EQ(tasks->size(), 1U);
  EXPECT_EQ(tasks->front().title, "UI task");
  EXPECT_EQ(tasks->front().priority, "high");
  EXPECT_EQ(tasks->front().start_date, "2026-10-30");
  EXPECT_EQ(tasks->front().due_date, "2026-11-01");
}

/// 验证 base64url 对 Unicode、引号、换行和 shell 元字符的往返安全性。
TEST(Boundary, Base64RoundTripsUnicodeAndQuotes) {
  const std::string value = "标题 \"quoted\" | 内容\n$()";
  const auto decoded = WorkflowUI::base64url_decode(WorkflowUI::base64url_encode(value));
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(*decoded, value);
}

/// 验证提醒脚本包含去重搜索，且 dry-run 不触发外部应用。
TEST(Boundary, ReminderSyncDryRunAndScriptsContainDedupMarker) {
  const auto reminders = ReminderSync::build_reminders_script();
  const auto omnifocus = ReminderSync::build_omnifocus_script();
  EXPECT_NE(reminders.find("body contains marker"), std::string::npos);
  EXPECT_NE(omnifocus.find("note of candidate contains marker"), std::string::npos);

  ReminderSync::SyncOptions options;
  options.dry_run = true;
  const auto result =
      ReminderSync::sync_task(ReminderSync::TaskReminder{.task_id = "abcdefghijklmnopqrst",
                                                         .title = "sync me",
                                                         .description = "body",
                                                         .due_date = "2026-11-08"},
                              ReminderSync::Provider::both, options);
  ASSERT_TRUE(result.has_value()) << ReminderSync::sync_error_message(result.error());
  EXPECT_TRUE(result->reminders_ok);
  EXPECT_TRUE(result->omnifocus_ok);
  EXPECT_EQ(result->marker, "[Stacks-ID:abcdefghijklmnopqrst]");
}

// ---- 并发与日志保留 -------------------------------------------------------

/// 8 线程并发生成 ID，断言全部唯一。
TEST(Parallel, GenerateIdsAreUniqueAcrossThreads) {
  constexpr int kThreads = 8;
  constexpr int kPerThread = 300;
  std::vector<std::string> ids;
  std::mutex mutex;
  std::vector<std::thread> workers;
  for (int thread = 0; thread < kThreads; ++thread) {
    workers.emplace_back([&] {
      std::vector<std::string> local;
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
  EXPECT_EQ(std::set<std::string>(ids.begin(), ids.end()).size(), ids.size());
}

/// 8 线程并发写接口日志，最终环形缓冲仍严格限制为 kMaxLines。
TEST(Parallel, InterfaceLogRemainsThreadSafeAndCapped) {
  TempDir dir;
  ASSERT_TRUE(interface_log::init(dir.root() / "interface.log").has_value());
  interface_log::clear();
  std::vector<std::thread> workers;
  for (int thread = 0; thread < 8; ++thread) {
    workers.emplace_back([thread] {
      for (int index = 0; index < 200; ++index) {
        interface_log::record(interface_log::ApiName("parallel"),
                              interface_log::LogText(std::format("{}:{}", thread, index)),
                              interface_log::LogText("ok"), interface_log::ApiStatus::ok);
      }
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }
  EXPECT_EQ(interface_log::count(), interface_log::kMaxLines);
}

/// 只删除超过三个月的接口/trace 日志，保留未来日期与无关文件。
TEST(Boundary, InterfaceLogRemovesFilesOlderThanThreeMonths) {
  TempDir dir;
  const fs::path old_interface = dir.root() / "interface.log_20000101_1.log";
  const fs::path old_trace = dir.root() / "trace-20000101.log";
  const fs::path current_interface = dir.root() / "interface.log_20991231_1.log";
  const fs::path current_trace = dir.root() / "trace-20991231.log";
  write_text(old_interface, "old\n");
  write_text(old_trace, "old\n");
  write_text(current_interface, "current\n");
  write_text(current_trace, "current\n");

  EXPECT_EQ(interface_log::cleanup_old_logs(dir.root() / "interface.log", 3), 2U);
  EXPECT_FALSE(fs::exists(old_interface));
  EXPECT_FALSE(fs::exists(old_trace));
  EXPECT_TRUE(fs::exists(current_interface));
  EXPECT_TRUE(fs::exists(current_trace));
}
