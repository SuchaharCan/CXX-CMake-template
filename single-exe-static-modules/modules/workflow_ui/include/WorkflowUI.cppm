/**
 * @file WorkflowUI.cppm
 * @module WorkflowUI
 * @brief Alfred Script Filter JSON 与动作分发适配层。
 *
 * 参数语法、日期校验和业务错误均由 C++ 核心处理。Alfred 只负责调用
 * `ui <query>` / `action <token>`，不需要再用 `/bin/zsh` 解析任务参数。
 *
 * UI 分层：
 * - filter：把查询映射为 Alfred Script Filter JSON；
 * - action：把稳定的冒号 token 映射到 TaskManager/ProjectSync/ReminderSync；
 * - 用户标题、描述和路径先做 base64url 编码，避免 `:`/空格破坏 token；
 * - 说明行始终 `valid=false`，Back 行固定在列表末尾，操作行默认第一。
 */
module;

#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "json.hpp"

export module WorkflowUI;

import std;
import InterfaceLog;
import ProjectSync;
import ReminderSync;
import TaskManager;

export namespace WorkflowUI {

/// 单次 Alfred 调用所需的运行上下文。
struct Context {
  std::filesystem::path workflow_dir;                  ///< 插件根目录，用于图标路径。
  TaskManager::DatabasePaths databases;                ///< 三个运行 JSON。
  std::optional<std::filesystem::path> inbox_library;  ///< 默认 Inbox 工作区。
};

/// 生成 Alfred Script Filter JSON；任何业务错误都转换为不可选错误条目。
std::string filter(const Context& context, std::string_view query);

/// 执行 Alfred action token；错误文本直接作为 Alfred Large Type/通知内容。
std::expected<std::string, std::string> action(const Context& context, std::string_view token);

/// URL-safe、无 padding 的 Base64 编码，用于 token 中的用户字段。
std::string base64url_encode(std::string_view value);

/// Base64url 解码；非法字符返回 nullopt，调用方转换为 action 错误。
std::optional<std::string> base64url_decode(std::string_view value);

}  // namespace WorkflowUI

namespace WorkflowUI {

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

// ---- 通用文本与图标工具 ---------------------------------------------------

/// 去除 Alfred 查询首尾 ASCII 空白。
std::string trim(std::string value) {
  const auto not_space = [](unsigned char character) { return !std::isspace(character); };
  value.erase(value.begin(), std::ranges::find_if(value, not_space));
  value.erase(std::ranges::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
}

/// 按首个分隔符拆分，保留其余部分作为 payload。
std::pair<std::string, std::string> split_first(std::string_view value, char separator) {
  const auto position = value.find(separator);
  if (position == std::string_view::npos) {
    return {std::string(value), std::string{}};
  }
  return {std::string(value.substr(0, position)), std::string(value.substr(position + 1))};
}

/// 一级命令使用插件品牌图标。
std::string icon_path(const Context& context) {
  const fs::path path = context.workflow_dir / "icons" / "icon.png";
  return path.string();
}

/// 引用 macOS 系统 CoreTypes 图标，避免随包分发第三方素材。
std::string system_icon(std::string_view name) {
  return (fs::path("/System/Library/CoreServices/CoreTypes.bundle/Contents/Resources") /
          std::string(name))
      .string();
}

/// 引用系统应用包内图标；调用方保证路径属于内置应用。
std::string app_icon(std::string_view app, std::string_view icon) {
  return (fs::path("/System/Applications") / std::string(app) / "Contents/Resources" /
          std::string(icon))
      .string();
}

/// 统一 Alfred 副标题的双语格式：中文在前、英文在后。
std::string bilingual(std::string_view chinese, std::string_view english) {
  return std::format("{} / {}", chinese, english);
}

/// 从 Alfred workflow 配置环境变量读取命令别名；空白值回退默认别名。
std::string alias_from_env(std::string_view variable, std::string_view fallback) {
  const std::string key(variable);
  if (const char* value = std::getenv(key.c_str()); value != nullptr && *value != '\0') {
    std::string cleaned = trim(value);
    if (!cleaned.empty()) {
      return cleaned;
    }
  }
  return std::string(fallback);
}

/// 八个可配置命令别名；字段初始化发生在每次 Alfred 进程启动时。
struct CommandAliases {
  std::string add_path = alias_from_env("STACKS_ALIAS_ADD_PATH", "ap");  ///< 添加路径。
  std::string reload = alias_from_env("STACKS_ALIAS_RELOAD", "rl");      ///< 重载全部。
  std::string config = alias_from_env("STACKS_ALIAS_CONFIG", "cf");      ///< 路径配置。
  std::string view = alias_from_env("STACKS_ALIAS_VIEW", "v");           ///< 三层浏览。
  std::string tasks = alias_from_env("STACKS_ALIAS_TASKS", "t");         ///< 全局任务搜索。
  std::string week = alias_from_env("STACKS_ALIAS_WEEK", "w");           ///< 本周截止。
  std::string timers = alias_from_env("STACKS_ALIAS_TIMERS", "tm");      ///< 计时列表。
  std::string inbox = alias_from_env("STACKS_ALIAS_INBOX", "i");         ///< 工作区 Inbox。
};

/// 项目使用文件夹/项目语义图标。
std::string icon_project() {
  return "icons/project.png";
}
/// 清单使用看板语义图标。
std::string icon_stack() {
  return "icons/board.png";
}
/// 任务使用勾选语义图标。
std::string icon_task() {
  return "icons/task.png";
}
/// Inbox 使用收件箱语义图标。
std::string icon_inbox() {
  return "icons/inbox.png";
}
/// 本周日期使用系统日历图标。
std::string icon_week() {
  return app_icon("Calendar.app", "AppIcon.icns");
}
/// 计时使用系统时钟图标。
std::string icon_timer() {
  return app_icon("Clock.app", "AppIcon.icns");
}
/// 同步使用系统同步图标。
std::string icon_sync() {
  return system_icon("Sync.icns");
}
/// 不可选错误/提示使用系统信息图标。
std::string icon_info() {
  return system_icon("ToolbarInfo.icns");
}
/// 说明行统一使用插件 Note 图标。
std::string icon_note() {
  return "icons/note.png";
}

/// 优先级值到中文短标签；未知值原样显示，空值显示“无”。
std::string priority_label(std::string_view value) {
  if (value == "low") {
    return "低";
  }
  if (value == "medium") {
    return "中";
  }
  if (value == "high") {
    return "高";
  }
  if (value == "critical") {
    return "紧急";
  }
  return value.empty() ? "无" : std::string(value);
}

/**
 * @brief 构造 Alfred item。
 * @param title 主标题。
 * @param subtitle 中英双语副标题。
 * @param argument 可选 action argv。
 * @param autocomplete 可选继续输入文本。
 * @param valid 是否允许回车。
 * @param icon Alfred 图标路径。
 * @note `uid` 使用 FNV-1a 稳定哈希，避免 Alfred 学习结果在每次调用随机变化。
 */
json item(std::string title, std::string subtitle, std::string argument = {},
          std::string autocomplete = {}, bool valid = true, std::string icon = "icons/icon.png") {
  std::uint32_t hash = 2166136261U;
  for (const char character : title + argument + autocomplete) {
    hash ^= static_cast<unsigned char>(character);
    hash *= 16777619U;
  }
  json value = {
      {"uid", std::format("{:08x}", hash)},  {"title", std::move(title)},
      {"subtitle", std::move(subtitle)},     {"valid", valid},
      {"icon", {{"path", std::move(icon)}}},
  };
  if (!argument.empty()) {
    value["arg"] = std::move(argument);
  }
  if (!autocomplete.empty()) {
    value["autocomplete"] = std::move(autocomplete);
  }
  value["text"] = {{"copy", value["title"]}, {"largetype", value["subtitle"]}};
  return value;
}

/// 构造不可选、带双语错误说明的 Alfred 错误条目。
json error_item(std::string message, std::string subtitle = {}) {
  const std::string chinese = subtitle.empty() ? std::move(message) : std::move(subtitle);
  return item("无法完成操作 / Operation failed",
              bilingual(chinese, "Open Alfred Debugger to inspect the failure"), "", "", false,
              icon_info());
}

/// 序列化 Alfred Script Filter 根对象；skipknowledge 防止非操作行污染排序。
std::string feedback(const json& items) {
  return json{{"skipknowledge", true}, {"items", items}}.dump();
}

/// 任务文本字段按“标题|描述|开始|截止|优先级”拆分后的内部值。
struct ParsedTaskInput {
  std::string title;        ///< 第一段，必填。
  std::string description;  ///< 第二段，可空。
  std::string start_date;   ///< 第三段，原始日期表达式。
  std::string due_date;     ///< 第四段，原始日期表达式。
  std::string priority;     ///< 第五段，原始优先级表达式。
};

/**
 * @brief 最多拆分五个字段，多余分隔符不会自动并入前四个字段。
 * @note 这里只拆字段；日期与优先级的合法性由 TaskManager 统一校验。
 */
ParsedTaskInput parse_task_fields(std::string_view text) {
  std::array<std::string, 5> fields;
  std::size_t field = 0;
  std::size_t start = 0;
  while (field < fields.size()) {
    const std::size_t end = text.find('|', start);
    fields[field] = trim(std::string(
        text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start)));
    if (end == std::string_view::npos) {
      break;
    }
    ++field;
    start = end + 1;
  }
  return {
      .title = fields[0],
      .description = fields[1],
      .start_date = fields[2],
      .due_date = fields[3],
      .priority = fields[4],
  };
}

/// 从 Project.json 读取 library_paths；文件缺失/损坏时返回空列表。
std::vector<std::string> library_paths(const Context& context) {
  std::vector<std::string> paths;
  std::ifstream input(context.databases.project_db);
  try {
    nlohmann::json project;
    if (input >> project && project.contains("library_paths") &&
        project["library_paths"].is_array()) {
      for (const auto& entry : project["library_paths"]) {
        if (entry.is_string()) {
          paths.push_back(entry.get<std::string>());
        }
      }
    }
  } catch (const std::exception&) {
  }
  return paths;
}

/// 把 TaskRecord 批量映射为任务条目；无结果时返回统一说明行。
json task_items(const std::vector<TaskManager::TaskRecord>& tasks) {
  json items = json::array();
  for (const auto& task : tasks) {
    const std::string due = task.due_date.empty() ? "未设置" : task.due_date;
    const std::string status_cn =
        task.done ? "已完成" : (!task.archived.empty() ? "已归档" : "待办");
    const std::string status_en =
        task.done ? "done" : (!task.archived.empty() ? "archived" : "todo");
    const std::string subtitle = bilingual(
        std::format("任务 · {} · {} → {} · 截止 {} · 优先级 {}", status_cn, task.project_title,
                    task.board_title, due, priority_label(task.priority)),
        std::format("Task · {} · {} → {} · due {} · prio {}", status_en, task.project_title,
                    task.board_title, due, task.priority.empty() ? "none" : task.priority));
    items.push_back(item(std::format("任务｜{} {}", task.done ? "✓" : "○", task.title), subtitle,
                         "", std::format("task:{}", task.id), false, icon_task()));
  }
  if (items.empty()) {
    items.push_back(item("任务｜没有匹配结果",
                         bilingual("换个关键词，或先执行 s rl 重载全部路径",
                                   "Try another keyword, or run s rl to reload all paths"),
                         "", "", false, icon_task()));
  }
  return items;
}

/**
 * @brief 大小写无关的“子串优先、子序列兜底”匹配。
 * @note 例如 `prjone` 可以匹配 `Project One`，但不会跨词修改原文。
 */
bool fuzzy_match(std::string_view haystack, std::string_view needle) {
  if (needle.empty()) {
    return true;
  }
  std::string haystack_lower(haystack);
  std::string needle_lower(needle);
  std::ranges::transform(haystack_lower, haystack_lower.begin(),
                         [](unsigned char character) { return std::tolower(character); });
  std::ranges::transform(needle_lower, needle_lower.begin(),
                         [](unsigned char character) { return std::tolower(character); });
  if (haystack_lower.find(needle_lower) != std::string::npos) {
    return true;
  }
  std::size_t cursor = 0;
  for (const char character : haystack_lower) {
    if (cursor < needle_lower.size() && character == needle_lower[cursor]) {
      ++cursor;
    }
  }
  return cursor == needle_lower.size();
}

/// 按项目/清单标题模糊过滤看板，保留 TaskManager 的稳定排序。
std::vector<TaskManager::BoardTarget> sorted_matching_boards(
    const std::vector<TaskManager::BoardTarget>& boards, std::string_view query) {
  std::vector<TaskManager::BoardTarget> result;
  for (const auto& board : boards) {
    const std::string haystack = std::format("{} {}", board.title, board.project_title);
    if (fuzzy_match(haystack, query)) {
      result.push_back(board);
    }
  }
  return result;
}

/**
 * @brief 渲染项目/清单选择列表。
 * @param boards 全部项目/清单目标。
 * @param query 用于模糊过滤的项目/清单关键词。
 * @param for_add true 时用于任务录入，false 时用于浏览。
 * @param payload 已输入的“标题|描述|开始|截止|优先级”，用于预览创建结果。
 */
json board_items(const std::vector<TaskManager::BoardTarget>& boards, std::string_view query,
                 bool for_add, std::string_view payload = {}) {
  json items = json::array();
  for (const auto& board : sorted_matching_boards(boards, query)) {
    const bool project = board.kind == "project";
    const std::string title =
        project ? std::format("项目｜{}", board.title)
                : std::format("项目｜{}  →  清单｜{}", board.project_title, board.title);
    const std::string icon = project ? icon_project() : icon_stack();
    if (for_add && !payload.empty()) {
      const auto parsed = parse_task_fields(payload);
      items.push_back(item(
          title,
          bilingual(
              std::format("三级：任务 · 回车创建：{} · 开始 {} · 截止 {} · 优先级 {} · {}",
                          parsed.title, parsed.start_date.empty() ? "未设置" : parsed.start_date,
                          parsed.due_date.empty() ? "未设置" : parsed.due_date,
                          parsed.priority.empty() ? "无" : priority_label(parsed.priority),
                          parsed.description.empty() ? "无描述" : parsed.description),
              std::format("Level 3: task · create {} · start {} · due {} · priority {}{}",
                          parsed.title, parsed.start_date.empty() ? "none" : parsed.start_date,
                          parsed.due_date.empty() ? "none" : parsed.due_date,
                          parsed.priority.empty() ? "none" : parsed.priority,
                          parsed.description.empty()
                              ? ""
                              : std::format(" · description: {}", parsed.description))),
          std::format("create:{}:{}", board.id, base64url_encode(payload)), "",
          !parsed.title.empty(), icon));
    } else {
      const std::string prefix = for_add ? "add:" : "board:";
      items.push_back(item(
          title,
          bilingual(
              std::format(
                  "{} · {} · {}", project ? "一级：项目" : "二级：清单", board.id,
                  for_add ? "继续输入：标题|描述|开始|截止|优先级" : "回车查看该项目/清单下的任务"),
              std::format("{} · {} · {}", project ? "Level 1: project" : "Level 2: list", board.id,
                          for_add ? "type title|description|start|due|priority"
                                  : "press enter to view its tasks")),
          "", prefix + board.id + " ", false, icon));
    }
  }
  if (items.empty()) {
    items.push_back(item("项目/清单｜没有匹配结果",
                         bilingual("先执行 s ap 添加路径，或 s rl 重载全部路径",
                                   "Run s ap to add a path, or s rl to reload all paths"),
                         "", "", false, icon_project()));
  }
  return items;
}

/// 渲染路径配置页：添加、重载与已保存路径说明行。
json config_items(const Context& context, const CommandAliases& aliases) {
  json items = json::array();
  items.push_back(item(
      "添加新路径",
      bilingual(std::format("add-path · {} — 选择新目录并加入索引", aliases.add_path),
                std::format("add-path · {} — choose a new folder and add it", aliases.add_path)),
      "add-path", "", true, icon_path(context)));
  items.push_back(
      item("重载全部路径",
           bilingual(std::format("reload · {} — 重新扫描所有已保存路径", aliases.reload),
                     std::format("reload · {} — rescan every saved path", aliases.reload)),
           "reload-all", "", true, icon_path(context)));
  const auto paths = library_paths(context);
  for (std::size_t index = 0; index < paths.size(); ++index) {
    const std::string& path = paths[index];
    items.push_back(
        item(std::format("路径｜{}", fs::path(path).filename().string()),
             bilingual(std::format("{} · {}", path, index == 0 ? "默认 Inbox 路径" : "已保存路径"),
                       index == 0 ? "path · default Inbox library" : "path · saved library"),
             path, "", false, system_icon("GenericFolderIcon.icns")));
  }
  return items;
}

/**
 * @brief 渲染任务详情页。
 * @note 说明行不可选；Back 固定最后一行；所有修改先进入二次输入页再 action。
 */
json task_detail(const TaskManager::TaskRecord& task) {
  json items = json::array();

  // 返回查询必须回到任务原层级：Inbox 带工作区，Backlog 带项目，Stack 带清单。
  std::string back_query;
  if (task.board_id == "inbox") {
    back_query = std::format("inbox-workspace:{} ", base64url_encode(task.library_path.string()));
  } else if (task.board_id == "backlog") {
    back_query = std::format("board:{} ", task.project_id);
  } else {
    back_query = std::format("board:{} ", task.board_id);
  }
  const json help_item =
      item(std::format("三级：任务｜{} {}", task.done ? "✓" : "○", task.title),
           bilingual(std::format("项目：{} → 清单：{} · 开始 {} · 截止 {} · 优先级 {} · {}",
                                 task.project_title, task.board_title,
                                 task.start_date.empty() ? "未设置" : task.start_date,
                                 task.due_date.empty() ? "未设置" : task.due_date,
                                 priority_label(task.priority), task.id),
                     std::format("project: {} → list: {} · start {} · due {} · priority {} · {}",
                                 task.project_title, task.board_title,
                                 task.start_date.empty() ? "none" : task.start_date,
                                 task.due_date.empty() ? "none" : task.due_date,
                                 task.priority.empty() ? "none" : task.priority, task.id)),
           "", "", false, icon_task());

  // 编辑组：条目本身不可选，autocomplete 让用户先输入值，再由 filter 生成 action。
  items.push_back(item(
      "修改标题", bilingual("输入新标题后回车，描述保持不变", "Type a new title, then press Enter"),
      "", std::format("rename:{} ", task.id), false, icon_info()));
  items.push_back(item(
      "修改描述",
      bilingual("输入新描述后回车，支持 Markdown/HTML", "Type a new description, then press Enter"),
      "", std::format("desc:{} ", task.id), false, icon_task()));
  items.push_back(item("设置截止日期",
                       bilingual("支持 2026-11-08、today、tomorrow、+3d、monday",
                                 "Supports YYYY-MM-DD, today, tomorrow, +3d and weekdays"),
                       "", std::format("due:{} ", task.id), false, icon_week()));
  items.push_back(
      item("设置开始日期",
           bilingual("与截止日期组成计划区间", "Combine with due date to define a planned range"),
           "", std::format("start:{} ", task.id), false, icon_week()));
  items.push_back(item("设置开始/截止区间",
                       bilingual("格式：开始日期|截止日期", "Format: start-date|due-date"), "",
                       std::format("schedule:{} ", task.id), false, icon_week()));
  items.push_back(item("设置优先级",
                       bilingual("可选：low/低、medium/中、high/高、critical/紧急",
                                 "Choose low, medium, high or critical priority"),
                       "", std::format("priority:{} ", task.id), false, icon_task()));

  // 完成状态以可执行 token 提交；1=完成、0=重开，避免文本布尔值歧义。
  items.push_back(item(
      task.done ? "重新打开任务" : "标记为已完成",
      task.done ? bilingual("恢复为待办并清空完成时间", "Reopen as To do and clear completion")
                : bilingual("写入完成状态和 100% 进度", "Mark done and write 100 percent progress"),
      std::format("done:{}:{}", task.id, task.done ? "0" : "1"), "", true, icon_task()));

  // 计时组保留一个不可选说明项，start/stop 各自是独立 action。
  items.push_back(
      item("计时操作",
           bilingual("启动计时或停止并写入 timelog", "Start a timer, or stop and write a timelog"),
           "", "", false, icon_timer()));
  items.back()["autocomplete"] = std::format("timer:{} ", task.id);
  items.push_back(item("启动计时", bilingual("立即为此任务开始计时", "Start timing this task now"),
                       std::format("timer-start:{}", task.id), "", true, icon_timer()));
  items.push_back(
      item("停止并写入时间记录",
           bilingual("生成 Stacks 兼容 timelog", "Stop and write a Stacks-compatible timelog"),
           std::format("timer-stop:{}", task.id), "", true, icon_timer()));

  // 外部提醒同步要求 duedate；无截止日期时显示不可选原因，不提前调用应用。
  if (!task.due_date.empty()) {
    items.push_back(
        item("同步到 Apple 提醒",
             bilingual(std::format("截止 {}", task.due_date), std::format("due {}", task.due_date)),
             std::format("sync:{}:reminders", task.id), "", true,
             app_icon("Reminders.app", "AppIcon.icns")));
    items.push_back(
        item("同步到 OmniFocus",
             bilingual(std::format("截止 {}", task.due_date), std::format("due {}", task.due_date)),
             std::format("sync:{}:omnifocus", task.id), "", true, icon_sync()));
    items.push_back(
        item("同步到提醒 + OmniFocus",
             bilingual("按 Stacks-ID 去重并更新", "Deduplicate and update by Stacks-ID"),
             std::format("sync:{}:both", task.id), "", true, icon_sync()));
  } else {
    items.push_back(
        item("提醒同步不可用",
             bilingual("先为该任务设置截止日期", "Set a due date before syncing reminders"), "", "",
             false, icon_info()));
  }

  // 归档保留任务文件；删除采用两步确认，真正执行移到 .trash。
  items.push_back(item("归档任务",
                       bilingual("写入 archived 时间，之后仍可恢复编辑",
                                 "Set archived time; the task remains recoverable"),
                       std::format("archive:{}", task.id), "", true, icon_project()));
  items.push_back(item("移到可恢复垃圾箱",
                       bilingual("从清单移除，文件移动到 tasks/.trash",
                                 "Remove from lists and move the file to tasks/.trash"),
                       "", std::format("delete:{} ", task.id), false,
                       system_icon("ToolbarDeleteIcon.icns")));
  items.push_back(help_item);
  items.push_back(item("← 返回上一级 / Back",
                       bilingual("返回任务列表，再选择其他操作",
                                 "Return to the task list and choose another action"),
                       "", back_query, false, icon_note()));
  return feedback(items);
}

// ---- Action 解析与业务分发 -------------------------------------------------

/// 外部同步提供方的显示名；与 action token 保持同一套英文名称。
std::string provider_label(ReminderSync::Provider provider) {
  return ReminderSync::provider_name(provider);
}

/// 将 action token 中的提供方文本解析为强类型枚举。
std::optional<ReminderSync::Provider> parse_provider(std::string_view value) {
  if (value == "reminders") {
    return ReminderSync::Provider::reminders;
  }
  if (value == "omnifocus") {
    return ReminderSync::Provider::omnifocus;
  }
  if (value == "both") {
    return ReminderSync::Provider::both;
  }
  return std::nullopt;
}

/// TaskManager 错误转换为 WorkflowUI 的 string error 通道。
std::expected<std::string, std::string> task_error(TaskManager::TaskError error) {
  return std::unexpected(std::string(TaskManager::task_error_message(error)));
}

/**
 * @brief 读取任务并同步到指定外部应用。
 * @note 返回值已经包含双语成功摘要和去重标记，Alfred 不再拼接 shell 文本。
 */
std::expected<std::string, std::string> sync_single(const Context& context,
                                                    std::string_view task_id,
                                                    ReminderSync::Provider provider,
                                                    bool dry_run = false) {
  const auto task =
      TaskManager::get_task(TaskManager::TaskId(std::string(task_id)), context.databases);
  if (!task) {
    return task_error(task.error());
  }
  ReminderSync::SyncOptions options;
  options.dry_run = dry_run;
  const auto result = ReminderSync::sync_task(
      ReminderSync::TaskReminder{
          .task_id = task->id,
          .title = task->title,
          .description = task->description,
          .due_date = task->due_date,
          .done = task->done,
      },
      provider, options);
  if (!result) {
    return std::unexpected(std::string(ReminderSync::sync_error_message(result.error())));
  }
  return std::format("已同步到 {} · {}\nSynced to {} · {}", provider_label(provider),
                     result->marker, provider_label(provider), result->marker);
}

/**
 * @brief 将冒号分隔 action token 分发到领域模块。
 * @note command 固定在 parts[0]；用户 payload 均已 base64url 编码。
 */
std::expected<std::string, std::string> dispatch_action(const Context& context,
                                                        std::string_view token) {
  // token 不使用 shell 引号协议，按冒号恢复字段即可。
  const auto parts = [&] {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= token.size()) {
      const std::size_t end = token.find(':', start);
      result.emplace_back(token.substr(start, end == std::string_view::npos ? end : end - start));
      if (end == std::string_view::npos) {
        break;
      }
      start = end + 1;
    }
    return result;
  }();
  if (parts.empty()) {
    return std::unexpected("empty action");
  }

  const std::string& command = parts[0];

  // 创建任务：create:<board-id>:<base64(payload)>。
  if (command == "create" && parts.size() == 3) {
    const auto payload = base64url_decode(parts[2]);
    if (!payload) {
      return std::unexpected("invalid create payload");
    }
    const auto parsed = parse_task_fields(*payload);
    TaskManager::CreateTaskOptions options{context.databases, context.inbox_library};
    const auto result = TaskManager::create_task(
        TaskManager::TargetId(parts[1]),
        TaskManager::TaskInput{
            TaskManager::TaskTitle(parsed.title),
            TaskManager::TaskDescription(parsed.description),
            parsed.start_date.empty() ? std::nullopt
                                      : std::optional<std::string>(parsed.start_date),
            parsed.due_date.empty() ? std::nullopt : std::optional<std::string>(parsed.due_date),
            parsed.priority,
        },
        options);
    if (!result) {
      return task_error(result.error());
    }
    return std::format("任务已创建 · {}\nTask created · {}", result->task_id.value,
                       result->task_id.value);
  }

  // 创建 Inbox：create-inbox:[base64(path):]<base64(payload)>。
  if (command == "create-inbox" && (parts.size() == 2 || parts.size() == 3)) {
    const auto payload = base64url_decode(parts.back());
    if (!payload) {
      return std::unexpected("invalid Inbox payload");
    }
    TaskManager::CreateTaskOptions options{context.databases, context.inbox_library};
    if (parts.size() == 3) {
      const auto library = base64url_decode(parts[1]);
      if (!library || library->empty()) {
        return std::unexpected("invalid Inbox library path");
      }
      options.inbox_library = fs::path(*library);
    }
    const auto parsed = parse_task_fields(*payload);
    const auto result = TaskManager::create_inbox_task(
        TaskManager::TaskInput{
            TaskManager::TaskTitle(parsed.title),
            TaskManager::TaskDescription(parsed.description),
            parsed.start_date.empty() ? std::nullopt
                                      : std::optional<std::string>(parsed.start_date),
            parsed.due_date.empty() ? std::nullopt : std::optional<std::string>(parsed.due_date),
            parsed.priority,
        },
        options);
    if (!result) {
      return task_error(result.error());
    }
    return std::format("已添加至收件箱 · {}\nAdded to Inbox · {}", result->task_id.value,
                       result->task_id.value);
  }

  // 单字段编辑：操作名:<task-id>:<base64(value)>。
  if ((command == "rename" || command == "desc" || command == "due" || command == "start" ||
       command == "priority") &&
      parts.size() == 3) {
    const auto payload = base64url_decode(parts[2]);
    if (!payload) {
      return std::unexpected("invalid edit payload");
    }
    TaskManager::TaskPatch patch;
    if (command == "rename") {
      patch.title = *payload;
    } else if (command == "desc") {
      patch.description = *payload;
    } else if (command == "due") {
      patch.due_date = *payload;
    } else if (command == "start") {
      patch.start_date = *payload;
    } else {
      patch.priority = *payload;
    }
    const auto result =
        TaskManager::update_task(TaskManager::TaskId(parts[1]), patch, context.databases);
    if (!result) {
      return task_error(result.error());
    }
    return "任务已更新\nTask updated";
  }

  // 计划区间：schedule:<task-id>:<base64(start)>:<base64(due)>。
  if (command == "schedule" && parts.size() == 4) {
    const auto start = base64url_decode(parts[2]);
    const auto due = base64url_decode(parts[3]);
    if (!start || !due) {
      return std::unexpected("invalid schedule payload");
    }
    const auto result =
        TaskManager::set_schedule(TaskManager::TaskId(parts[1]), *start, *due, context.databases);
    if (!result) {
      return task_error(result.error());
    }
    return std::format("计划已更新 · {} → {}\nSchedule updated · {} → {}", result->start_date,
                       result->due_date, result->start_date, result->due_date);
  }

  // 完成/重开使用 1/0 token，避免布尔文本大小写歧义。
  if (command == "done" && parts.size() == 3) {
    const auto result = TaskManager::set_task_done(TaskManager::TaskId(parts[1]), parts[2] == "1",
                                                   context.databases);
    if (!result) {
      return task_error(result.error());
    }
    return result->done ? "任务已完成\nTask completed" : "任务已重新打开\nTask reopened";
  }

  // 计时 action 直接使用任务 ID；业务模块负责状态冲突分类。
  if (command == "timer-start" && parts.size() == 2) {
    const auto result = TaskManager::start_timer(TaskManager::TaskId(parts[1]), context.databases);
    if (!result) {
      return task_error(result.error());
    }
    return std::format("计时已开始 · {}\nTimer started · {}", result->title, result->title);
  }
  if (command == "timer-stop" && parts.size() == 2) {
    const auto result = TaskManager::stop_timer(TaskManager::TaskId(parts[1]), context.databases);
    if (!result) {
      return task_error(result.error());
    }
    return std::format("计时已记录 · {} 秒\nTimer saved · {} seconds", result->duration_seconds,
                       result->duration_seconds);
  }
  if (command == "archive" && parts.size() == 2) {
    const auto result = TaskManager::archive_task(TaskManager::TaskId(parts[1]), context.databases);
    if (!result) {
      return task_error(result.error());
    }
    return "任务已归档\nTask archived";
  }

  // Alfred 第二次回车才发送 delete-confirm，第一次 delete 仅显示说明页。
  if (command == "delete-confirm" && parts.size() == 2) {
    const auto result = TaskManager::delete_task(TaskManager::TaskId(parts[1]), context.databases);
    if (!result) {
      return task_error(result.error());
    }
    return std::format("任务已移到 {}\nTask moved to {}", result->trash_file.filename().string(),
                       result->trash_file.filename().string());
  }

  // 单任务/本周同步共用 sync_single；批量模式收集失败明细后整体返回错误。
  if (command == "sync" && parts.size() == 3) {
    const auto provider = parse_provider(parts[2]);
    if (!provider) {
      return std::unexpected("unknown sync provider");
    }
    return sync_single(context, parts[1], *provider);
  }
  if (command == "sync-week" && parts.size() == 2) {
    const auto provider = parse_provider(parts[1]);
    if (!provider) {
      return std::unexpected("unknown sync provider");
    }
    const auto tasks = TaskManager::list_week_due(context.databases);
    if (!tasks) {
      return task_error(tasks.error());
    }
    std::size_t succeeded = 0;
    std::vector<std::string> failures;
    for (const auto& task : *tasks) {
      const auto result = sync_single(context, task.id, *provider);
      if (result) {
        ++succeeded;
      } else {
        failures.push_back(std::format("{}: {}", task.id, result.error()));
      }
    }
    if (!failures.empty()) {
      return std::unexpected(
          std::format("{} 个成功，{} 个失败：{}", succeeded, failures.size(), failures.front()));
    }
    return std::format("本周 {} 个截止任务已同步到 {}\n{} due tasks synced to {}", succeeded,
                       provider_label(*provider), succeeded, provider_label(*provider));
  }

  // 路径操作走 ProjectSync，不经过 TaskManager，保持模块职责分离。
  if (command == "add-path" && parts.size() == 2) {
    const auto path = base64url_decode(parts[1]);
    if (!path) {
      return std::unexpected("invalid add-path payload");
    }
    const auto result =
        ProjectSync::sync_all(*path, {context.databases.project_db, context.databases.stack_db});
    if (!result) {
      return std::unexpected(std::string(ProjectSync::sync_error_message(result.error())));
    }
    return std::format("已添加路径 · {} 个项目 · {} 个清单\nPath added · {} projects · {} lists",
                       result->project_count, result->stack_count, result->project_count,
                       result->stack_count);
  }
  if (command == "reload-all" && parts.size() == 1) {
    const auto result =
        ProjectSync::sync_all("", {context.databases.project_db, context.databases.stack_db});
    if (!result) {
      return std::unexpected(std::string(ProjectSync::sync_error_message(result.error())));
    }
    return std::format(
        "已重载全部路径 · {} 个项目 · {} 个清单\nAll paths reloaded · {} projects · {} lists",
        result->project_count, result->stack_count, result->project_count, result->stack_count);
  }
  return std::unexpected(std::format("unknown action: {}", token));
}

}  // namespace

std::string base64url_encode(std::string_view value) {
  // 6-bit 分组累积；`-_` 替换标准 `+/`，且省略 padding，适合 Alfred token。
  static constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string output;
  output.reserve((value.size() * 4 + 2) / 3);
  std::uint32_t buffer = 0;
  int bits = 0;
  for (const char raw_character : value) {
    const auto character = static_cast<unsigned char>(raw_character);
    buffer = (buffer << 8U) | character;
    bits += 8;
    while (bits >= 6) {
      bits -= 6;
      output.push_back(alphabet[(buffer >> bits) & 0x3FU]);
    }
  }
  if (bits > 0) {
    output.push_back(alphabet[(buffer << (6 - bits)) & 0x3FU]);
  }
  return output;
}

std::optional<std::string> base64url_decode(std::string_view value) {
  // 解码前严格拒绝 alphabet 外字符；不包含 padding，因此无需宽容跳过。
  static constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string output;
  output.reserve(value.size() * 3 / 4);
  std::uint32_t buffer = 0;
  int bits = 0;
  for (const char character : value) {
    const std::size_t index = alphabet.find(character);
    if (index == std::string_view::npos) {
      return std::nullopt;
    }
    buffer = (buffer << 6U) | static_cast<std::uint32_t>(index);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      output.push_back(static_cast<char>((buffer >> bits) & 0xFFU));
    }
  }
  return output;
}

std::string filter_impl(const Context& context, std::string_view raw_query) {
  // filter 是纯表现层：所有分支只调用领域模块并组装 JSON，不直接写文件。
  const std::string query = trim(std::string(raw_query));
  json items = json::array();

  try {
    const CommandAliases aliases;

    // 空查询：第一行是所有操作说明，后面是可选命令和项目/清单三级入口。
    if (query.empty()) {
      const json help_item = item(
          "所有可用操作",
          bilingual(
              std::format("{} 添加路径 · {} 重载 · {} 查看 · {} 搜索 · {} 本周 · {} 计时 · {} "
                          "收件箱 · {} 配置",
                          aliases.add_path, aliases.reload, aliases.view, aliases.tasks,
                          aliases.week, aliases.timers, aliases.inbox, aliases.config),
              std::format("{} add path · {} reload · {} view · {} search · {} week · {} timers · "
                          "{} inbox · {} config",
                          aliases.add_path, aliases.reload, aliases.view, aliases.tasks,
                          aliases.week, aliases.timers, aliases.inbox, aliases.config)),
          "", "", false, icon_note());
      items.push_back(
          item("添加路径",
               bilingual(
                   std::format("add-path · {} — 选择新目录并加入索引", aliases.add_path),
                   std::format("add-path · {} — choose and index a new library", aliases.add_path)),
               "", aliases.add_path + " ", false, icon_path(context)));
      items.push_back(
          item("重载全部路径",
               bilingual(std::format("reload · {} — 重新扫描所有已保存路径", aliases.reload),
                         std::format("reload · {} — rescan all saved libraries", aliases.reload)),
               "", aliases.reload, false, icon_path(context)));
      items.push_back(
          item("查看项目 / 清单任务",
               bilingual(std::format("view · {} — 项目级 → 清单级 → 任务级", aliases.view),
                         std::format("view · {} — project → list → task", aliases.view)),
               "", aliases.view + " ", false, icon_path(context)));
      items.push_back(item(
          "全局搜索任务",
          bilingual(
              std::format("tasks · {} — 搜索全部工作区、项目和清单", aliases.tasks),
              std::format("tasks · {} — search every library, project and list", aliases.tasks)),
          "", aliases.tasks + " ", false, icon_path(context)));
      items.push_back(item(
          "本周截止任务",
          bilingual(std::format("week · {} — 聚合全部路径", aliases.week),
                    std::format("week · {} — due this week across all libraries", aliases.week)),
          "", aliases.week, false, icon_path(context)));
      items.push_back(item(
          "运行中的计时",
          bilingual(std::format("timers · {} — 查看或停止计时", aliases.timers),
                    std::format("timers · {} — inspect or stop running timers", aliases.timers)),
          "", aliases.timers, false, icon_path(context)));
      items.push_back(item(
          "添加到收件箱",
          bilingual(std::format("inbox · {} — 先选择路径，默认第一个", aliases.inbox),
                    std::format("inbox · {} — choose a library; first is default", aliases.inbox)),
          "", aliases.inbox + " ", false, icon_path(context)));
      items.push_back(
          item("路径配置",
               bilingual(std::format("config · {} — 查看已保存路径", aliases.config),
                         std::format("config · {} — inspect saved library paths", aliases.config)),
               "", aliases.config, false, icon_path(context)));
      const auto boards = TaskManager::list_boards(context.databases);
      if (!boards) {
        items.push_back(error_item(std::string(TaskManager::task_error_message(boards.error()))));
        return feedback(items);
      }
      const json board_rows = board_items(*boards, "", true);
      for (const auto& board : board_rows) {
        items.push_back(board);
      }
      items.push_back(help_item);
      return feedback(items);
    }

    // 路径操作只返回一个可执行条目，避免 Alfred 对 action token 二次猜测。
    if (query == "reload-all") {
      return feedback(json::array({item("重载全部路径",
                                        bilingual("reload · rl — 重新扫描所有已保存路径",
                                                  "reload · rl — rescan all saved libraries"),
                                        "reload-all", "", true, icon_path(context))}));
    }

    if (query == "add-path") {
      return feedback(json::array({item("添加新路径",
                                        bilingual("add-path · ap — 选择目录并加入索引",
                                                  "add-path · ap — choose a folder and add it"),
                                        "add-path", "", true, icon_path(context))}));
    }

    if (query == "config") {
      return feedback(config_items(context, aliases));
    }

    // 兼容 `add`/`board`：`add` 用于快速创建，`board` 用于三级浏览入口。
    if (query == "add" || query == "board" || query.starts_with("board ")) {
      const auto boards = TaskManager::list_boards(context.databases);
      if (!boards) {
        return feedback(json::array(
            {error_item(std::string(TaskManager::task_error_message(boards.error())))}));
      }
      json help_item;
      if (query == "board" || query.starts_with("board ")) {
        help_item = item(
            "查看与编辑 / View and edit",
            bilingual(
                "输入关键词模糊匹配项目或清单，回车进入下一级；任务详情内可返回上一级",
                "Fuzzy-match projects or lists, press Enter to drill down, and use Back inside"),
            "", "", false, icon_note());
      }
      const std::string board_query = query.starts_with("board ") ? trim(query.substr(6)) : "";
      const json board_rows = board_items(*boards, board_query, query == "add");
      for (const auto& board : board_rows) {
        items.push_back(board);
      }
      if (!help_item.is_null()) {
        items.push_back(help_item);
      }
      return feedback(items);
    }

    // 选中目标后继续输入任务字段，最终生成 create:<board>:<payload>。
    if (query.starts_with("add:")) {
      const auto [target, payload] = split_first(query.substr(4), ' ');
      const auto boards = TaskManager::list_boards(context.databases);
      if (!boards) {
        return feedback(json::array(
            {error_item(std::string(TaskManager::task_error_message(boards.error())))}));
      }
      const auto board = std::ranges::find_if(
          *boards, [&target](const TaskManager::BoardTarget& value) { return value.id == target; });
      if (board == boards->end()) {
        return feedback(json::array({error_item("找不到目标项目/清单", target)}));
      }
      if (trim(payload).empty()) {
        return feedback(json::array(
            {item(std::format("{}：{}", board->kind == "project" ? "项目" : "清单", board->title),
                  bilingual("继续输入：标题|描述|开始|截止|优先级",
                            "Type title|description|start|due|priority"),
                  "", "", false, board->kind == "project" ? icon_project() : icon_stack())}));
      }
      const auto parsed = parse_task_fields(payload);
      if (parsed.title.empty()) {
        return feedback(json::array(
            {error_item("任务标题不能为空", "Format: title|description|start|due|priority")}));
      }
      const std::string encoded = base64url_encode(payload);
      return feedback(json::array({item(
          std::format("三级：任务｜创建：{}", parsed.title),
          bilingual(std::format("项目：{} → 清单：{} · 开始 {} · 截止 {} · 优先级 {}",
                                board->project_title, board->title,
                                parsed.start_date.empty() ? "未设置" : parsed.start_date,
                                parsed.due_date.empty() ? "未设置" : parsed.due_date,
                                parsed.priority.empty() ? "无" : priority_label(parsed.priority)),
                    "Press Enter to create this task in the selected project/list"),
          std::format("create:{}:{}", target, encoded), "", true,
          board->kind == "project" ? icon_project() : icon_stack())}));
    }

    // Inbox 第一层只列工作区；第一个路径作为默认值但不能越过选择步骤。
    if (query == "inbox") {
      const auto paths = library_paths(context);
      if (paths.empty()) {
        return feedback(
            json::array({item("收件箱｜没有已保存路径",
                              bilingual("先执行 s ap 添加路径", "Run s ap to add a library first"),
                              "", "", false, icon_inbox())}));
      }
      const json help_item =
          item("收件箱 / Inbox",
               bilingual("先选择工作区；进入后直接查看、模糊搜索或新建收件箱任务",
                         "Choose a library, then view, fuzzy-search or create Inbox tasks"),
               "", "", false, icon_note());
      for (std::size_t index = 0; index < paths.size(); ++index) {
        const std::string& path = paths[index];
        items.push_back(item(
            std::format("收件箱路径｜{}{}", fs::path(path).filename().string(),
                        index == 0 ? "（默认）" : ""),
            bilingual(std::format("{} · {}", path, index == 0 ? "默认第一个路径" : "已保存路径"),
                      index == 0 ? "Default (first) Inbox library" : "Saved Inbox library"),
            "", std::format("inbox-workspace:{} ", base64url_encode(path)), false, icon_inbox()));
      }
      items.push_back(help_item);
      return feedback(items);
    }

    // Inbox 工作区页：无任务直接显示新建；有任务显示模糊搜索结果。
    if (query.starts_with("inbox-workspace:")) {
      const auto [encoded_path, search] = split_first(query.substr(16), ' ');
      const auto path = base64url_decode(encoded_path);
      if (!path || path->empty()) {
        return feedback(json::array({error_item("收件箱路径无效", "Invalid Inbox library path")}));
      }
      const std::string path_token = base64url_encode(*path);
      const std::string search_text = trim(search);
      TaskManager::TaskListOptions options;
      options.target = TaskManager::TargetId("inbox");
      const auto tasks = TaskManager::list_tasks(context.databases, options);
      if (!tasks) {
        return feedback(
            json::array({error_item(std::string(TaskManager::task_error_message(tasks.error())))}));
      }
      std::vector<TaskManager::TaskRecord> filtered;
      for (const auto& task : *tasks) {
        if (task.library_path.lexically_normal() != fs::path(*path).lexically_normal()) {
          continue;
        }
        const std::string haystack = std::format("{} {}", task.title, task.description);
        if (fuzzy_match(haystack, search_text)) {
          filtered.push_back(task);
        }
      }
      const json help_item = item(
          std::format("收件箱工作区 / Inbox workspace：{}", fs::path(*path).filename().string()),
          bilingual("直接显示当前收件箱任务；输入关键词模糊匹配，无匹配时进入新建",
                    "Inbox tasks are shown directly; fuzzy-match or create when nothing matches"),
          "", "", false, icon_note());

      if (!filtered.empty()) {
        const json task_rows = task_items(filtered);
        for (const auto& task : task_rows) {
          items.push_back(task);
        }
      } else if (!search_text.empty()) {
        items.push_back(
            item(std::format("新建任务 / Create task：{}", search_text),
                 bilingual("当前收件箱没有匹配任务，回车自动进入新建流程",
                           "No matching Inbox task; press Enter to create it"),
                 std::format("create-inbox:{}:{}", path_token, base64url_encode(search_text)), "",
                 true, icon_task()));
      }

      if (!filtered.empty()) {
        items.push_back(item(
            "新建收件箱任务 / Create another Inbox task",
            bilingual("保持当前任务列表，继续录入新任务", "Keep the list and create another task"),
            "", std::format("inbox-create:{} ", path_token), false, icon_task()));
      } else if (search_text.empty()) {
        items.push_back(item("录入收件箱任务 / Create Inbox task",
                             bilingual("当前工作区收件箱无任务时直接进入录入",
                                       "Create directly when this Inbox has no matching tasks"),
                             "", std::format("inbox-create:{} ", path_token), false, icon_task()));
      }
      items.push_back(help_item);
      items.push_back(item("← 返回路径列表 / Back",
                           bilingual("返回全部收件箱工作区", "Back to all Inbox libraries"), "",
                           "inbox ", false, icon_note()));
      return feedback(items);
    }

    // 新建 Inbox 任务：路径与 payload 均保留到最终 action token。
    if (query.starts_with("inbox-create:")) {
      const auto [encoded_path, payload] = split_first(query.substr(13), ' ');
      const auto path = base64url_decode(encoded_path);
      if (!path || path->empty()) {
        return feedback(json::array({error_item("收件箱路径无效", "Invalid Inbox library path")}));
      }
      if (trim(payload).empty()) {
        return feedback(json::array({item("收件箱｜填写任务",
                                          bilingual("继续输入：标题|描述|开始|截止|优先级",
                                                    "Type title|description|start|due|priority"),
                                          "", "", false, icon_inbox())}));
      }
      const auto parsed = parse_task_fields(payload);
      if (parsed.title.empty()) {
        return feedback(
            json::array({error_item("任务标题不能为空", "Task title cannot be empty")}));
      }
      return feedback(json::array({item(
          std::format("三级：任务｜添加到收件箱：{}", parsed.title),
          bilingual(std::format("路径：{} · 开始 {} · 截止 {} · 优先级 {}", *path,
                                parsed.start_date.empty() ? "未设置" : parsed.start_date,
                                parsed.due_date.empty() ? "未设置" : parsed.due_date,
                                parsed.priority.empty() ? "无" : priority_label(parsed.priority)),
                    "Library, plan dates and priority are shown above; press Enter to create"),
          std::format("create-inbox:{}:{}", base64url_encode(*path), base64url_encode(payload)), "",
          true, icon_inbox())}));
    }

    // 用户看到 `s i xxx` 时先阻止直接录入，确保工作区选择具有明确语义。
    if (query.starts_with("inbox ")) {
      return feedback(
          json::array({item("s i 后不接受直接文本 / Text after s i is blocked",
                            bilingual("请先选择工作区，再直接搜索或新建收件箱任务",
                                      "Select a library first, then search or create Inbox tasks"),
                            "", "inbox", false, icon_inbox())}));
    }

    // 全局任务搜索覆盖所有工作区，不使用 board target 过滤。
    if (query == "tasks") {
      const auto tasks = TaskManager::list_tasks(context.databases);
      if (!tasks) {
        return feedback(
            json::array({error_item(std::string(TaskManager::task_error_message(tasks.error())))}));
      }
      json rows = task_items(*tasks);
      rows.push_back(item("全局任务搜索",
                          bilingual("范围：全部工作区 → 全部项目 → 全部清单",
                                    "Scope: every library → every project → every list"),
                          "", "", false, icon_note()));
      return feedback(rows);
    }
    if (query.starts_with("tasks ")) {
      TaskManager::TaskListOptions options;
      options.query = trim(query.substr(6));
      const auto tasks = TaskManager::list_tasks(context.databases, options);
      if (!tasks) {
        return feedback(
            json::array({error_item(std::string(TaskManager::task_error_message(tasks.error())))}));
      }
      json rows = task_items(*tasks);
      rows.push_back(item(std::format("全局任务搜索｜{}", trim(query.substr(6))),
                          bilingual("范围：全部工作区 → 全部项目 → 全部清单",
                                    "Scope: every library → every project → every list"),
                          "", "", false, icon_note()));
      return feedback(rows);
    }

    // 项目/清单任务页：任务匹配不到时按当前输入创建，Back 固定在最后。
    if (query.starts_with("board:")) {
      const auto [target, search] = split_first(query.substr(6), ' ');
      TaskManager::TaskListOptions options;
      options.target = TaskManager::TargetId(target);
      const auto tasks = TaskManager::list_tasks(context.databases, options);
      if (!tasks) {
        return feedback(
            json::array({error_item(std::string(TaskManager::task_error_message(tasks.error())))}));
      }
      const json help_item =
          item("项目 / 清单任务页",
               bilingual("输入关键词模糊匹配任务；回车编辑，匹配不到时进入插入流程",
                         "Fuzzy-match tasks, press Enter to edit, or create when nothing matches"),
               "", "", false, icon_note());
      std::vector<TaskManager::TaskRecord> filtered;
      const std::string search_text = trim(search);
      for (const auto& task : *tasks) {
        const std::string haystack = std::format("{} {} {} {}", task.title, task.description,
                                                 task.project_title, task.board_title);
        if (fuzzy_match(haystack, search_text)) {
          filtered.push_back(task);
        }
      }
      if (!search_text.empty() && filtered.empty()) {
        items.push_back(item(std::format("新建任务 / Create task：{}", search_text),
                             bilingual("没有匹配任务，回车进入插入流程",
                                       "No matching task; press Enter to start the insert flow"),
                             std::format("create:{}:{}", target, base64url_encode(search_text)), "",
                             true, icon_task()));
      } else if (!filtered.empty()) {
        const json task_rows = task_items(filtered);
        for (const auto& task : task_rows) {
          items.push_back(task);
        }
      } else {
        items.push_back(
            item("当前项目 / 清单没有任务",
                 bilingual("继续输入标题即可创建新任务", "Type a title to create the first task"),
                 "", "", false, icon_task()));
      }
      items.push_back(help_item);
      items.push_back(item("← 返回上一级 / Back",
                           bilingual("返回项目与清单列表，继续选择其他层级",
                                     "Return to the project and list selection"),
                           "", "board ", false, icon_note()));
      return feedback(items);
    }

    // 本周页第一项为批量同步 action，随后是本周边界说明和任务列表。
    if (query == "week") {
      const auto tasks = TaskManager::list_week_due(context.databases);
      if (!tasks) {
        return feedback(
            json::array({error_item(std::string(TaskManager::task_error_message(tasks.error())))}));
      }
      const auto bounds = TaskManager::week_bounds();
      json due_items = task_items(*tasks);
      due_items.insert(due_items.begin(),
                       item("同步本周到提醒 + OmniFocus",
                            bilingual("任务必须已有截止日期", "Tasks must already have due dates"),
                            "sync-week:both", "", true, icon_sync()));
      due_items.push_back(item(
          "本周截止任务",
          bilingual(std::format("{} → {} · {} 项", bounds.first, bounds.last, tasks->size()),
                    std::format("{} → {} · {} tasks", bounds.first, bounds.last, tasks->size())),
          "", "", false, icon_note()));
      return feedback(due_items);
    }

    // 计时页每一项回车即停止并写入 timelog；空列表给出操作说明。
    if (query == "timers") {
      const auto timers = TaskManager::list_timers(context.databases);
      if (!timers) {
        return feedback(json::array(
            {error_item(std::string(TaskManager::task_error_message(timers.error())))}));
      }
      json timer_items = json::array();
      for (const auto& timer : *timers) {
        timer_items.push_back(
            item(std::format("▶ {}", timer.title),
                 bilingual(std::format("开始于 {} · {}", timer.started_at, timer.task_id),
                           std::format("started {} · task {}", timer.started_at, timer.task_id)),
                 std::format("timer-stop:{}", timer.task_id), "", true, icon_timer()));
      }
      if (timer_items.empty()) {
        timer_items.push_back(
            item("没有运行中的计时",
                 bilingual("从任务详情页启动计时", "Start a timer from a task detail page"), "", "",
                 false, icon_timer()));
      }
      return feedback(timer_items);
    }

    // 任务详情页集中提供编辑、日期、完成、计时、同步、归档和删除入口。
    if (query.starts_with("task:")) {
      const std::string id = trim(query.substr(5));
      const auto task = TaskManager::get_task(TaskManager::TaskId(id), context.databases);
      if (!task) {
        return feedback(
            json::array({error_item(std::string(TaskManager::task_error_message(task.error())))}));
      }
      return task_detail(*task);
    }

    // 单字段编辑先显示输入页，再生成对应 action 的确认条目。
    for (const std::string_view command : {"rename:", "desc:", "due:", "start:", "priority:"}) {
      if (!query.starts_with(command)) {
        continue;
      }
      const std::string remainder = query.substr(command.size());
      const auto [id, payload] = split_first(remainder, ' ');
      if (trim(payload).empty()) {
        const std::string label = command == "rename:"  ? "新标题"
                                  : command == "desc:"  ? "新描述"
                                  : command == "due:"   ? "截止日期"
                                  : command == "start:" ? "开始日期"
                                                        : "优先级";
        const std::string english = command == "priority:" ? "Type low, medium, high or critical"
                                                           : "Type the new value, then press Enter";
        return feedback(json::array(
            {item(std::format("填写{}", label),
                  bilingual(command == "priority:"
                                ? "可选：low/低、medium/中、high/高、critical/紧急"
                                : "继续输入；日期可用 today、tomorrow、+3d、monday 或 YYYY-MM-DD",
                            english),
                  "", "", false, icon_info())}));
      }
      const std::string action_prefix(command.substr(0, command.size() - 1));
      return feedback(
          json::array({item(std::format("三级：任务｜应用更改：{}", trim(payload)),
                            bilingual(std::format("任务 {}", id), std::format("Task {}", id)),
                            std::format("{}:{}:{}", action_prefix, id, base64url_encode(payload)),
                            "", true, icon_task())}));
    }

    // 开始/截止区间必须一次输入两端，后端统一校验先后关系。
    if (query.starts_with("schedule:")) {
      const std::string remainder = query.substr(9);
      const auto [id, payload] = split_first(remainder, ' ');
      const auto [start, due] = split_first(payload, '|');
      if (trim(start).empty() || trim(due).empty()) {
        return feedback(
            json::array({error_item("格式：开始日期|截止日期", "Format: start-date|due-date")}));
      }
      return feedback(
          json::array({item(std::format("三级：任务｜应用计划：{} → {}", trim(start), trim(due)),
                            bilingual(std::format("任务 {}", id), std::format("Task {}", id)),
                            std::format("schedule:{}:{}:{}", id, base64url_encode(trim(start)),
                                        base64url_encode(trim(due))),
                            "", true, icon_week())}));
    }

    // timer 查询保留为详情页快捷入口；与 timers 总列表职责不同。
    if (query.starts_with("timer:")) {
      const std::string id = trim(query.substr(6));
      const auto task = TaskManager::get_task(TaskManager::TaskId(id), context.databases);
      if (!task) {
        return feedback(
            json::array({error_item(std::string(TaskManager::task_error_message(task.error())))}));
      }
      return feedback(json::array({
          item("启动计时", bilingual(task->title, "Start timing this task"),
               std::format("timer-start:{}", id), "", true, icon_timer()),
          item("停止并写入 timelog", bilingual(task->title, "Stop and write a timelog"),
               std::format("timer-stop:{}", id), "", true, icon_timer()),
      }));
    }

    // 删除采用二次确认，真正删除前不发送破坏性 action。
    if (query.starts_with("delete:")) {
      const std::string id = trim(query.substr(7));
      return feedback(json::array({item(
          "确认移到可恢复垃圾箱？",
          bilingual("再次回车执行；任务文件移入 tasks/.trash，并从清单移除",
                    "Press Enter again; move the task to tasks/.trash and remove it from lists"),
          std::format("delete-confirm:{}", id), "", true, system_icon("ToolbarDeleteIcon.icns"))}));
    }

    // 默认分支把任意文本视为任务字段，每个项目/清单都显示创建预览。
    {
      const auto boards = TaskManager::list_boards(context.databases);
      if (!boards) {
        return feedback(json::array(
            {error_item(std::string(TaskManager::task_error_message(boards.error())))}));
      }
      return feedback(board_items(*boards, "", true, query));
    }

    return feedback(json::array(
        {error_item("没有匹配的命令", "按 Esc 回到 s 总览；可用 ap、rl、v、t、w、tm、i、cf")}));
  } catch (const std::exception& error) {
    // UI 适配层最后一道异常边界；任何异常转换为可选错误条目而非崩溃 Alfred。
    return feedback(
        json::array({error_item("界面数据处理失败", std::format("UI error: {}", error.what()))}));
  }
}

std::string filter(const Context& context, std::string_view raw_query) {
  // 包装 impl 以统一记录 query、输出字节数和 item 数量。
  const std::string output = filter_impl(context, raw_query);
  int item_count = -1;
  try {
    const auto parsed = json::parse(output);
    if (parsed.contains("items") && parsed["items"].is_array()) {
      item_count = static_cast<int>(parsed["items"].size());
    }
  } catch (const std::exception&) {
  }
  interface_log::record(
      interface_log::ApiName("WorkflowUI::filter"),
      interface_log::LogText(std::format("query={}", raw_query)),
      interface_log::LogText(std::format("bytes={} items={}", output.size(), item_count)),
      item_count >= 0 ? interface_log::ApiStatus::ok : interface_log::ApiStatus::failed);
  return output;
}

std::expected<std::string, std::string> action(const Context& context, std::string_view token) {
  // action 层只负责日志边界；业务错误文本由 dispatch_action 生成。
  auto result = dispatch_action(context, token);
  interface_log::record(interface_log::ApiName("WorkflowUI::action"),
                        interface_log::LogText(std::string(token)),
                        interface_log::LogText(result ? *result : result.error()),
                        result ? interface_log::ApiStatus::ok : interface_log::ApiStatus::failed);
  return result;
}

}  // namespace WorkflowUI
