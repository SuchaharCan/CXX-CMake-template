
.. _program_listing_file_modules_project_sync_include_ProjectSync.cppm:

Program Listing for File ProjectSync.cppm
=========================================

|exhale_lsh| :ref:`Return to documentation for file <file_modules_project_sync_include_ProjectSync.cppm>` (``modules/project_sync/include/ProjectSync.cppm``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   
   module;
   
   #include <fcntl.h>
   #include <sys/file.h>
   #include <unistd.h>
   
   #include <array>
   #include <chrono>
   #include <cstdint>
   #include <ctime>
   #include <filesystem>
   #include <fstream>
   #include <nlohmann/json.hpp>
   #include <set>
   #include <string>
   #include <string_view>
   #include <system_error>
   #include <vector>
   
   export module ProjectSync;
   
   import std;
   import InterfaceLog;
   
   export namespace ProjectSync {
   
   // ---- 公共数据契约 ---------------------------------------------------------
   
   struct DbPaths {
     std::filesystem::path project_db{"data/Project.json"};  
     std::filesystem::path stack_db{"data/Stack.json"};      
   };
   
   struct WorkspaceDbPaths {
     std::filesystem::path project_db{"data/Project.json"};       
     std::filesystem::path stack_db{"data/Stack.json"};           
     std::filesystem::path timer_db{"data/Timers.json"};          
     std::filesystem::path lock_file{"data/.stacks-tools.lock"};  
   };
   
   struct WorkspaceStatus {
     std::filesystem::path path;                
     bool exists = false;                       
     bool is_directory = false;                 
     bool structure_valid = false;              
     bool has_projects = false;                 
     std::size_t project_file_count = 0;        
     std::vector<std::string> missing_entries;  
   };
   
   struct WorkspaceRemoveResult {
     std::filesystem::path path;             
     std::size_t library_paths_removed = 0;  
     std::size_t project_nodes_removed = 0;  
     std::size_t stack_nodes_removed = 0;    
     std::size_t timers_removed = 0;         
   };
   
   enum class SyncError {
     search_path_missing,        
     search_path_not_directory,  
     workspace_invalid,          
     workspace_not_registered,   
     project_db_unreadable,      
     stack_db_unreadable,        
     timer_db_unreadable,        
     project_file_unreadable,    
     lock_failure,               
     io_failure,                 
   };
   
   struct SyncSummary {
     std::size_t project_count = 0;                         
     std::size_t stack_count = 0;                           
     std::vector<std::filesystem::path> library_paths;      
     std::vector<std::filesystem::path> invalid_libraries;  
     std::vector<std::filesystem::path> failed_projects;    
   };
   
   WorkspaceStatus inspect_workspace(const std::filesystem::path& path);
   
   std::expected<std::vector<WorkspaceStatus>, SyncError> list_workspaces(const DbPaths& paths);
   
   std::expected<WorkspaceRemoveResult, SyncError> remove_workspace_index(
       const std::filesystem::path& path, const WorkspaceDbPaths& paths = {});
   
   std::expected<SyncSummary, SyncError> sync_all(const std::filesystem::path& search_path,
                                                  const DbPaths& paths = {});
   
   template <std::invocable<const std::filesystem::directory_entry&> Visitor>
   void for_each_project_file(const std::filesystem::path& directory, Visitor&& visitor) {
     if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
       return;
     }
   
     std::error_code error;
     std::filesystem::recursive_directory_iterator iterator(
         directory, std::filesystem::directory_options::skip_permission_denied, error);
     const std::filesystem::recursive_directory_iterator end;
     while (!error && iterator != end) {
       if (iterator->is_regular_file(error) && iterator->path().extension() == ".project") {
         std::invoke(visitor, *iterator);
       }
       iterator.increment(error);
     }
   }
   
   std::string_view sync_error_message(SyncError error) noexcept;
   
   std::string format_file_time(std::filesystem::file_time_type file_time);
   
   }  // namespace ProjectSync
   
   namespace ProjectSync {
   
   namespace {
   
   namespace fs = std::filesystem;
   using json = nlohmann::json;
   
   std::string now_timestamp() {
     // Project/Stack 索引的 updateTime 使用本地秒级时间，和 Stacks 文本界面一致。
     return std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::system_clock::now());
   }
   
   fs::path normalized_path(const fs::path& value) {
     std::error_code error;
     const fs::path canonical = fs::weakly_canonical(value, error);
     if (!error) {
       return canonical;
     }
     return fs::absolute(value, error).lexically_normal();
   }
   
   std::expected<std::set<std::string>, SyncError> load_library_paths(const fs::path& project_db) {
     std::set<std::string> result;
     if (!fs::exists(project_db)) {
       return result;
     }
   
     std::ifstream input(project_db);
     try {
       json db;
       if (!(input >> db) || !db.is_object()) {
         return std::unexpected(SyncError::project_db_unreadable);
       }
       if (db.contains("library_paths") && db["library_paths"].is_array()) {
         for (const auto& item : db["library_paths"]) {
           if (item.is_string() && !item.get<std::string>().empty()) {
             result.insert(normalized_path(item.get<std::string>()).string());
           }
         }
       }
     } catch (const std::exception&) {
       return std::unexpected(SyncError::project_db_unreadable);
     }
     return result;
   }
   
   bool atomic_write_json(const fs::path& target, const json& value) {
     std::error_code error;
     if (target.has_parent_path()) {
       fs::create_directories(target.parent_path(), error);
       if (error) {
         return false;
       }
     }
   
     const fs::path temporary =
         target.parent_path() /
         std::format(".{}.tmp.{}.{}", target.filename().string(), ::getpid(),
                     std::chrono::steady_clock::now().time_since_epoch().count());
     {
       // 关闭 ofstream 后再 rename；保证目标文件看到的是完整 JSON。
       std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
       if (!output) {
         return false;
       }
       output << value.dump(4) << '\n';
       output.flush();
       if (!output) {
         fs::remove(temporary, error);
         return false;
       }
     }
   
     fs::rename(temporary, target, error);
     if (error) {
       fs::remove(temporary, error);
       return false;
     }
     return true;
   }
   
   std::string safe_string(const json& object, std::string_view key, std::string fallback = {}) {
     if (object.is_object() && object.contains(key) && object[key].is_string()) {
       return object[key].get<std::string>();
     }
     return fallback;
   }
   
   std::string missing_entries_summary(const std::vector<std::string>& entries) {
     std::string result;
     for (const auto& entry : entries) {
       if (!result.empty()) {
         result += ',';
       }
       result += entry;
     }
     return result;
   }
   
   struct RequiredWorkspaceEntry {
     std::string_view name;
     bool directory = false;
   };
   
   inline constexpr std::array<RequiredWorkspaceEntry, 5> kRequiredWorkspaceEntries{{
       {"companies", false},
       {"documents.tree", false},
       {"people", false},
       {"tasks", true},
       {"timeboxes", true},
   }};
   
   bool path_is_within(const fs::path& child, const fs::path& parent) {
     const fs::path normalized_child = normalized_path(child);
     const fs::path normalized_parent = normalized_path(parent);
     const fs::path relative = normalized_child.lexically_relative(normalized_parent);
     return !relative.empty() && relative != "." && *relative.begin() != "..";
   }
   
   class ScopedFileLock {
    public:
     explicit ScopedFileLock(const fs::path& path) {
       std::error_code error;
       fs::create_directories(path.parent_path(), error);
       if (error) {
         return;
       }
       descriptor_ = ::open(path.c_str(), O_CREAT | O_RDWR, 0600);
       if (descriptor_ < 0) {
         return;
       }
       if (::flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
         ::close(descriptor_);
         descriptor_ = -1;
       }
     }
   
     ~ScopedFileLock() {
       if (descriptor_ >= 0) {
         ::flock(descriptor_, LOCK_UN);
         ::close(descriptor_);
       }
     }
   
     ScopedFileLock(const ScopedFileLock&) = delete;
     ScopedFileLock& operator=(const ScopedFileLock&) = delete;
   
     [[nodiscard]] bool ok() const noexcept { return descriptor_ >= 0; }
   
    private:
     int descriptor_ = -1;
   };
   
   }  // namespace
   
   std::string format_file_time(fs::file_time_type file_time) {
     // file_clock 的纪元没有跨平台保证，通过“当前时钟差值”平移到 system_clock。
     const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
         file_time - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
     const std::time_t seconds = std::chrono::system_clock::to_time_t(system_time);
   
     // Homebrew libc++ 可能没有 tzdb；localtime_r + strftime 是当前平台的稳定回退。
     std::tm local{};
     if (localtime_r(&seconds, &local) == nullptr) {
       return {};
     }
     char buffer[32]{};
     if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local) == 0) {
       return {};
     }
     return buffer;
   }
   
   WorkspaceStatus inspect_workspace(const fs::path& path) {
     WorkspaceStatus status;
     status.path = normalized_path(path);
     const auto finish = [&] {
       interface_log::record(
           interface_log::ApiName("ProjectSync::inspect_workspace"),
           interface_log::LogText(std::format("path={}", status.path.string())),
           interface_log::LogText(std::format("exists={} directory={} valid={} projects={} missing={}",
                                              status.exists, status.is_directory,
                                              status.structure_valid, status.project_file_count,
                                              missing_entries_summary(status.missing_entries))),
           interface_log::ApiStatus::ok);
       return status;
     };
   
     std::error_code error;
     status.exists = fs::exists(status.path, error);
     if (error || !status.exists) {
       return finish();
     }
     status.is_directory = fs::is_directory(status.path, error);
     if (error || !status.is_directory) {
       return finish();
     }
   
     for (const auto& required : kRequiredWorkspaceEntries) {
       const fs::path candidate = status.path / required.name;
       const bool type_ok = required.directory ? fs::is_directory(candidate, error)
                                               : fs::is_regular_file(candidate, error);
       if (error || !type_ok) {
         status.missing_entries.emplace_back(required.name);
         error.clear();
       }
     }
     status.structure_valid = status.missing_entries.empty();
   
     for_each_project_file(status.path,
                           [&](const fs::directory_entry&) { ++status.project_file_count; });
     status.has_projects = status.project_file_count > 0;
     return finish();
   }
   
   std::expected<std::vector<WorkspaceStatus>, SyncError> list_workspaces(const DbPaths& paths) {
     const auto library_paths = load_library_paths(paths.project_db);
     if (!library_paths) {
       interface_log::record(
           interface_log::ApiName("ProjectSync::list_workspaces"),
           interface_log::LogText(std::format("project_db={}", paths.project_db.string())),
           interface_log::LogText(std::string(sync_error_message(library_paths.error()))),
           interface_log::ApiStatus::failed);
       return std::unexpected(library_paths.error());
     }
   
     std::vector<WorkspaceStatus> result;
     for (const auto& value : *library_paths) {
       result.push_back(inspect_workspace(value));
     }
     const auto valid_count = std::ranges::count_if(
         result, [](const WorkspaceStatus& item) { return item.structure_valid; });
     interface_log::record(
         interface_log::ApiName("ProjectSync::list_workspaces"),
         interface_log::LogText(std::format("project_db={}", paths.project_db.string())),
         interface_log::LogText(std::format("total={} valid={} invalid={}", result.size(), valid_count,
                                            result.size() - static_cast<std::size_t>(valid_count))),
         interface_log::ApiStatus::ok);
     return result;
   }
   
   std::expected<WorkspaceRemoveResult, SyncError> remove_workspace_index(
       const fs::path& path, const WorkspaceDbPaths& paths) {
     const fs::path target = normalized_path(path);
     const auto fail = [&](SyncError error) -> std::expected<WorkspaceRemoveResult, SyncError> {
       interface_log::record(interface_log::ApiName("ProjectSync::remove_workspace_index"),
                             interface_log::LogText(std::format("path={}", target.string())),
                             interface_log::LogText(std::string(sync_error_message(error))),
                             interface_log::ApiStatus::failed);
       return std::unexpected(error);
     };
   
     ScopedFileLock lock(paths.lock_file);
     if (!lock.ok()) {
       return fail(SyncError::lock_failure);
     }
   
     if (!fs::exists(paths.project_db)) {
       return fail(SyncError::workspace_not_registered);
     }
     const auto project_loaded = [&]() -> std::expected<json, SyncError> {
       std::ifstream input(paths.project_db, std::ios::binary);
       try {
         json value;
         if (!(input >> value) || !value.is_object()) {
           return std::unexpected(SyncError::project_db_unreadable);
         }
         return value;
       } catch (const std::exception&) {
         return std::unexpected(SyncError::project_db_unreadable);
       }
     }();
     if (!project_loaded) {
       return fail(project_loaded.error());
     }
   
     json project_before = *project_loaded;
     json project_after = project_before;
     WorkspaceRemoveResult result{.path = target};
   
     if (project_after.contains("library_paths") && project_after["library_paths"].is_array()) {
       auto& entries = project_after["library_paths"];
       const auto old_size = entries.size();
       entries.erase(std::remove_if(entries.begin(), entries.end(),
                                    [&](const json& item) {
                                      return item.is_string() &&
                                             normalized_path(item.get<std::string>()) == target;
                                    }),
                     entries.end());
       result.library_paths_removed = old_size - entries.size();
     }
     if (project_after.contains("nodes") && project_after["nodes"].is_array()) {
       auto& nodes = project_after["nodes"];
       const auto old_size = nodes.size();
       nodes.erase(
           std::remove_if(nodes.begin(), nodes.end(),
                          [&](const json& node) {
                            const std::string library = safe_string(node, "library_path", "");
                            const std::string project_path = safe_string(node, "path", "");
                            return (!library.empty() && normalized_path(library) == target) ||
                                   (!project_path.empty() && path_is_within(project_path, target));
                          }),
           nodes.end());
       result.project_nodes_removed = old_size - nodes.size();
     }
     const bool project_changed = result.library_paths_removed > 0 || result.project_nodes_removed > 0;
     if (project_changed) {
       project_after["updateTime"] = now_timestamp();
     }
   
     json stack_after = json::object();
     json stack_before = json::object();
     bool stack_changed = false;
     if (fs::exists(paths.stack_db)) {
       std::ifstream input(paths.stack_db, std::ios::binary);
       try {
         if (!(input >> stack_before) || !stack_before.is_object()) {
           return fail(SyncError::stack_db_unreadable);
         }
       } catch (const std::exception&) {
         return fail(SyncError::stack_db_unreadable);
       }
       stack_after = stack_before;
       if (stack_after.contains("nodes") && stack_after["nodes"].is_array()) {
         auto& nodes = stack_after["nodes"];
         const auto old_size = nodes.size();
         nodes.erase(
             std::remove_if(nodes.begin(), nodes.end(),
                            [&](const json& node) {
                              const std::string library = safe_string(node, "library_path", "");
                              const std::string project_path = safe_string(node, "project_path", "");
                              return (!library.empty() && normalized_path(library) == target) ||
                                     (!project_path.empty() && path_is_within(project_path, target));
                            }),
             nodes.end());
         result.stack_nodes_removed = old_size - nodes.size();
         stack_changed = result.stack_nodes_removed > 0;
         if (stack_changed) {
           stack_after["updateTime"] = now_timestamp();
         }
       }
     }
   
     json timers_after = json::object();
     json timers_before = json::object();
     bool timers_changed = false;
     std::set<std::string> workspace_task_ids;
     const fs::path task_directory = target / "tasks";
     std::error_code error;
     if (fs::is_directory(task_directory, error)) {
       for (const auto& entry : fs::directory_iterator(task_directory, error)) {
         if (!error && entry.is_regular_file(error) && entry.path().extension() == ".task") {
           workspace_task_ids.insert(entry.path().stem().string());
         }
       }
     }
     if (fs::exists(paths.timer_db)) {
       std::ifstream input(paths.timer_db, std::ios::binary);
       try {
         if (!(input >> timers_before) || !timers_before.is_object()) {
           return fail(SyncError::timer_db_unreadable);
         }
       } catch (const std::exception&) {
         return fail(SyncError::timer_db_unreadable);
       }
       timers_after = timers_before;
       if (timers_after.contains("timers") && timers_after["timers"].is_object()) {
         auto& timers = timers_after["timers"];
         for (auto iterator = timers.begin(); iterator != timers.end();) {
           const std::string task_file = safe_string(iterator.value(), "task_file", "");
           const bool path_match = !task_file.empty() && path_is_within(task_file, target);
           const bool id_match = workspace_task_ids.contains(iterator.key());
           if (path_match || id_match) {
             iterator = timers.erase(iterator);
             ++result.timers_removed;
           } else {
             ++iterator;
           }
         }
         timers_changed = result.timers_removed > 0;
       }
     }
   
     if (result.library_paths_removed == 0 && result.project_nodes_removed == 0 &&
         result.stack_nodes_removed == 0 && result.timers_removed == 0) {
       return fail(SyncError::workspace_not_registered);
     }
   
     struct PendingWrite {
       fs::path path;
       const json* before = nullptr;
       const json* after = nullptr;
     };
     std::vector<PendingWrite> writes;
     if (project_changed) {
       writes.push_back({paths.project_db, &project_before, &project_after});
     }
     if (stack_changed) {
       writes.push_back({paths.stack_db, &stack_before, &stack_after});
     }
     if (timers_changed) {
       writes.push_back({paths.timer_db, &timers_before, &timers_after});
     }
   
     std::vector<std::size_t> written;
     for (std::size_t index = 0; index < writes.size(); ++index) {
       if (!atomic_write_json(writes[index].path, *writes[index].after)) {
         for (auto iterator = written.rbegin(); iterator != written.rend(); ++iterator) {
           atomic_write_json(writes[*iterator].path, *writes[*iterator].before);
         }
         return fail(SyncError::io_failure);
       }
       written.push_back(index);
     }
   
     interface_log::record(
         interface_log::ApiName("ProjectSync::remove_workspace_index"),
         interface_log::LogText(std::format("path={}", target.string())),
         interface_log::LogText(std::format(
             "changes=paths:{} project_nodes:{} stack_nodes:{} timers:{} filesystem=untouched",
             result.library_paths_removed, result.project_nodes_removed, result.stack_nodes_removed,
             result.timers_removed)),
         interface_log::ApiStatus::ok);
     return result;
   }
   
   std::expected<SyncSummary, SyncError> sync_all(const fs::path& search_path, const DbPaths& paths) {
     // 接口日志先保存业务入参，输出在 operation 完成后统一记录。
     const std::string input =
         std::format("search_path={} project_db={} stack_db={}", search_path.string(),
                     paths.project_db.string(), paths.stack_db.string());
     SyncSummary summary;
     std::optional<SyncError> first_error;
   
     // 整段同步收进立即调用 lambda：每个错误分支直接 return，避免深嵌套。
     const auto operation = [&]() -> std::expected<std::monostate, SyncError> {
       // 阶段 1：恢复历史路径，并校验/追加本次新路径。
       auto library_paths = load_library_paths(paths.project_db);
       if (!library_paths) {
         return std::unexpected(library_paths.error());
       }
       if (!search_path.empty()) {
         if (!fs::exists(search_path)) {
           return std::unexpected(SyncError::search_path_missing);
         }
         if (!fs::is_directory(search_path)) {
           return std::unexpected(SyncError::search_path_not_directory);
         }
         const WorkspaceStatus workspace = inspect_workspace(search_path);
         if (!workspace.structure_valid) {
           summary.invalid_libraries.push_back(workspace.path);
           return std::unexpected(SyncError::workspace_invalid);
         }
         library_paths->insert(workspace.path.string());
       }
   
       // 阶段 2：遍历工作区，生成 Project 索引与扁平 Project/Stack 索引。
       json project_nodes = json::array();
       json stack_nodes = json::array();
       std::set<std::string> project_ids;
   
       for (const auto& library_path_string : *library_paths) {
         const fs::path library_path(library_path_string);
         const WorkspaceStatus workspace = inspect_workspace(library_path);
         if (!workspace.structure_valid) {
           summary.invalid_libraries.push_back(workspace.path);
           continue;
         }
         summary.library_paths.push_back(workspace.path);
   
         // 先收集后排序，消除 recursive_directory_iterator 的平台相关顺序。
         std::vector<fs::directory_entry> project_files;
         try {
           for_each_project_file(library_path, [&](const fs::directory_entry& entry) {
             project_files.push_back(entry);
           });
         } catch (const std::exception&) {
           if (!first_error) {
             first_error = SyncError::io_failure;
           }
           continue;
         }
         std::ranges::sort(project_files, {},
                           [](const auto& entry) { return entry.path().lexically_normal().string(); });
   
         for (const auto& entry : project_files) {
           try {
             // 单个项目损坏时记录路径并继续，不牺牲同一工作区的其他项目。
             std::ifstream project_file(entry.path(), std::ios::binary);
             json content;
             if (!(project_file >> content) || !content.is_object()) {
               summary.failed_projects.push_back(entry.path());
               if (!first_error) {
                 first_error = SyncError::project_file_unreadable;
               }
               continue;
             }
   
             const std::string project_id = safe_string(content, "id");
             // 空 ID/重复 ID 不能进入索引，否则 Alfred action token 无法稳定寻址。
             if (project_id.empty() || !project_ids.insert(project_id).second) {
               summary.failed_projects.push_back(entry.path());
               if (!first_error) {
                 first_error = SyncError::project_file_unreadable;
               }
               continue;
             }
             const std::string project_title = safe_string(content, "title", "Untitled");
             const std::string project_path = normalized_path(entry.path()).string();
             const std::string modified = format_file_time(entry.last_write_time());
   
             // 项目自身同时出现在 Project 与 Stack 索引，允许项目级直接创建任务。
             project_nodes.push_back({
                 {"id", project_id},
                 {"title", project_title},
                 {"path", project_path},
                 {"library_path", library_path.string()},
                 {"mtime", modified},
             });
             stack_nodes.push_back({
                 {"id", project_id},
                 {"projectid", project_id},
                 {"title", project_title},
                 {"kind", "project"},
                 {"project_path", project_path},
                 {"library_path", library_path.string()},
             });
   
             if (!content.contains("stacks") || !content["stacks"].is_array()) {
               continue;
             }
             // 清单节点继承 projectid，WorkflowUI 可据此显示“项目 → 清单”三级路径。
             for (const auto& stack : content["stacks"]) {
               if (!stack.is_object()) {
                 continue;
               }
               const std::string stack_id = safe_string(stack, "id");
               if (stack_id.empty()) {
                 continue;
               }
               stack_nodes.push_back({
                   {"id", stack_id},
                   {"projectid", project_id},
                   {"title", safe_string(stack, "title", "Untitled")},
                   {"kind", "stack"},
                   {"project_path", project_path},
                   {"library_path", library_path.string()},
               });
             }
           } catch (const std::exception&) {
             summary.failed_projects.push_back(entry.path());
             if (!first_error) {
               first_error = SyncError::project_file_unreadable;
             }
           }
         }
       }
   
       summary.project_count = project_nodes.size();
       summary.stack_count = stack_nodes.size();
   
       // 阶段 3：先完整构造两个 JSON，再分别做原子替换。
       const json project_db = {
           {"library_paths", json(*library_paths)},
           {"nodes", std::move(project_nodes)},
           {"updateTime", now_timestamp()},
       };
       const json stack_db = {
           {"nodes", std::move(stack_nodes)},
           {"updateTime", now_timestamp()},
       };
       if (!atomic_write_json(paths.project_db, project_db) ||
           !atomic_write_json(paths.stack_db, stack_db)) {
         return std::unexpected(SyncError::io_failure);
       }
       // 部分项目损坏仍完成索引刷新，但调用方会收到明确的分类错误。
       if (first_error) {
         return std::unexpected(*first_error);
       }
       return {};
     }();
   
     // 所有可恢复错误都通过 expected 返回；日志记录稳定的摘要而非异常栈。
     const bool ok = operation.has_value();
     const std::string output = std::format(
         "ok={} projects={} stacks={} valid_libs={} invalid_libs={} failed_projects={} error={}", ok,
         summary.project_count, summary.stack_count, summary.library_paths.size(),
         summary.invalid_libraries.size(), summary.failed_projects.size(),
         ok ? std::string{} : sync_error_message(operation.error()));
     interface_log::record(interface_log::ApiName("ProjectSync::sync_all"),
                           interface_log::LogText(input), interface_log::LogText(output),
                           ok ? interface_log::ApiStatus::ok : interface_log::ApiStatus::failed);
     if (!operation) {
       return std::unexpected(operation.error());
     }
     return summary;
   }
   
   std::string_view sync_error_message(SyncError error) noexcept {
     // 错误文案是 Alfred/Debug trace 的对外契约，集中在此维护。
     switch (error) {
       case SyncError::search_path_missing:
         return "search path does not exist";
       case SyncError::search_path_not_directory:
         return "search path is not a directory";
       case SyncError::workspace_invalid:
         return "path is not a valid Stacks workspace";
       case SyncError::workspace_not_registered:
         return "workspace path is not registered";
       case SyncError::project_db_unreadable:
         return "cannot parse project database";
       case SyncError::stack_db_unreadable:
         return "cannot parse stack database";
       case SyncError::timer_db_unreadable:
         return "cannot parse timer database";
       case SyncError::project_file_unreadable:
         return "one or more project files cannot be parsed";
       case SyncError::lock_failure:
         return "another Stacks Tools write is in progress";
       case SyncError::io_failure:
         return "failed to write database";
     }
     return "unknown sync error";
   }
   
   }  // namespace ProjectSync
