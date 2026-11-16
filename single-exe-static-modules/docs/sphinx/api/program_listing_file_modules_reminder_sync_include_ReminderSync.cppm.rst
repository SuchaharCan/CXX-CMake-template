
.. _program_listing_file_modules_reminder_sync_include_ReminderSync.cppm:

Program Listing for File ReminderSync.cppm
==========================================

|exhale_lsh| :ref:`Return to documentation for file <file_modules_reminder_sync_include_ReminderSync.cppm>` (``modules/reminder_sync/include/ReminderSync.cppm``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   
   module;
   
   #include <fcntl.h>
   #include <spawn.h>
   #include <sys/wait.h>
   #include <unistd.h>
   
   #include <array>
   #include <cerrno>
   #include <cstdio>
   #include <cstdlib>
   #include <cstring>
   #include <string>
   #include <string_view>
   #include <vector>
   
   extern char** environ;
   
   export module ReminderSync;
   
   import std;
   import InterfaceLog;
   
   export namespace ReminderSync {
   
   // ---- 公共输入/输出模型 ----------------------------------------------------
   
   enum class Provider {
     reminders,  
     omnifocus,  
     both,       
   };
   
   struct TaskReminder {
     std::string task_id;      
     std::string title;        
     std::string description;  
     std::string due_date;     
     bool done = false;        
   };
   
   struct SyncOptions {
     bool dry_run = false;                                          
     std::string reminders_list = "Stacks";                         
     std::string omnifocus_bundle_id = "com.omnigroup.OmniFocus4";  
   };
   
   struct SyncResult {
     bool reminders_ok = false;     
     bool omnifocus_ok = false;     
     std::string reminders_output;  
     std::string omnifocus_output;  
     std::string marker;            
   };
   
   enum class SyncError {
     invalid_task_id,        
     due_date_required,      
     invalid_due_date,       
     osascript_unavailable,  
     reminders_failed,       
     omnifocus_failed,       
     process_failure,        
   };
   
   std::expected<SyncResult, SyncError> sync_task(const TaskReminder& task, Provider provider,
                                                  const SyncOptions& options = {});
   
   std::string build_reminders_script();
   
   std::string build_omnifocus_script();
   
   std::string provider_name(Provider provider) noexcept;
   
   std::string_view sync_error_message(SyncError error) noexcept;
   
   }  // namespace ReminderSync
   
   namespace ReminderSync {
   
   namespace {
   
   struct ProcessResult {
     int exit_code = -1;  
     std::string output;  
     std::string error;   
   };
   
   bool valid_task_id(std::string_view value) {
     return !value.empty() && std::ranges::all_of(value, [](char character) {
       return std::isalnum(static_cast<unsigned char>(character)) || character == '-' ||
              character == '_';
     });
   }
   
   std::optional<std::array<int, 3>> split_date(std::string_view value) {
     if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
       return std::nullopt;
     }
     try {
       const int year = std::stoi(std::string(value.substr(0, 4)));
       const int month = std::stoi(std::string(value.substr(5, 2)));
       const int day = std::stoi(std::string(value.substr(8, 2)));
       const std::chrono::year_month_day date{
           std::chrono::year{year},
           std::chrono::month{static_cast<unsigned>(month)},
           std::chrono::day{static_cast<unsigned>(day)},
       };
       if (!date.ok()) {
         return std::nullopt;
       }
       return std::array{year, month, day};
     } catch (const std::exception&) {
       return std::nullopt;
     }
   }
   
   std::string read_descriptor(int descriptor) {
     std::string result;
     std::array<char, 4096> buffer{};
     while (true) {
       const ssize_t count = ::read(descriptor, buffer.data(), buffer.size());
       if (count > 0) {
         result.append(buffer.data(), static_cast<std::size_t>(count));
       } else if (count == 0) {
         break;
       } else if (errno != EINTR) {
         break;
       }
     }
     return result;
   }
   
   std::expected<ProcessResult, SyncError> run_osascript(std::string_view script,
                                                         const std::vector<std::string>& arguments) {
     // 先显式检查路径，便于区分“系统没有 osascript”和普通脚本失败。
     if (!std::filesystem::exists("/usr/bin/osascript")) {
       return std::unexpected(SyncError::osascript_unavailable);
     }
   
     int stdout_pipe[2]{};
     int stderr_pipe[2]{};
     // 两个管道分别捕获应用结果与 AppleScript 诊断。
     if (::pipe(stdout_pipe) != 0 || ::pipe(stderr_pipe) != 0) {
       return std::unexpected(SyncError::process_failure);
     }
   
     posix_spawn_file_actions_t actions;
     posix_spawn_file_actions_init(&actions);
   
     // 子进程把管道写端映射到 stdout/stderr，并关闭所有不需要的管道端。
     posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);
     posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO);
     posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
     posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);
     posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);
     posix_spawn_file_actions_addclose(&actions, stderr_pipe[1]);
   
     std::vector<std::string> owned_arguments;
     owned_arguments.reserve(arguments.size() + 3);
   
     // `--` 后所有用户值都是独立 argv，不经过 shell word splitting。
     owned_arguments.emplace_back("/usr/bin/osascript");
     owned_arguments.emplace_back("-e");
     owned_arguments.emplace_back(script);
     owned_arguments.emplace_back("--");
     owned_arguments.insert(owned_arguments.end(), arguments.begin(), arguments.end());
     std::vector<char*> argv;
     argv.reserve(owned_arguments.size() + 1);
     for (auto& argument : owned_arguments) {
       argv.push_back(argument.data());
     }
     argv.push_back(nullptr);
   
     // 优先使用 posix_spawn，避免 fork 大量 std 状态和全局 logger。
     pid_t child = -1;
     const int spawn_result =
         ::posix_spawn(&child, "/usr/bin/osascript", &actions, nullptr, argv.data(), environ);
     posix_spawn_file_actions_destroy(&actions);
   
     // 父进程只保留读端，否则子进程写入永远不会收到 EOF。
     ::close(stdout_pipe[1]);
     ::close(stderr_pipe[1]);
     if (spawn_result != 0) {
       ::close(stdout_pipe[0]);
       ::close(stderr_pipe[0]);
       return std::unexpected(SyncError::process_failure);
     }
   
     ProcessResult result;
     // 顺序读取两个管道，适合个人任务同步的小输出量。
     result.output = read_descriptor(stdout_pipe[0]);
     result.error = read_descriptor(stderr_pipe[0]);
     ::close(stdout_pipe[0]);
     ::close(stderr_pipe[0]);
   
     int status = 0;
     // waitpid 可能被信号中断；EINTR 时继续等待同一个子进程。
     while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
     }
     if (WIFEXITED(status)) {
       result.exit_code = WEXITSTATUS(status);
     } else if (WIFSIGNALED(status)) {
       result.exit_code = 128 + WTERMSIG(status);
     }
     return result;
   }
   
   std::vector<std::string> common_arguments(const TaskReminder& task, std::string_view marker) {
     const auto date = split_date(task.due_date);
     if (!date) {
       return {};
     }
     return {
         task.title,
         task.description,
         marker.data(),
         std::to_string((*date)[0]),
         std::to_string((*date)[1]),
         std::to_string((*date)[2]),
         task.done ? "true" : "false",
     };
   }
   
   }  // namespace
   
   std::string build_reminders_script() {
     // 脚本只按 argv 下标取值；用户内容不插入脚本文本本身。
     return R"APPLESCRIPT(
   on run argv
     set taskName to item 1 of argv
     set taskBody to item 2 of argv
     set marker to item 3 of argv
     set dueYearText to item 4 of argv
     set dueMonthText to item 5 of argv
     set dueDayText to item 6 of argv
     set completedValue to (item 7 of argv) is "true"
     set listName to item 8 of argv
   
     set dueDate to date (dueYearText & "-" & dueMonthText & "-" & dueDayText)
     set time of dueDate to 86340
   
     tell application "Reminders"
       -- 目标列表可配置；不存在时创建一次。
       set targetList to missing value
       repeat with candidate in lists
         if name of candidate is listName then
           set targetList to candidate
           exit repeat
         end if
       end repeat
       if targetList is missing value then
         set targetList to make new list with properties {name:listName}
       end if
   
       -- 隐藏标记承担 upsert 主键；重复同步只更新，不新增副本。
       set matches to every reminder of targetList whose body contains marker
       if (count of matches) > 0 then
         set targetReminder to item 1 of matches
         set name of targetReminder to taskName
         set body of targetReminder to taskBody
         set due date of targetReminder to dueDate
       else
         set targetReminder to make new reminder at end of targetList with properties {name:taskName, body:taskBody, due date:dueDate}
       end if
       set completed of targetReminder to completedValue
       return "Reminders:" & (id of targetReminder)
     end tell
   end run
   )APPLESCRIPT";
   }
   
   std::string build_omnifocus_script() {
     // OmniFocus 在 default document 的 flattened tasks 中按 note 标记查找。
     return R"APPLESCRIPT(
   on run argv
     set taskName to item 1 of argv
     set taskBody to item 2 of argv
     set marker to item 3 of argv
     set dueYearText to item 4 of argv
     set dueMonthText to item 5 of argv
     set dueDayText to item 6 of argv
     set completedValue to (item 7 of argv) is "true"
     set dueDate to date (dueYearText & "-" & dueMonthText & "-" & dueDayText)
     set time of dueDate to 86340
   
     tell application "OmniFocus"
       set targetDocument to default document
       tell targetDocument
         -- 只更新 Inbox/项目中的现有任务；首次同步创建 Inbox task。
         set targetTask to missing value
         repeat with candidate in flattened tasks
           if (note of candidate contains marker) then
             set targetTask to candidate
             exit repeat
           end if
         end repeat
         if targetTask is missing value then
           set targetTask to make new inbox task with properties {name:taskName, note:taskBody, due date:dueDate}
         else
           set name of targetTask to taskName
           set note of targetTask to taskBody
           set due date of targetTask to dueDate
         end if
         if completedValue then
           mark complete targetTask
         else
           mark incomplete targetTask
         end if
         return "OmniFocus:" & (id of targetTask)
       end tell
     end tell
   end run
   )APPLESCRIPT";
   }
   
   std::expected<SyncResult, SyncError> sync_task(const TaskReminder& task, Provider provider,
                                                  const SyncOptions& options) {
     // 入参日志不含 description 全文，只保留排障所需的任务标识和同步模式。
     const std::string input =
         std::format("task_id={} title={} due={} provider={} dry_run={}", task.task_id, task.title,
                     task.due_date, provider_name(provider), options.dry_run);
     const auto operation = [&]() -> std::expected<SyncResult, SyncError> {
       // 在启动任何外部进程前完成全部领域校验。
       if (!valid_task_id(task.task_id)) {
         return std::unexpected(SyncError::invalid_task_id);
       }
       if (task.due_date.empty()) {
         return std::unexpected(SyncError::due_date_required);
       }
       if (!split_date(task.due_date)) {
         return std::unexpected(SyncError::invalid_due_date);
       }
   
       SyncResult result;
       // 去重标记写入描述末尾，保持在普通 Stacks/Markdown 文本中不可见干扰最小。
       result.marker = std::format("[Stacks-ID:{}]", task.task_id);
       const std::string body = std::format("{}\n\n{}", task.description, result.marker);
       TaskReminder script_task = task;
       script_task.description = body;
       const bool use_reminders = provider == Provider::reminders || provider == Provider::both;
       const bool use_omnifocus = provider == Provider::omnifocus || provider == Provider::both;
   
       if (options.dry_run) {
         // dry-run 不依赖 macOS 自动化权限，也不需要应用正在运行。
         result.reminders_ok = use_reminders;
         result.omnifocus_ok = use_omnifocus;
         result.reminders_output = use_reminders ? "dry-run: reminders script ready" : "";
         result.omnifocus_output = use_omnifocus ? "dry-run: OmniFocus script ready" : "";
         return result;
       }
   
       if (use_reminders) {
         // 第八个 argv 是目标列表名；脚本自身负责创建缺失列表。
         auto arguments = common_arguments(script_task, result.marker);
         if (arguments.empty()) {
           return std::unexpected(SyncError::invalid_due_date);
         }
         arguments.push_back(options.reminders_list);
         const auto process = run_osascript(build_reminders_script(), arguments);
         if (!process || process->exit_code != 0) {
           return std::unexpected(SyncError::reminders_failed);
         }
         result.reminders_ok = true;
         result.reminders_output = process->output;
       }
   
       if (use_omnifocus) {
         // OmniFocus 脚本当前通过应用名/默认文档操作；bundle 配置保留给扩展。
         auto arguments = common_arguments(script_task, result.marker);
         if (arguments.empty()) {
           return std::unexpected(SyncError::invalid_due_date);
         }
         arguments.push_back(options.omnifocus_bundle_id);
         const auto process = run_osascript(build_omnifocus_script(), arguments);
         if (!process || process->exit_code != 0) {
           return std::unexpected(SyncError::omnifocus_failed);
         }
         result.omnifocus_ok = true;
         result.omnifocus_output = process->output;
       }
       return result;
     }();
   
     // 成功只记录两个后端状态；失败记录稳定错误分类，不泄露 osascript stderr。
     const bool ok = operation.has_value();
     interface_log::record(
         interface_log::ApiName("ReminderSync::sync_task"), interface_log::LogText(input),
         interface_log::LogText(ok ? std::format("reminders={} omnifocus={}", operation->reminders_ok,
                                                 operation->omnifocus_ok)
                                   : std::string(sync_error_message(operation.error()))),
         ok ? interface_log::ApiStatus::ok : interface_log::ApiStatus::failed);
     return operation;
   }
   
   std::string provider_name(Provider provider) noexcept {
     // token 名称与 Alfred action 协议保持一致。
     switch (provider) {
       case Provider::reminders:
         return "reminders";
       case Provider::omnifocus:
         return "omnifocus";
       case Provider::both:
         return "both";
     }
     return "unknown";
   }
   
   std::string_view sync_error_message(SyncError error) noexcept {
     // UI 和 CLI 共用错误映射，避免两处文案漂移。
     switch (error) {
       case SyncError::invalid_task_id:
         return "task id is invalid";
       case SyncError::due_date_required:
         return "task has no due date";
       case SyncError::invalid_due_date:
         return "task due date is invalid";
       case SyncError::osascript_unavailable:
         return "osascript is unavailable";
       case SyncError::reminders_failed:
         return "Apple Reminders sync failed";
       case SyncError::omnifocus_failed:
         return "OmniFocus sync failed";
       case SyncError::process_failure:
         return "failed to launch osascript";
     }
     return "unknown reminder sync error";
   }
   
   }  // namespace ReminderSync
