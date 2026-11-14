# Omni-Stacks / single-exe-static-modules 的 LLDB 调试手册

本文以当前模板的真实代码和 GTest 为对象，给出一套可照着执行的 LLDB 调试流程。
内容不只介绍命令，还包含三个已经实际运行过的实验：

1. 调试 `TaskManager::generate_id`，检查 20 字符任务 ID；
2. 调试 `TaskManager::week_bounds`，检查周日作为一周最后一天时是否正确；
3. 调试 `WorkflowUI::filter`，检查 Alfred Script Filter JSON 与条目数量。

实验使用空白测试工作区 `tests/.runtime`，不会直接修改真实 Stacks 库。

## 1. 环境与前提

模板当前要求：

```text
macOS
CMake >= 4.5.0
Ninja
Homebrew LLVM / Clang >= 23.1
just
```

本文实际验证环境：

```text
lldb-2103.0.34.407
Homebrew clang version 23.1.4
CMake 4.5.0
Ninja 1.13.2
```

进入模板目录：

```bash
cd /Users/songhoujin/00CMake-Template/single-exe-static-modules
```

检查工具：

```bash
lldb --version
clang++ --version
cmake --version
ninja --version
```

首次在 macOS 上调试时，系统可能提示允许终端或 Codex 使用 Developer Tools。
若系统拒绝了调试权限，需要在“系统设置 → 隐私与安全性 → 开发者工具”中允许当前
终端应用。

## 2. 构建带完整符号的 Debug 版本

不要使用 RelWithDebInfo 作为第一轮排错环境。RelWithDebInfo 有调试信息，但变量
仍可能被优化掉。先配置 Debug：

```bash
just configure \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCXX_ENABLE_GTEST=ON \
  -DCXX_ENABLE_DOCS=OFF
```

构建可执行文件和测试：

```bash
cmake --build build \
  --target Stacks_Tools static_modules_tests \
  -j10
```

检查 Mach-O 与符号：

```bash
file build/static_modules_tests
nm -nm build/static_modules_tests | c++filt | grep generate_id
```

也可以在 LLDB 内查询：

```bash
lldb -b build/static_modules_tests \
  -o 'image lookup -rn generate_id' \
  -o 'image lookup -rn week_bounds' \
  -o 'image lookup -rn filter_impl' \
  -o quit
```

正常会看到类似：

```text
Summary: static_modules_tests`TaskManager::generate_id@TaskManager()
Summary: static_modules_tests`TaskManager::week_bounds@TaskManager(...)
Summary: static_modules_tests`WorkflowUI::filter_impl@WorkflowUI(...)
```

`@TaskManager`、`@WorkflowUI` 表示函数来自对应 C++ 模块。

## 3. 启动 LLDB 的两种方式

### 3.1 交互模式

```bash
lldb build/static_modules_tests
```

进入 LLDB 后设置断点并运行：

```text
(lldb) breakpoint set --name TaskManager::generate_id
(lldb) run --gtest_filter=Boundary.GenerateIdFixedLengthAndAlphabet
```

### 3.2 批处理模式

适合把调试过程写入日志：

```bash
lldb -b build/static_modules_tests \
  -o 'breakpoint set --name TaskManager::generate_id' \
  -o 'run --gtest_filter=Boundary.GenerateIdFixedLengthAndAlphabet' \
  -o 'bt' \
  -o quit
```

`-b` 表示 batch，不进入交互提示；每个 `-o` 执行一条 LLDB 命令。

## 4. 最基本的调试流程

一个有效调试循环通常是：

```text
设置断点 → run → 看当前位置 → bt → frame variable
→ step/next → 修改预期后重新 run
```

示例：

```text
(lldb) breakpoint set --name TaskManager::generate_id
(lldb) run --gtest_filter=Boundary.GenerateIdFixedLengthAndAlphabet
(lldb) bt
(lldb) frame variable
(lldb) next
(lldb) continue
```

关键命令：

| LLDB 命令 | 作用 |
| --- | --- |
| `run [args...]` | 启动程序 |
| `process launch -- [args]` | 显式启动进程 |
| `continue` / `c` | 继续运行 |
| `next` / `n` | 当前帧下一行，不进入被调函数 |
| `step` / `s` | 进入被调函数 |
| `finish` | 执行到当前函数返回 |
| `until` | 执行到当前行之后/指定行 |
| `breakpoint list` | 查看断点 |
| `breakpoint disable 1` | 禁用 1 号断点 |
| `breakpoint delete 1` | 删除 1 号断点 |
| `thread backtrace` / `bt` | 当前线程调用栈 |
| `frame variable` / `v` | 当前帧局部变量 |
| `frame select N` | 选择调用栈第 N 帧 |
| `up` / `down` | 在调用栈上下移动 |
| `expression -- expr` / `p expr` | 执行 C++ 表达式 |
| `continue` | 继续到下一个断点 |
| `quit` / `q` | 退出 LLDB |

不要把 GDB 的 `po` 当成通用 C++ 打印命令。LLDB 调试 C++ 时优先使用：

```text
(lldb) frame variable variable_name
(lldb) expression -- variable_name
```

## 5. 断点：最常用的功能

### 5.1 按函数设置

```text
(lldb) breakpoint set --name TaskManager::generate_id
(lldb) breakpoint set --name WorkflowUI::filter
```

模块中的完整显示可能是：

```text
TaskManager::generate_id@TaskManager()
```

函数名断点通常最稳，不怕源码行号变化。

### 5.2 按文件与行号设置

```text
(lldb) breakpoint set --file TaskManager.cppm --line 1024
(lldb) breakpoint set --file WorkflowUI.cppm --line 1418
```

如果源码后续修改导致行号变化，先执行：

```text
(lldb) source list TaskManager::generate_id
```

再选择目标行。

### 5.3 条件断点

只在满足条件时停下：

```text
(lldb) breakpoint set --file TaskManager.cppm --line 1919 \
  --condition 'weekday_value == 0'
```

适合测试循环几百次、只关心周日边界的场景。

### 5.4 忽略前 N 次命中

`ignore-count` 表示跳过前 N 次命中：

```text
(lldb) breakpoint set --name TaskManager::generate_id --ignore-count 199
```

这会在第 200 次调用时停下。

### 5.5 断点命中后自动执行命令

```text
(lldb) breakpoint command add 1
> frame variable
> bt 3
> continue
> DONE
```

适合记录循环或偶发问题。

## 6. 查看变量与修改变量

常用命令：

```text
(lldb) frame variable
(lldb) frame variable id
(lldb) expression -- id
(lldb) expression -- id.size()
(lldb) expression -- anchor
(lldb) expression -- weekday_value
```

修改变量：

```text
(lldb) expression -- weekday_value = 6
```

修改变量通常只影响当前进程，不修改源码。

查看容器/字符串时：

```text
(lldb) frame variable --show-all-children
(lldb) expression -- output.substr(0, 240)
(lldb) expression -- items.size()
```

如果输出太长，设置上限：

```text
(lldb) settings set target.max-children-count 200
```

### 6.1 断点刚命中时变量可能还没初始化

函数入口断点命中时，后面的局部变量往往还没有执行初始化。此时
`frame variable` 可能显示 `0`、空字符串或看似随机的值。这不是程序 bug。

例如在 `TaskManager::generate_id` 入口停下时，`id` 尚未构造。应执行几次
`next`，或在“变量已经构造、返回值尚未生成”的行设置断点。

## 7. 调用栈与线程

```text
(lldb) bt
(lldb) thread list
(lldb) thread backtrace all
```

示例调用栈：

```text
frame #0: TaskManager::generate_id
frame #1: Boundary_GenerateIdFixedLengthAndAlphabet_Test::TestBody
frame #2: testing::Test::Run
```

查看上层测试代码：

```text
(lldb) frame select 1
(lldb) source list
```

回到业务函数：

```text
(lldb) frame select 0
```

## 8. 实验一：调试任务 ID 生成

目标：检查 `TaskManager::generate_id` 是否返回 20 字符且字符均在合法字母表内。

当前源码关键位置：

```text
modules/task_manager/include/TaskManager.cppm
generate_id
line 1024: return TaskId(std::move(id));
```

### 8.1 交互式步骤

```bash
lldb build/static_modules_tests
```

```text
(lldb) breakpoint set --file TaskManager.cppm --line 1024
(lldb) run --gtest_filter=Boundary.GenerateIdFixedLengthAndAlphabet
(lldb) bt 4
(lldb) frame variable id
(lldb) expression -- id.size()
(lldb) expression -- id
```

### 8.2 预期观察

`id` 是已经填满的 20 字符字符串，例如一次实际运行得到：

```text
(std::string) id = "VZV_t-pPfIUSP-GXGUr7"
(std::basic_string<char>::size_type) $0 = 20
```

随机 ID 每次不同，但长度必须始终为 20，字符必须属于：

```text
-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz
```

继续运行：

```text
(lldb) breakpoint disable 1
(lldb) continue
```

测试应显示：

```text
[ PASSED ] 1 test.
```

### 8.3 如果 ID 长度错误

在 LLDB 中检查：

```text
(lldb) expression -- TaskManager::kTaskIdLength
(lldb) frame variable id
(lldb) expression -- id.size()
```

若常量是 19/21，检查 `kTaskIdLength` 是否被改坏；若长度正确但字符错误，
继续检查 `time_bits`、`sequence`、`generator` 和写字符的两个循环。

实际运行日志：

```text
tests/logs/lldb-debug-1-generate-id.log
```

## 9. 实验二：调试本周边界

目标：2026-11-01 是周日，`week_bounds("2026-11-01")` 应返回：

```text
first = 2026-10-26   # 周一
last  = 2026-11-01   # 周日
```

当前源码关键位置：

```text
modules/task_manager/include/TaskManager.cppm
line 1919: return { .first=..., .last=... };
```

### 9.1 交互式步骤

```bash
lldb build/static_modules_tests
```

```text
(lldb) breakpoint set --file TaskManager.cppm --line 1919
(lldb) run --gtest_filter=Boundary.NormalizeDateAndWeekBounds
(lldb) bt 4
(lldb) frame variable anchor parsed weekday_value monday_based monday
(lldb) expression -- anchor
(lldb) expression -- weekday_value
(lldb) expression -- monday_based
```

### 9.2 预期观察

实际 LLDB 输出：

```text
(const std::string) anchor = "2026-11-01"
(const std::optional<std::chrono::year_month_day>) parsed = Has Value=true {
  Value = (__y_ = year=2026, __m_ = month=November, __d_ = day=1)
}
(const int) weekday_value = 0
(const int) monday_based = 6
(const std::chrono::sys_days) monday = date=2026-10-26Z timestamp=20752 days
```

这里：

- `weekday_value = 0` 表示周日；
- `monday_based = 6` 表示周日是周一基索引的第 6 天；
- `monday = 2026-10-26`；
- 返回值最后一项应使用 `monday + 6 days`，即 2026-11-01。

单步到返回项：

```text
(lldb) next
(lldb) frame variable monday
(lldb) expression -- std::chrono::year_month_day{monday + std::chrono::days{6}}
```

继续：

```text
(lldb) breakpoint disable 1
(lldb) continue
```

### 9.3 如果周日被算成周六

优先检查：

```text
(lldb) expression -- (weekday_value + 6) % 7
(lldb) expression -- monday + std::chrono::days{6}
```

若最后表达式是 2026-10-31，检查返回值是否误写成 `days{5}`。对应变异测试：

```bash
just mutation-test
```

应看到 `week-sunday` mutant 被测试杀死。

实际运行日志：

```text
tests/logs/lldb-debug-2-week-bounds.log
```

## 10. 实验三：调试 Alfred Script Filter

目标：调试 `WorkflowUI::filter`，确认空查询能生成合法 Alfred JSON，并检查
条目数量。

当前源码关键位置：

```text
modules/workflow_ui/include/WorkflowUI.cppm
line 1418: interface_log 记录前，output 和 item_count 已生成
```

测试名：

```text
Boundary.WorkflowUiEmitsValidAlfredJsonAndExecutesCreateAction
```

### 10.1 交互式步骤

```bash
lldb build/static_modules_tests
```

```text
(lldb) breakpoint set --file WorkflowUI.cppm --line 1418
(lldb) run --gtest_filter=Boundary.WorkflowUiEmitsValidAlfredJsonAndExecutesCreateAction
(lldb) bt 4
(lldb) frame variable raw_query output item_count
(lldb) expression -- output.substr(0, 240)
(lldb) expression -- item_count
```

### 10.2 预期观察

第一次 `filter` 调用参数为空：

```text
(std::string_view) raw_query = ""
(int) item_count = 12
```

一次实际运行的输出前缀：

```text
{"items":[
  {"autocomplete":"ap ","title":"添加路径",...},
  {"autocomplete":"rl","title":"重载全部路径",...},
  {"autocomplete":"v ","title":"查看项目 / 清单任务",...}
]}
```

`item_count = 12` 是当前 Fixture 下的结果，包含操作入口、项目/清单和说明项。
若数据 Fixture、命令别名或 UI 层级发生变化，数量可能不同，但 JSON 必须始终合法。

继续：

```text
(lldb) breakpoint disable 1
(lldb) continue
```

测试还会继续执行 `WorkflowUI::action` 创建任务；最终应通过。

### 10.3 如果 Alfred 显示空列表或 JSON 错误

先检查：

```text
(lldb) frame variable output
(lldb) expression -- output.front()
(lldb) expression -- output.back()
(lldb) expression -- output.size()
```

再检查 `item_count`。若 `item_count == -1`，说明 `nlohmann::json::parse` 失败。

继续向调用栈上层：

```text
(lldb) frame select 1
(lldb) bt
```

也可以直接调试真实 CLI：

```bash
lldb build/Stacks_Tools
```

```text
(lldb) settings set target.env-vars STACKS_LOGGING=0
(lldb) breakpoint set --name WorkflowUI::filter
(lldb) run ui ""
```

注意：CLI 会从 `argv[0]` 和当前目录发现运行根。正式排查 Alfred 时，应使用安装包
中的 `Stacks_Tools` 和 workflow 根目录；单元测试更适合先隔离业务问题。

实际运行日志：

```text
tests/logs/lldb-debug-3-workflow-filter.log
```

## 11. GTest 调试技巧

只运行一个测试：

```bash
build/static_modules_tests \
  --gtest_filter=Boundary.NormalizeDateAndWeekBounds
```

在 LLDB 中运行同一个测试：

```text
(lldb) run --gtest_filter=Boundary.NormalizeDateAndWeekBounds
```

测试失败时立即停住：

```text
(lldb) run --gtest_filter=Boundary.NormalizeDateAndWeekBounds \
  --gtest_break_on_failure
```

列出全部测试：

```bash
build/static_modules_tests --gtest_list_tests
```

保留失败测试的空白工作区：

```bash
KEEP_STACKS_TEST_WORKSPACE=1 \
  build/static_modules_tests \
  --gtest_filter=Boundary.WeekDueAggregatesMultipleLibrariesAndExcludesDone
```

测试输出会打印：

```text
[TEST WORKSPACE] /.../tests/.runtime/unit-...
```

该目录可以直接检查项目文件、任务文件、Project.json、Stack.json 和 Timers.json。

在 LLDB 中设置环境变量：

```text
(lldb) settings set target.env-vars KEEP_STACKS_TEST_WORKSPACE=1
```

## 12. 调试真实 CLI 与数据安全

先使用空白测试数据：

```bash
lldb build/Stacks_Tools
```

```text
(lldb) settings set target.env-vars STACKS_LOGGING=0
(lldb) breakpoint set --name TaskManager::create_task
(lldb) run boards
```

调试 `ui`：

```text
(lldb) run ui ""
```

调试 action：

```text
(lldb) run action "reload-all"
```

不要直接在真实 `Read - Code`/业务工作区上测试写接口。真实数据调试应使用：

```bash
just real-data-smoke "/path/to/StacksLibrary"
```

该脚本先复制到 `tests/.runtime/real-data`，再执行 create/edit/done/reopen/timer/
inbox/delete。需要检查真实数据时，在脚本创建的副本上继续用 LLDB。

## 13. C++ 模块调试注意事项

本项目使用 C++26 模块和静态模块库：

```text
import std;
import InterfaceLog;
import TaskManager;
import WorkflowUI;
```

LLDB 已能显示模块符号，例如：

```text
TaskManager::generate_id@TaskManager()
WorkflowUI::filter@WorkflowUI(...)
```

建议：

- 第一轮使用 `Debug`，不要用 Release；
- 函数名断点优先于入口行断点；
- 检查返回值时，在 `return` 前一行设置断点；
- 行表偶尔会在多行表达式附近前后跳动，配合 `source list` 和 `bt` 确认位置；
- LLDB 无法显示某个 STL/模块类型时，先用 `expression` 调用方法，例如
  `id.size()`、`output.substr(0, 240)`；
- 不再把构建目录中的标准库模块 BMI 当成业务源码调试；std 由 CMake 4.5 原生构建。

## 14. 崩溃与异常排查

崩溃时先查看调用栈：

```text
(lldb) bt all
(lldb) thread list
(lldb) frame variable --show-all-children
```

查看当前线程寄存器：

```text
(lldb) register read
```

查看反汇编：

```text
(lldb) disassemble --frame
(lldb) disassemble --mixed --frame
```

查看源码和当前位置：

```text
(lldb) source list
(lldb) frame info
```

如果异常发生在 C++ `std::expected` 错误路径，不要只看异常断点。项目故意用
`std::expected` 返回可恢复错误，因此应断在：

```text
(lldb) breakpoint set --name TaskManager::task_error_message
```

或在具体接口的 `return std::unexpected(...)` 行检查 error 枚举。

## 15. 常用 LLDB 命令速查

| 目的 | 命令 |
| --- | --- |
| 加载程序 | `lldb build/static_modules_tests` |
| 启动 | `run` / `r` |
| 继续 | `continue` / `c` |
| 下一行 | `next` / `n` |
| 单步进入 | `step` / `s` |
| 返回当前函数 | `finish` |
| 调用栈 | `bt` / `thread backtrace` |
| 所有线程栈 | `bt all` |
| 局部变量 | `frame variable` / `v` |
| 单变量 | `frame variable id` |
| 表达式 | `expression -- id.size()` / `p id.size()` |
| 修改变量 | `expression -- value = 6` |
| 源码 | `source list` |
| 文件行断点 | `breakpoint set -f File.cppm -l 100` |
| 函数断点 | `breakpoint set -n TaskManager::generate_id` |
| 条件断点 | `breakpoint set ... -c 'condition'` |
| 断点列表 | `breakpoint list` |
| 禁用/删除断点 | `breakpoint disable 1` / `breakpoint delete 1` |
| 线程列表 | `thread list` |
| 切线程 | `thread select N` |
| 当前帧 | `frame info` |
| 上/下帧 | `up` / `down` |
| 退出 | `quit` |

## 16. 三个实验的批处理复现

### 16.1 ID 生成

```bash
lldb -b build/static_modules_tests \
  -o 'breakpoint set --file TaskManager.cppm --line 1024' \
  -o 'run --gtest_filter=Boundary.GenerateIdFixedLengthAndAlphabet' \
  -o 'bt 4' \
  -o 'frame variable id' \
  -o 'expression -- id.size()' \
  -o 'breakpoint disable 1' \
  -o continue \
  -o quit
```

关键输出：

```text
(std::string) id = "..."
(std::basic_string<char>::size_type) $0 = 20
```

### 16.2 周边界

```bash
lldb -b build/static_modules_tests \
  -o 'breakpoint set --file TaskManager.cppm --line 1919' \
  -o 'run --gtest_filter=Boundary.NormalizeDateAndWeekBounds' \
  -o 'frame variable anchor parsed weekday_value monday_based monday' \
  -o 'breakpoint disable 1' \
  -o continue \
  -o quit
```

关键输出：

```text
anchor = "2026-11-01"
weekday_value = 0
monday_based = 6
monday = date=2026-10-26Z
```

### 16.3 Script Filter

```bash
lldb -b build/static_modules_tests \
  -o 'breakpoint set --file WorkflowUI.cppm --line 1418' \
  -o 'run --gtest_filter=Boundary.WorkflowUiEmitsValidAlfredJsonAndExecutesCreateAction' \
  -o 'frame variable raw_query output item_count' \
  -o 'expression -- output.substr(0, 240)' \
  -o 'breakpoint disable 1' \
  -o continue \
  -o quit
```

关键输出：

```text
raw_query = ""
item_count = 12
output = "{\"items\":[...]}"
```

## 17. 调试完成后

退出 LLDB：

```text
(lldb) quit
```

查看调试日志：

```bash
ls -lh tests/logs/lldb-debug-*.log
```

清理 Debug 构建：

```bash
just clean
```

重新运行正式测试：

```bash
just test
just mutation-test
just smoke
```

## 18. 排查清单

遇到断点不命中时，按顺序检查：

1. 是否使用了 `Debug` 构建；
2. 可执行文件是否就是刚构建的那个；
3. `image lookup -rn 函数名` 是否有符号；
4. GTest 过滤器是否正确；
5. 是否命中了错误进程/错误参数；
6. C++ 模块代码是否在 `@ModuleName` 符号中；
7. 是否被优化、内联或源代码行号已经变化；
8. 是否有条件断点把命中过滤掉了。

遇到变量值异常时：

1. 确认该变量所在行已经执行；
2. 在 `return` 前一行重新设断点；
3. 尝试 `expression -- variable_name`；
4. 对容器改用 size()、下标访问和 substr() 等方法；
5. 检查是否是未初始化值，而不是最终业务值。

遇到 Alfred 实际运行与测试不一致时：

1. 先确认安装的是最新二进制；
2. 检查 `STACKS_WORKFLOW_DIR`；
3. 开启 `STACKS_LOGGING` 和 `STACKS_DEBUG`；
4. 对照 `log/trace-YYYYMMDD.log`；
5. 用 LLDB 断在 `WorkflowUI::filter`/`action`，分别观察 argv 和 action token；
6. 最后再回到真实工作区副本做写入验证。
