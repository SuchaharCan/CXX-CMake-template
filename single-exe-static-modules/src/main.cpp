/**
 * @file main.cpp
 * @brief Stacks_Tools 单 main：只做单层 import 与 CLI 路由，不含业务逻辑。
 *
 * 设计依据（详见 docs/DESIGN_DOC.md）：
 * - 类型即注释：命令行参数第一时间转换成强类型（ProjectSync::DbPaths、
 *   TaskManager::ProjectId / TaskTitle / TaskDescription / CreateTaskOptions），
 *   业务模块内部不再出现裸 string 拼参。
 * - 错误分类：模块返回 std::expected，main 只负责把错误枚举映射为
 *   可读消息与退出码（0 成功 / 1 用法错误 / 2 操作失败）；
 *   只有"参数个数不足"这类用法错误直接打印 usage。
 * - 提前返回：每个子命令一个独立函数，失败立即 return，不写多层 if-else。
 * - 函数对象设计：list 命令通过 TaskManager::for_each_backlog_id 传入
 *   带状态 lambda（计数 + 打印），不引入 std::function。
 * - 无 UB：所有 argv 访问前都先做 argc 边界检查。
 */
import std;
import InterfaceLog;
import ProjectSync;
import TaskManager;

namespace {

namespace fs = std::filesystem;

/// CLI 退出码约定（显式枚举，避免魔法数字）。
enum class ExitCode : int {
  ok = 0,         ///< 命令成功
  usage = 1,      ///< 参数个数不足 / 未知命令
  op_failed = 2,  ///< 模块返回可恢复错误
};

/**
 * @brief 从起始目录向上查找包含 CMakeLists.txt 的项目根目录。
 * @param start_path 起始目录
 * @return 项目根目录；找不到时回退当前工作目录
 * @note 目录逐级回退一定终止：路径耗尽后直接返回 current_path。
 */
fs::path find_project_root(fs::path start_path) {
  fs::path path = fs::absolute(start_path);
  while (path.has_relative_path()) {
    if (fs::exists(path / "CMakeLists.txt")) {
      return path;
    }
    path = path.parent_path();
  }
  return fs::current_path();
}

/// 打印 CLI 用法。
void print_usage() {
  std::println("Usage:");
  std::println("  Stacks_Tools sync <search_path>");
  std::println("  Stacks_Tools add <project_or_stack_id> \"title|description\"");
  std::println("  Stacks_Tools list <project_file>");
}

/**
 * @brief sync 子命令：扫描库目录，重建项目/Stack 索引。
 * @return ExitCode
 */
int run_sync(const fs::path& search_path, const fs::path& root_path) {
  const ProjectSync::DbPaths db_paths{
      root_path / "data" / "Project.json",
      root_path / "data" / "Stack.json",
  };
  const auto result = ProjectSync::sync_all(search_path, db_paths);
  if (!result) {
    std::println(std::cerr, "sync failed: {}", ProjectSync::sync_error_message(result.error()));
    return static_cast<int>(ExitCode::op_failed);
  }
  std::println("sync ok: projects={} stacks={}", result->project_count, result->stack_count);
  return static_cast<int>(ExitCode::ok);
}

/**
 * @brief add 子命令：为目标（Project/Stack）创建任务。
 * @param raw_input "title" 或 "title|description"
 * @return ExitCode
 */
int run_add(const fs::path& root_path, const std::string& target_id, const std::string& raw_input) {
  const auto separator = raw_input.find('|');
  const TaskManager::TaskTitle title(
      separator == std::string::npos ? raw_input : raw_input.substr(0, separator));
  const TaskManager::TaskDescription description(
      separator == std::string::npos ? std::string{} : raw_input.substr(separator + 1));
  const TaskManager::CreateTaskOptions options{
      root_path / "data" / "Project.json",
      root_path / "data" / "Stack.json",
  };
  const auto result =
      TaskManager::create_task(TaskManager::ProjectId(target_id), title, description, options);
  if (!result) {
    std::println(std::cerr, "task failed: {}", TaskManager::task_error_message(result.error()));
    return static_cast<int>(ExitCode::op_failed);
  }
  std::println("task created: id={} target={} file={}", result->task_id.value, result->target_kind,
               result->task_file.string());
  return static_cast<int>(ExitCode::ok);
}

/**
 * @brief list 子命令：打印项目文件 backlog 中的任务 ID。
 * @return ExitCode
 * @note 通过函数对象 lambda 遍历（计数 + 打印），体现函数式设计。
 */
int run_list(const fs::path& project_file) {
  std::size_t count = 0;
  const auto visited =
      TaskManager::for_each_backlog_id(project_file, [&count](std::string_view task_id) {
        std::println("  - {}", task_id);
        ++count;
      });
  if (!visited) {
    std::println(std::cerr, "list failed: {}", TaskManager::task_error_message(visited.error()));
    return static_cast<int>(ExitCode::op_failed);
  }
  std::println("backlog tasks: {}", count);
  return static_cast<int>(ExitCode::ok);
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    print_usage();
    return static_cast<int>(ExitCode::usage);
  }

  const fs::path root_path = find_project_root(fs::current_path());

  // bqlog 初始化失败属可恢复错误：内存环形缓冲仍可用，仅告警不退出。
  const auto log_init = interface_log::init(root_path / "log" / "interface.log");
  if (!log_init) {
    std::println(std::cerr, "warning: {}; in-memory ring log remains active",
                 interface_log::log_error_message(log_init.error()));
  }

  const std::string command = argv[1];
  // 同步命令，手动获取最新的 记录
  if (command == "sync") {
    if (argc < 3) {
      std::println(std::cerr, "Error: 'sync' requires <search_path>.");
      return static_cast<int>(ExitCode::usage);
    }
    return run_sync(argv[2], root_path);
  }
  // 向 指定看板中添加任务命令
  if (command == "add") {
    if (argc < 4) {
      std::println(std::cerr,
                   "Error: 'add' requires <project_or_stack_id> and \"title|description\".");
      return static_cast<int>(ExitCode::usage);
    }
    return run_add(root_path, argv[2], argv[3]);
  }
  // 获取 所有的 看板
  if (command == "list") {
    if (argc < 3) {
      std::println(std::cerr, "Error: 'list' requires <project_file>.");
      return static_cast<int>(ExitCode::usage);
    }
    return run_list(argv[2]);
  }

  std::println(std::cerr, "Unknown command: {}", command);
  return static_cast<int>(ExitCode::usage);
}
