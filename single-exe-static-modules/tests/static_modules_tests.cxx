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
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
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

/// 测试期间临时覆盖 Alfred 环境变量，析构时恢复原值/缺失状态。
class ScopedEnvironmentVariable {
 public:
  ScopedEnvironmentVariable(std::string name, std::string value) : name_(std::move(name)) {
    if (const char* current = std::getenv(name_.c_str()); current != nullptr) {
      previous_ = current;
    }
    ::setenv(name_.c_str(), value.c_str(), 1);
  }

  ~ScopedEnvironmentVariable() {
    if (previous_) {
      ::setenv(name_.c_str(), previous_->c_str(), 1);
    } else {
      ::unsetenv(name_.c_str());
    }
  }

  ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
  ScopedEnvironmentVariable& operator=(const ScopedEnvironmentVariable&) = delete;

 private:
  std::string name_;
  std::optional<std::string> previous_;
};

/**
 * @brief 自动创建/清理空白测试工作区。
 * @note 设置 `KEEP_STACKS_TEST_WORKSPACE=1` 可保留目录用于调试。
 */
class TempDir {
 public:
  TempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    // CTest 并行启动多个测试进程；PID 必须参与目录名，避免 steady_clock 同 tick 碰撞。
    root_ = fs::path(STACKS_TEST_RUNTIME_DIR) /
            std::format("unit-{}-{}-{}", ::getpid(), stamp, g_test_sequence.fetch_add(1));
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
 * @brief 创建 Stacks 2 固定工作区骨架。
 * @note 顶层 companies/documents.tree/people 是普通文件，tasks/timeboxes 是目录；
 *       `.project` 不属于有效性必要条件，由测试按场景单独添加。
 */
void create_valid_workspace(const fs::path& path) {
  fs::create_directories(path / "tasks");
  fs::create_directories(path / "timeboxes");
  write_text(path / "companies", "{}\n");
  write_text(path / "documents.tree", "{}\n");
  write_text(path / "people", "{}\n");
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

    create_valid_workspace(library);
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
    write_json(stack_db, {
                             {"nodes", json::array({
                                           {{"id", "p1"},
                                            {"projectid", "p1"},
                                            {"title", "Project One"},
                                            {"kind", "project"},
                                            {"project_path", project_file.string()},
                                            {"library_path", library.string()}},
                                           {{"id", "s1"},
                                            {"projectid", "p1"},
                                            {"title", "Stack One"},
                                            {"kind", "stack"},
                                            {"project_path", project_file.string()},
                                            {"library_path", library.string()}},
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
  const fs::path library = dir.root() / "library";
  create_valid_workspace(library);
  const fs::path nested = library / "workspace" / "nested";
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
  create_valid_workspace(library);
  write_text(library / "bad.project", "{not-json");
  const auto invalid = ProjectSync::sync_all(library, paths);
  ASSERT_FALSE(invalid.has_value());
  EXPECT_EQ(invalid.error(), ProjectSync::SyncError::project_file_unreadable);
}

/// 验证有效工作区的五个固定成员类型，以及 `.project` 是可选项目提示。
TEST(Boundary, WorkspaceInspectionRequiresFiveExactEntryTypes) {
  TempDir dir;
  const fs::path workspace = dir.root() / "workspace";
  create_valid_workspace(workspace);
  auto status = ProjectSync::inspect_workspace(workspace);
  EXPECT_TRUE(status.exists);
  EXPECT_TRUE(status.is_directory);
  EXPECT_TRUE(status.structure_valid);
  EXPECT_TRUE(status.missing_entries.empty());
  EXPECT_FALSE(status.has_projects);

  write_json(workspace / "p1.project", {{"id", "p1"}, {"stacks", json::array()}});
  status = ProjectSync::inspect_workspace(workspace);
  EXPECT_TRUE(status.structure_valid);
  EXPECT_TRUE(status.has_projects);
  EXPECT_EQ(status.project_file_count, 1U);

  struct MemberCase {
    std::string name;
    bool should_be_directory;
  };
  const std::array<MemberCase, 5> cases{{
      {"companies", false},
      {"documents.tree", false},
      {"people", false},
      {"tasks", true},
      {"timeboxes", true},
  }};
  for (std::size_t index = 0; index < cases.size(); ++index) {
    const fs::path invalid = dir.root() / std::format("invalid-{}", index);
    create_valid_workspace(invalid);
    fs::remove_all(invalid / cases[index].name);
    if (cases[index].should_be_directory) {
      write_text(invalid / cases[index].name, "{}\n");
    } else {
      fs::create_directories(invalid / cases[index].name);
    }
    const auto invalid_status = ProjectSync::inspect_workspace(invalid);
    EXPECT_FALSE(invalid_status.structure_valid);
    EXPECT_NE(std::ranges::find(invalid_status.missing_entries, cases[index].name),
              invalid_status.missing_entries.end());
  }
}

/// 验证新增路径必须是完整工作区，已保存的失效路径则保留用于诊断/移除。
TEST(Boundary, SyncRejectsInvalidWorkspaceAndListsInvalidSavedPath) {
  TempDir dir;
  const fs::path valid = dir.root() / "valid";
  const fs::path invalid = dir.root() / "invalid";
  create_valid_workspace(valid);
  fs::create_directories(invalid);
  write_text(invalid / "companies", "{}\n");
  const ProjectSync::DbPaths paths{dir.root() / "data" / "Project.json",
                                   dir.root() / "data" / "Stack.json"};

  const auto rejected = ProjectSync::sync_all(invalid, paths);
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error(), ProjectSync::SyncError::workspace_invalid);
  EXPECT_FALSE(fs::exists(paths.project_db));

  write_json(paths.project_db,
             {{"library_paths", json::array({invalid.string()})}, {"nodes", json::array()}});
  write_json(paths.stack_db, {{"nodes", json::array()}});
  const auto listed = ProjectSync::list_workspaces(paths);
  ASSERT_TRUE(listed.has_value()) << ProjectSync::sync_error_message(listed.error());
  ASSERT_EQ(listed->size(), 1U);
  EXPECT_FALSE(listed->front().structure_valid);
  EXPECT_EQ(listed->front().path, fs::weakly_canonical(invalid));

  const auto synced = ProjectSync::sync_all("", paths);
  ASSERT_TRUE(synced.has_value());
  EXPECT_TRUE(synced->library_paths.empty());
  ASSERT_EQ(synced->invalid_libraries.size(), 1U);
  EXPECT_EQ(read_json(paths.project_db)["library_paths"].size(), 1U);
}

/// 验证路径移除只改 Project/Stack/Timers 三个插件索引，工作区文件保持不变。
TEST(Boundary, RemoveWorkspaceIndexKeepsFilesystemUntouched) {
  TempDir dir;
  const fs::path removed = dir.root() / "workspace-remove";
  const fs::path kept = dir.root() / "workspace-keep";
  create_valid_workspace(removed);
  create_valid_workspace(kept);
  const fs::path removed_project = removed / "p-remove.project";
  const fs::path kept_project = kept / "p-keep.project";
  write_json(removed_project, {{"id", "p-remove"}, {"stacks", json::array()}});
  write_json(kept_project, {{"id", "p-keep"}, {"stacks", json::array()}});
  const fs::path removed_task = removed / "tasks" / "task-remove.task";
  write_json(removed_task, {{"id", "task-remove"}, {"title", "Keep the file"}});

  const ProjectSync::WorkspaceDbPaths paths{
      .project_db = dir.root() / "data" / "Project.json",
      .stack_db = dir.root() / "data" / "Stack.json",
      .timer_db = dir.root() / "data" / "Timers.json",
      .lock_file = dir.root() / "data" / ".stacks-tools.lock",
  };
  write_json(
      paths.project_db,
      {{"library_paths", json::array({removed.string(), kept.string()})},
       {"nodes",
        json::array({
            {{"id", "p-remove"},
             {"path", removed_project.string()},
             {"library_path", removed.string()}},
            {{"id", "p-keep"}, {"path", kept_project.string()}, {"library_path", kept.string()}},
        })}});
  write_json(paths.stack_db, {{"nodes", json::array({
                                            {{"id", "p-remove"},
                                             {"projectid", "p-remove"},
                                             {"project_path", removed_project.string()},
                                             {"library_path", removed.string()}},
                                            {{"id", "stack-remove"},
                                             {"projectid", "p-remove"},
                                             {"project_path", removed_project.string()},
                                             {"library_path", removed.string()}},
                                            {{"id", "p-keep"},
                                             {"projectid", "p-keep"},
                                             {"project_path", kept_project.string()},
                                             {"library_path", kept.string()}},
                                        })}});
  write_json(
      paths.timer_db,
      {{"timers",
        {{"task-remove", {{"task_file", removed_task.string()}, {"title", "remove"}}},
         {"task-keep",
          {{"task_file", (kept / "tasks" / "task-keep.task").string()}, {"title", "keep"}}}}}});

  const auto result = ProjectSync::remove_workspace_index(removed, paths);
  ASSERT_TRUE(result.has_value()) << ProjectSync::sync_error_message(result.error());
  EXPECT_EQ(result->library_paths_removed, 1U);
  EXPECT_EQ(result->project_nodes_removed, 1U);
  EXPECT_EQ(result->stack_nodes_removed, 2U);
  EXPECT_EQ(result->timers_removed, 1U);

  const json project = read_json(paths.project_db);
  ASSERT_EQ(project["library_paths"].size(), 1U);
  EXPECT_EQ(project["library_paths"][0], kept.string());
  ASSERT_EQ(project["nodes"].size(), 1U);
  EXPECT_EQ(project["nodes"][0]["id"], "p-keep");

  const json stack = read_json(paths.stack_db);
  ASSERT_EQ(stack["nodes"].size(), 1U);
  EXPECT_EQ(stack["nodes"][0]["id"], "p-keep");

  const json timers = read_json(paths.timer_db);
  ASSERT_EQ(timers["timers"].size(), 1U);
  EXPECT_TRUE(timers["timers"].contains("task-keep"));
  EXPECT_FALSE(timers["timers"].contains("task-remove"));

  EXPECT_TRUE(fs::exists(removed_project));
  EXPECT_TRUE(fs::exists(removed_task));
  EXPECT_TRUE(fs::exists(removed / "companies"));
  EXPECT_TRUE(fs::is_directory(removed / "timeboxes"));
}

/// 验证 Project 已写、Stack 写失败时恢复 Project 快照，不留下半移除索引。
TEST(Boundary, RemoveWorkspaceIndexRollsBackEarlierWrites) {
  TempDir dir;
  const fs::path workspace = dir.root() / "workspace";
  create_valid_workspace(workspace);
  const fs::path project_file = workspace / "p1.project";
  write_json(project_file, {{"id", "p1"}, {"stacks", json::array()}});
  const fs::path project_dir = dir.root() / "project-db";
  const fs::path stack_dir = dir.root() / "stack-db";
  const ProjectSync::WorkspaceDbPaths paths{
      .project_db = project_dir / "Project.json",
      .stack_db = stack_dir / "Stack.json",
      .timer_db = dir.root() / "timer-db" / "Timers.json",
      .lock_file = dir.root() / "data" / ".stacks-tools.lock",
  };
  const json project_before = {
      {"library_paths", json::array({workspace.string()})},
      {"nodes", json::array({{{"id", "p1"},
                              {"path", project_file.string()},
                              {"library_path", workspace.string()}}})},
      {"updateTime", "before"},
  };
  write_json(paths.project_db, project_before);
  write_json(paths.stack_db, {{"nodes", json::array({{{"id", "p1"},
                                                      {"projectid", "p1"},
                                                      {"project_path", project_file.string()},
                                                      {"library_path", workspace.string()}}})},
                              {"updateTime", "before"}});
  write_json(paths.timer_db, {{"timers", json::object()}});

  fs::permissions(stack_dir, fs::perms::owner_read | fs::perms::owner_exec,
                  fs::perm_options::replace);
  const auto result = ProjectSync::remove_workspace_index(workspace, paths);
  fs::permissions(stack_dir, fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec,
                  fs::perm_options::replace);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ProjectSync::SyncError::io_failure);
  EXPECT_EQ(read_json(paths.project_db), project_before);
  EXPECT_TRUE(fs::exists(project_file));
}

/// 验证工作区目录已被外部移除时，仍能清理陈旧索引，不要求路径先恢复存在。
TEST(Boundary, RemoveWorkspaceIndexAcceptsMissingRegisteredPath) {
  TempDir dir;
  const fs::path missing = dir.root() / "already-removed-workspace";
  const ProjectSync::WorkspaceDbPaths paths{
      .project_db = dir.root() / "data" / "Project.json",
      .stack_db = dir.root() / "data" / "Stack.json",
      .timer_db = dir.root() / "data" / "Timers.json",
      .lock_file = dir.root() / "data" / ".stacks-tools.lock",
  };
  write_json(paths.project_db,
             {{"library_paths", json::array({missing.string()})},
              {"nodes", json::array({{{"id", "p1"}, {"library_path", missing.string()}}})}});
  write_json(paths.stack_db,
             {{"nodes", json::array({{{"id", "s1"}, {"library_path", missing.string()}}})}});
  write_json(
      paths.timer_db,
      {{"timers", {{"task1", {{"task_file", (missing / "tasks" / "task1.task").string()}}}}}});

  const auto result = ProjectSync::remove_workspace_index(missing, paths);
  ASSERT_TRUE(result.has_value()) << ProjectSync::sync_error_message(result.error());
  EXPECT_FALSE(fs::exists(missing));
  EXPECT_TRUE(read_json(paths.project_db)["library_paths"].empty());
  EXPECT_TRUE(read_json(paths.project_db)["nodes"].empty());
  EXPECT_TRUE(read_json(paths.stack_db)["nodes"].empty());
  EXPECT_TRUE(read_json(paths.timer_db)["timers"].empty());
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

/// 验证标题命中永远排在描述命中之前，并支持有序子序列模糊搜索。
TEST(Boundary, TaskSearchRanksTitleBeforeDescriptionAndSupportsSubsequence) {
  Fixture fixture;
  fixture.create("s1", "Project needle handoff", "title hit");
  fixture.create("s1", "Unrelated heading", "description contains needle");

  auto tasks = TaskManager::list_tasks(fixture.databases,
                                       TaskManager::TaskListOptions{.target = std::nullopt,
                                                                    .query = "needle",
                                                                    .include_done = true,
                                                                    .include_archived = false});
  ASSERT_TRUE(tasks.has_value()) << TaskManager::task_error_message(tasks.error());
  ASSERT_EQ(tasks->size(), 2U);
  EXPECT_EQ(tasks->front().title, "Project needle handoff");
  EXPECT_EQ(tasks->back().title, "Unrelated heading");

  tasks = TaskManager::list_tasks(fixture.databases,
                                  TaskManager::TaskListOptions{.target = std::nullopt,
                                                               .query = "prjneedl",
                                                               .include_done = true,
                                                               .include_archived = false});
  ASSERT_TRUE(tasks.has_value());
  ASSERT_EQ(tasks->size(), 1U);
  EXPECT_EQ(tasks->front().title, "Project needle handoff");
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
  create_valid_workspace(library2);
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
  ASSERT_EQ(create_json["items"].size(), 6U);
  EXPECT_EQ(create_json["items"][0]["title"], "标题：UI task");
  EXPECT_EQ(create_json["items"][1]["title"], "描述：UI body");
  EXPECT_EQ(create_json["items"][5]["title"], "回车创建任务 / Create task");
  for (const auto& entry : create_json["items"]) {
    ASSERT_TRUE(entry.contains("arg"));
    EXPECT_TRUE(entry["arg"].get<std::string>().starts_with("create-preview:board:"));
    EXPECT_FALSE(entry.contains("autocomplete"));
    EXPECT_FALSE(entry.contains("uid"));
  }
  const auto action =
      WorkflowUI::action(context, create_json["items"][5]["arg"].get<std::string>());
  ASSERT_TRUE(action.has_value()) << action.error();
  EXPECT_NE(action->find("任务已创建"), std::string::npos);

  const auto tasks = TaskManager::list_tasks(fixture.databases);
  ASSERT_TRUE(tasks.has_value());
  ASSERT_EQ(tasks->size(), 1U);
  EXPECT_EQ(tasks->front().title, "UI task");
  EXPECT_EQ(tasks->front().priority, "high");
  EXPECT_EQ(tasks->front().start_date, "2026-10-30");
  EXPECT_EQ(tasks->front().due_date, "2026-11-01");

  const auto stale = WorkflowUI::action(context, "stale-action");
  ASSERT_FALSE(stale.has_value());
  EXPECT_NE(stale.error().find("stale"), std::string::npos);
}

/// 验证 `s cf` / `config` 直达路径页，短句柄不泄露完整路径，并可从确认页移除索引。
TEST(Boundary, WorkflowUiConfigUsesWorkspaceStatusAndRemovesIndexAfterConfirmation) {
  Fixture fixture;
  const WorkflowUI::Context context{
      .workflow_dir = fixture.dir.root(),
      .databases = fixture.databases,
      .inbox_library = fixture.library,
  };
  for (const std::string_view query : {"cf"}) {
    const json config = json::parse(WorkflowUI::filter(context, query));
    std::string removal_query;
    for (const auto& entry : config["items"]) {
      const std::string title = entry.value("title", "");
      EXPECT_NE(title, "添加新路径");
      EXPECT_NE(title, "重载全部路径");
      if (title.starts_with("路径｜")) {
        ASSERT_TRUE(entry.contains("autocomplete"));
        removal_query = entry["autocomplete"].get<std::string>();
        EXPECT_TRUE(removal_query.starts_with("remove-path:✅"));
        EXPECT_TRUE(removal_query.ends_with("✅ "));
        EXPECT_EQ(removal_query.find(fixture.library.string()), std::string::npos);
        EXPECT_EQ(entry.value("valid", true), false);
        EXPECT_FALSE(entry.contains("uid"));
      }
    }
    ASSERT_FALSE(removal_query.empty());
    EXPECT_TRUE(std::ranges::any_of(config["items"], [](const json& entry) {
      return entry.value("title", "") == "路径索引移除说明 / Path index removal";
    }));
    const json confirm = json::parse(WorkflowUI::filter(context, removal_query));
    std::string confirm_action;
    bool has_cancel = false;
    for (const auto& entry : confirm["items"]) {
      if (entry.contains("arg")) {
        const std::string action = entry["arg"].get<std::string>();
        if (action.starts_with("remove-path-confirm:")) {
          confirm_action = action;
        }
      }
      const std::string title = entry.value("title", "");
      has_cancel = has_cancel || title.starts_with("否，返回路径列表");
    }
    ASSERT_FALSE(confirm_action.empty());
    EXPECT_TRUE(has_cancel);
    const auto removed = WorkflowUI::action(context, confirm_action);
    ASSERT_TRUE(removed.has_value()) << removed.error();
    EXPECT_NE(removed->find("工作区文件未删除"), std::string::npos);
    break;
  }

  EXPECT_TRUE(read_json(fixture.project_db)["library_paths"].empty());
  EXPECT_TRUE(read_json(fixture.project_db)["nodes"].empty());
  EXPECT_TRUE(read_json(fixture.stack_db)["nodes"].empty());
  EXPECT_TRUE(fs::exists(fixture.project_file));
  EXPECT_TRUE(fs::exists(fixture.library / "companies"));
}

/// 验证 `s v` 目标页用一个输入框解析五字段，下方六行只负责回显和回车创建。
TEST(Boundary, WorkflowUiOneShotFiveFieldFormUsesDefaultsAndShortHandles) {
  Fixture fixture;
  const WorkflowUI::Context context{
      .workflow_dir = fixture.dir.root(),
      .databases = fixture.databases,
      .inbox_library = fixture.library,
  };
  const json boards = json::parse(WorkflowUI::filter(context, "board"));
  std::string stack_query;
  for (const auto& entry : boards["items"]) {
    const std::string title = entry.value("title", "");
    if (title.find("Stack One") != std::string::npos) {
      ASSERT_TRUE(entry.contains("autocomplete"));
      stack_query = entry["autocomplete"].get<std::string>();
      EXPECT_TRUE(stack_query.starts_with("board:✅"));
      EXPECT_TRUE(stack_query.ends_with("✅ "));
      EXPECT_EQ(stack_query.find("s1"), std::string::npos);
      EXPECT_EQ(entry.value("valid", true), false);
      EXPECT_FALSE(entry.contains("uid"));
      break;
    }
  }
  ASSERT_FALSE(stack_query.empty());
  const json page = json::parse(WorkflowUI::filter(
      context, std::format("{}Review release|Check docs \\| rollback|||", stack_query)));
  ASSERT_EQ(page["items"].size(), 6U);
  EXPECT_EQ(page["items"][0]["title"], "标题：Review release");
  EXPECT_EQ(page["items"][1]["title"], "描述：Check docs | rollback");
  EXPECT_EQ(page["items"][2]["title"],
            std::format("开始：{}", TaskManager::normalize_date("today").value()));
  EXPECT_EQ(page["items"][3]["title"], "截止：（未设置）");
  EXPECT_EQ(page["items"][4]["title"], "优先级：low");
  EXPECT_EQ(page["items"][2]["subtitle"],
            "Default: today · Allowed: today, tomorrow, +Nd, weekday, YYYY-MM-DD");
  EXPECT_EQ(page["items"][3]["subtitle"],
            "Default: unset · Allowed: today, tomorrow, +Nd, weekday, YYYY-MM-DD");
  EXPECT_EQ(page["items"][4]["subtitle"], "Default: low · Allowed: low, medium, high, critical");
  EXPECT_EQ(page["items"][5]["title"], "回车创建任务 / Create task");

  std::string create_action;
  for (const auto& entry : page["items"]) {
    ASSERT_TRUE(entry.contains("arg"));
    const std::string action = entry["arg"].get<std::string>();
    EXPECT_TRUE(action.starts_with("create-preview:board:"));
    EXPECT_FALSE(entry.contains("autocomplete"));
    EXPECT_FALSE(entry.contains("uid"));
    EXPECT_TRUE(entry.value("valid", false));
    if (create_action.empty()) {
      create_action = action;
    } else {
      EXPECT_EQ(action, create_action);
    }
  }
  ASSERT_FALSE(create_action.empty());
  const auto created = WorkflowUI::action(context, create_action);
  ASSERT_TRUE(created.has_value()) << created.error();
  EXPECT_NE(created->find("task:✅"), std::string::npos);
  EXPECT_FALSE(read_json(fixture.timer_db.parent_path() / "UiState.json").contains("draft"));

  const auto tasks = TaskManager::list_tasks(
      fixture.databases, TaskManager::TaskListOptions{.target = TaskManager::TargetId("s1"),
                                                      .query = "",
                                                      .include_done = true,
                                                      .include_archived = false});
  ASSERT_TRUE(tasks.has_value());
  ASSERT_EQ(tasks->size(), 1U);
  EXPECT_EQ(tasks->front().title, "Review release");
  EXPECT_EQ(tasks->front().description, "Check docs | rollback");
  EXPECT_EQ(tasks->front().start_date, TaskManager::normalize_date("today").value());
  EXPECT_EQ(tasks->front().priority, "low");

  const json refreshed_page = json::parse(WorkflowUI::filter(context, stack_query));
  bool short_task_handle = false;
  std::string task_query;
  for (const auto& entry : refreshed_page["items"]) {
    if (entry.contains("autocomplete")) {
      const std::string autocomplete = entry["autocomplete"].get<std::string>();
      if (autocomplete.starts_with("task:✅")) {
        EXPECT_TRUE(autocomplete.ends_with("✅ "));
        short_task_handle = true;
        task_query = autocomplete;
      }
    }
  }
  EXPECT_TRUE(short_task_handle);
  ASSERT_FALSE(task_query.empty());
  EXPECT_FALSE(json::parse(WorkflowUI::filter(context, task_query))["items"].empty());

  // 旧版完整 ID 仍能直接打开详情，避免升级后已有 Alfred token 失效。
  const json task_page =
      json::parse(WorkflowUI::filter(context, std::format("task:{}", tasks->front().id)));
  EXPECT_FALSE(task_page["items"].empty());
}

/// 验证 Inbox 路径可回车进入任务列表，输入五字段后切换为六行创建面板。
TEST(Boundary, WorkflowUiInboxOpensTasksAndUsesOneShotCreateForm) {
  Fixture fixture;
  const WorkflowUI::Context context{
      .workflow_dir = fixture.dir.root(),
      .databases = fixture.databases,
      .inbox_library = fixture.library,
  };
  const json inbox = json::parse(WorkflowUI::filter(context, "inbox"));
  std::string inbox_query;
  for (const auto& entry : inbox["items"]) {
    if (entry.contains("autocomplete")) {
      const std::string autocomplete = entry["autocomplete"].get<std::string>();
      if (autocomplete.starts_with("inbox:✅")) {
        inbox_query = autocomplete;
        EXPECT_TRUE(autocomplete.ends_with("✅ "));
        EXPECT_EQ(autocomplete.find(fixture.library.string()), std::string::npos);
        EXPECT_EQ(entry.value("valid", true), false);
        EXPECT_FALSE(entry.contains("uid"));
        break;
      }
    }
  }
  ASSERT_FALSE(inbox_query.empty());
  const auto existing = TaskManager::create_inbox_task(
      TaskManager::TaskInput{.title = TaskManager::TaskTitle("Existing Inbox task"),
                             .description = TaskManager::TaskDescription("visible"),
                             .start_date = std::nullopt,
                             .due_date = std::nullopt,
                             .priority = "low"},
      fixture.create_options);
  ASSERT_TRUE(existing.has_value()) << TaskManager::task_error_message(existing.error());

  const json task_page = json::parse(WorkflowUI::filter(context, inbox_query));
  bool has_task_handle = false;
  for (const auto& entry : task_page["items"]) {
    if (entry.contains("autocomplete")) {
      const std::string autocomplete = entry["autocomplete"].get<std::string>();
      has_task_handle = has_task_handle || autocomplete.starts_with("task:✅");
    }
  }
  EXPECT_TRUE(has_task_handle);
  EXPECT_FALSE(task_page["items"].empty());

  const json page = json::parse(
      WorkflowUI::filter(context, std::format("{}New Inbox|full description|||", inbox_query)));
  ASSERT_EQ(page["items"].size(), 6U);
  EXPECT_EQ(page["items"][0]["title"], "标题：New Inbox");
  EXPECT_EQ(page["items"][1]["title"], "描述：full description");
  EXPECT_EQ(page["items"][5]["title"], "回车创建任务 / Create task");
  EXPECT_TRUE(page["items"][5]["arg"].get<std::string>().starts_with("create-preview:inbox:"));
}

/// 验证短键尾部哨兵可由 Alfred 环境变量替换，删除哨兵后仍可继续输入。
TEST(Boundary, WorkflowUiHandleSentinelIsConfigurableAndOptional) {
  ScopedEnvironmentVariable sentinel("STACKS_HANDLE_SENTINEL", "::");
  Fixture fixture;
  const WorkflowUI::Context context{
      .workflow_dir = fixture.dir.root(),
      .databases = fixture.databases,
      .inbox_library = fixture.library,
  };
  const json boards = json::parse(WorkflowUI::filter(context, "board"));
  std::string board_query;
  for (const auto& entry : boards["items"]) {
    if (entry.value("title", "").find("Stack One") != std::string::npos) {
      board_query = entry["autocomplete"].get<std::string>();
      break;
    }
  }
  ASSERT_FALSE(board_query.empty());
  EXPECT_TRUE(board_query.ends_with(":: "));

  const json configured = json::parse(
      WorkflowUI::filter(context, std::format("{}Configured sentinel|||", board_query)));
  ASSERT_EQ(configured["items"].size(), 6U);
  EXPECT_EQ(configured["items"][0]["title"], "标题：Configured sentinel");

  const std::size_t sentinel_position = board_query.find(" ::");
  ASSERT_NE(sentinel_position, std::string::npos);
  const std::string without_sentinel = board_query.substr(0, sentinel_position);
  const json omitted =
      json::parse(WorkflowUI::filter(context, std::format("{} No sentinel|||", without_sentinel)));
  ASSERT_EQ(omitted["items"].size(), 6U);
  EXPECT_EQ(omitted["items"][0]["title"], "标题：No sentinel");
}

/// 验证完成/归档可见性默认关闭，环境变量开启后项目与 Inbox 均可检索。
TEST(Boundary, WorkflowUiCompletedArchivedVisibilitySwitchDefaultsOff) {
  ScopedEnvironmentVariable sentinel("STACKS_HANDLE_SENTINEL", "✅");
  Fixture fixture;
  const WorkflowUI::Context context{
      .workflow_dir = fixture.dir.root(),
      .databases = fixture.databases,
      .inbox_library = fixture.library,
  };
  fixture.create("s1", "open visible");
  const auto done = fixture.create("s1", "done hidden by default");
  ASSERT_TRUE(
      TaskManager::set_task_done(TaskManager::TaskId(done.task_id.value), true, fixture.databases)
          .has_value());
  const auto archived = fixture.create("s1", "archived hidden by default");
  ASSERT_TRUE(
      TaskManager::archive_task(TaskManager::TaskId(archived.task_id.value), fixture.databases)
          .has_value());
  const auto inbox_done = TaskManager::create_inbox_task(
      TaskManager::TaskInput{.title = TaskManager::TaskTitle("Inbox done hidden"),
                             .description = TaskManager::TaskDescription(""),
                             .start_date = std::nullopt,
                             .due_date = std::nullopt,
                             .priority = "low"},
      fixture.create_options);
  ASSERT_TRUE(inbox_done.has_value());
  ASSERT_TRUE(TaskManager::set_task_done(TaskManager::TaskId(inbox_done->task_id.value), true,
                                         fixture.databases)
                  .has_value());
  const auto inbox_open = TaskManager::create_inbox_task(
      TaskManager::TaskInput{.title = TaskManager::TaskTitle("Inbox open visible"),
                             .description = TaskManager::TaskDescription(""),
                             .start_date = std::nullopt,
                             .due_date = std::nullopt,
                             .priority = "low"},
      fixture.create_options);
  ASSERT_TRUE(inbox_open.has_value());

  const json boards = json::parse(WorkflowUI::filter(context, "board"));
  std::string board_query;
  for (const auto& entry : boards["items"]) {
    if (entry.value("title", "").find("Stack One") != std::string::npos) {
      board_query = entry["autocomplete"].get<std::string>();
      break;
    }
  }
  ASSERT_FALSE(board_query.empty());
  const json inbox = json::parse(WorkflowUI::filter(context, "inbox"));
  std::string inbox_query;
  for (const auto& entry : inbox["items"]) {
    if (entry.contains("autocomplete")) {
      const std::string autocomplete = entry["autocomplete"].get<std::string>();
      if (autocomplete.starts_with("inbox:✅")) {
        inbox_query = autocomplete;
        break;
      }
    }
  }
  ASSERT_FALSE(inbox_query.empty());

  const auto rendered_titles = [](const json& feedback) {
    std::set<std::string> titles;
    for (const auto& entry : feedback["items"]) {
      if (entry.contains("autocomplete") &&
          entry["autocomplete"].get<std::string>().starts_with("task:✅")) {
        titles.insert(entry.value("title", ""));
      }
    }
    return titles;
  };

  {
    ScopedEnvironmentVariable include_switch("STACKS_INCLUDE_DONE_ARCHIVED", "0");
    const auto board_titles =
        rendered_titles(json::parse(WorkflowUI::filter(context, board_query)));
    EXPECT_NE(board_titles.find("任务｜○ open visible"), board_titles.end());
    EXPECT_EQ(board_titles.find("任务｜✓ done hidden by default"), board_titles.end());
    EXPECT_EQ(board_titles.find("任务｜○ archived hidden by default"), board_titles.end());

    const auto inbox_titles =
        rendered_titles(json::parse(WorkflowUI::filter(context, inbox_query)));
    EXPECT_NE(inbox_titles.find("任务｜○ Inbox open visible"), inbox_titles.end());
    EXPECT_EQ(inbox_titles.find("任务｜✓ Inbox done hidden"), inbox_titles.end());
  }

  {
    ScopedEnvironmentVariable include_switch("STACKS_INCLUDE_DONE_ARCHIVED", "1");
    const auto board_titles =
        rendered_titles(json::parse(WorkflowUI::filter(context, board_query)));
    EXPECT_NE(board_titles.find("任务｜✓ done hidden by default"), board_titles.end());
    EXPECT_NE(board_titles.find("任务｜○ archived hidden by default"), board_titles.end());

    const auto inbox_titles =
        rendered_titles(json::parse(WorkflowUI::filter(context, inbox_query)));
    EXPECT_NE(inbox_titles.find("任务｜✓ Inbox done hidden"), inbox_titles.end());
  }
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

/// 验证新增/修改接口把接口名、入参与内部字段变化写入统一的空格分隔摘要。
TEST(Boundary, CoreInterfacesEmitStructuredChangeLogs) {
  TempDir dir;
  ASSERT_TRUE(interface_log::init(dir.root() / "interface.log").has_value());
  interface_log::clear();
  Fixture fixture;
  const auto status = ProjectSync::inspect_workspace(fixture.library);
  EXPECT_TRUE(status.structure_valid);
  const auto created = fixture.create("p1", "log title", "log body");
  TaskManager::TaskPatch patch;
  patch.title = "log title updated";
  patch.priority = "high";
  const auto updated = TaskManager::update_task(TaskManager::TaskId(created.task_id.value), patch,
                                                fixture.databases);
  ASSERT_TRUE(updated.has_value()) << TaskManager::task_error_message(updated.error());
  EXPECT_TRUE(ProjectSync::list_workspaces({fixture.project_db, fixture.stack_db}).has_value());

  const std::string logs =
      interface_log::recent() | std::views::join_with('\n') | std::ranges::to<std::string>();
  EXPECT_NE(logs.find("ProjectSync::inspect_workspace"), std::string::npos);
  EXPECT_NE(logs.find("ProjectSync::list_workspaces"), std::string::npos);
  EXPECT_NE(logs.find("TaskManager::create_task"), std::string::npos);
  EXPECT_NE(logs.find("TaskManager::update_task"), std::string::npos);
  EXPECT_NE(logs.find("changed=title,priority"), std::string::npos);
}
