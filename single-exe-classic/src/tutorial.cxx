/**
 * @file tutorial.cxx
 * @brief step1 主程序：读命令行数值、调用 TutorialMath 求平方根并打印。
 *
 * 运行日志经 ScenarioLog 写入 log/exe.log（【不可删除】见 ScenarioLog.hxx/.cxx）。
 */

/**
 * \mainpage CMakeTutorialStep1 — 单可执行程序模板演示
 *
 * 本页由 `just docs`（clang-doc + Doxygen）自动生成，展示模板内置的
 * 注释文档自动化能力：
 * - TutorialMath：有限域平方根与命令行参数校验（纯函数，便于 gtest 覆盖）
 * - ModernParadigm：强类型 / std::expected / std::span / concept 现代范式
 * - ScenarioLog：spdlog 场景日志封装（exe / gtest / lldb 三场景 + 日志裁剪）
 * - 本文件：值传递 vs 引用传递、std::hash 特化、依赖注入、std::lcm/gcd 演示
 */

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <numeric>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "CMakeTutorialConfig.hxx"
#include "ScenarioLog.hxx"
#include "TutorialMath.hxx"

using scenario_log::log_error;
using scenario_log::log_info;
using scenario_log::log_sep;
using std::lcm;

// 验证 值传递 引用传递 常量引用传递之间的区别

/** @brief 按值传递：形参修改不影响实参。 @param x 传入值（副本） */
void passByValue(int x) {
  x += 10;
  std::cout << "函数内的值 （按值传递)：" << x << std::endl;
}

/** @brief 按引用传递：函数内修改直接作用于实参。 @param x 传入引用 */
void passByReference(int& x) {
  x += 10;
  std::cout << "函数内的值 （按引用传递): " << x << std::endl;
}

/** @brief 常量引用传递：只读访问，禁止修改（编译器强制）。 @param value 只读引用 */
void modifyValue(const int& value) {
  // value can't; modify
  std::cout << "函数内的值 （按常量引用传递): " << value << std::endl;
}

/**
 * @brief 人员结构 + std::hash 特化演示（自定义类型可入无序容器）。
 */
struct hash_Person {
  std::string name;
  int age;  // int前面不能添加std
  // 用于关联容器的比较运算符
  bool operator==(const hash_Person& other) const { return name == other.name && age == other.age; }

  bool operator!=(const hash_Person& other) const {
    return !(*this == other);  // ⚠️ 此处语法待理解 和 注释
  }
};
// DI 依赖注入 创建 和 使用 分离
/** @brief 依赖注入演示：输出接口（构造函数注入进 di_MessageService）。 */
class di_Printer {
 public:
  void print(const std::string& message) { std::cout << "Printing: " << message << std::endl; }
};

/** @brief 依赖注入演示：持有 di_Printer 引用，不自行创建依赖。 */
class di_MessageService {
 private:
  di_Printer& printer;

 public:
  di_MessageService(di_Printer& printer) : printer(printer) {}
  void sendMessage(const std::string& message) { printer.print(message); }
};

// ===================================================std -
// Start=========================================
namespace std {
template <>
struct hash<hash_Person> {
  std::size_t operator()(const hash_Person& p) const {
    std::size_t nameHash = std::hash<std::string>{}(p.name);
    std::size_t ageHash = std::hash<int>{}(p.age);
    return nameHash ^ ageHash;
  }
};
}  // namespace std
// ===================================================std -
// End=========================================

/**
 * @brief 程序入口：校验参数 → compute_sqrt → 打印 CMake 注入的宏与版本信息。
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组（argv[1] 为待开平方的数值）
 * @return 0 成功；1 缺少参数；2 输入非法（负数 / 超出上限）
 */
int main(int argc, char* argv[]) {
  log_sep();
  log_info("step1 starting");

  if (!tutorial::has_numeric_argument(argc)) {
    log_error(std::string("Usage: ") + argv[0] + " number");

    // 演示 configure_file / project() 注入的宏与版本号
    std::cout << "宏定义_plusplus查看CXX语言版本:  " << __cplusplus << "\n";
    std::cout << "项目版本主版本号CMakeTutorial_VERSION_MAJOR: " << CMakeTutorialStep1_VERSION_MAJOR
              << "\n";
    std::cout << "项目版本次版本号CMakeTutorial_VERSION_MINOR: " << CMakeTutorialStep1_VERSION_MINOR
              << "\n";
    std::cout << "STR_TEST: " STR_TEST << "\n";
    std::cout << "测试是否支持 中文 字符显示 || 有参 和无参数 两种调用方式\n";

    log_sep();
    return 1;
  }

  char* end = nullptr;
  const double inputValue = std::strtod(argv[1], &end);  // 解析 argv[1] 为 double
  log_info("computing sqrt of " + std::to_string(inputValue));

  const double outputValue = tutorial::compute_sqrt(inputValue);
  if (outputValue < 0.0) {
    log_error("negative input is not supported");
    log_sep();
    return 2;
  }

  std::cout << "The square root of " << inputValue << " is " << outputValue << '\n';
  log_info("result = " + std::to_string(outputValue));

#if defined(CXX_TUTORIAL_DEBUG_TRACE) && CXX_TUTORIAL_DEBUG_TRACE
  log_info("CXX_TUTORIAL_DEBUG_TRACE is enabled");
#endif

  []() { std::cout << "Test this []()\n"; }();  // 演示 lambda 语法

  // ------------- std::lcm-----------
  // 计算两个数的最小公倍数（LCM）。这在事件同步、分数处理和数学计算任务中非常有用。
  int lcm_a = 12;
  int lcm_b = 18;
  int lcm_result = std::lcm(lcm_a, lcm_b);

  std::cout << "1️⃣HOK(" << lcm_a << ", " << lcm_b << ") = " << lcm_result
            << '\n';  // HOK(12, 18) = 36

  // -------------std::gcd-----------------------
  // Std::gcd计算两个数字的最大公除数（GDI）。这对于可除性问题、分数还原和数值算法很有用。
  int gcd_a = 84;
  int gcd_b = 30;
  int gcd_result = std::gcd(gcd_a, lcm_b);

  std::cout << "2️⃣HOK(" << gcd_a << ", " << gcd_b << ") = " << gcd_result
            << '\n';  // HOK(12, 18) = 36

  // --------------std::iota----------------
  // std::iota — 算法来自库 <numeric>，它用从指定起始数值开始递增1的数值填充一个范围。
  // 这在生成简单的数字序列、索引、行号、测试数据时很有用，无需手动循环。

  std::vector<int> iota_numbers(10);

  std::iota(iota_numbers.begin(), iota_numbers.end(), 100);
  std::cout << "3️⃣数字序列： \n";
  for (int n : iota_numbers) {
    std::cout << n << ' ';
  }
  std::cout << '\n';

  // --------------std::iota----------------
  // std::exchange 将变量的值替换为新值并返回旧值。这在重置状态、移动值和编写原子操作时很有用。
  int exchange_status = 1;
  int exchange_old = std::exchange(exchange_status, 0);
  std::cout << "Before: " << exchange_old << '\n';
  std::cout << "After: " << exchange_status << '\n';

  // Std::clamp在指定范围内限制值。这对于验证数字、规范化数据和防止出国很有用。

  int clamp_value = 150;
  int clamp_result = std::clamp(clamp_value, 0, 100);
  std::cout << "有限价值 : " << clamp_result << '\n';
  // 思考 值传递问题
  // 在C++中，函数参数可以通过值、引用和常量引用传递。这些方法会影响性能和安全性。让我们弄清楚它是如何工作的。
  // •按值转移：创建对象的副本，对于大型对象来说，这可能很昂贵。
  // •通过引用传输：将引用传输到对象，避免复制。
  // •通过恒定引用进行传输：增加对函数内对象更改的保护。

  int passBy = 5;
  std::cout << "\n⭐值传递，引用传递 常量引用传递前 值：" << passBy << std::endl;
  passByValue(passBy);
  std::cout << "函数调用后的值（按值传递 value）：" << passBy << std::endl;
  passByReference(passBy);
  std::cout << "函数调用后的值 （按引用传递 ref）：" << passBy << std::endl;
  modifyValue(passBy);
  std::cout << "函数调用后的值 （按常量引用传递 const ref）：" << passBy << std::endl;

  // 重载std::hash
  hash_Person person{"John Doe", 25};
  std::hash<hash_Person> personHasher;           // 为hash_Perso类型创建一个哈希函数的实例
  std::size_t hashValue = personHasher(person);  // 计算哈希值
  std::cout << "⚙️ 1️⃣Hash value: " << hashValue << std::endl;

  // DI
  //  依赖注入（DI）是一种设计模式，它允许管理对象之间的依赖关系。它有助于将对象的创建与使用分离，并提供更为灵活和可测试的程序架构。
  //  在DI中，对象不会直接获取自己的依赖，而是通过一个外部源来提供。这个源称为依赖注入容器。容器负责创建和管理依赖，对象则通过构造函数、方法或属性来获取它们。
  di_Printer di_Printer;
  di_MessageService di_MessageService(di_Printer);
  di_MessageService.sendMessage("Hello, World!");
  log_sep();
  log_info("step1 finished ok");
  return 0;
}
