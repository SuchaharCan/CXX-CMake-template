# =============================================================================
# 00_core_lint.cmake — 语法规范门禁（警告探测 + clang-tidy + clang-format）
# 分层: core（必选）
# 独立: 否
# 上游: 00_core_compiler.cmake（CXX_LLVM_BIN 定位工具）
#       00_core_color.cmake（cxx_msg，lint 消息统一 purple=语法检测）
#       00_core_buildlog.cmake（cxx_build_log_echo_commands 门禁提示）
# 下游: 10_core_setup.cmake（cxx_apply_template_defaults / cxx_phase_finish）
# 功能:
#   - 编译警告候选探测与应用（cxx_filter_supported_warning_flags）
#   - CXX_ENABLE_CLANG_TIDY / CXX_ENABLE_CLANG_FORMAT（默认 ON）
#   - CXX_SAFETY_LINT / CXX_ENABLE_INCLUDE_CLEANER 选择 .clang-tidy 配置
#   - 注册 syntax-check / tidy-check 手动目标
# =============================================================================

include_guard(GLOBAL)

# flag → 中文作用说明（阶段 3 Performing Test 使用）
set(_CXX_FLAG_HELP_Wall "启用大量常见问题警告")
set(_CXX_FLAG_HELP_Wextra "启用额外常见警告")
set(_CXX_FLAG_HELP_Weffc__ "Effective C++ 风格建议（多见于 GCC）")
set(_CXX_FLAG_HELP_Wpedantic "按 ISO 标准严格检查（非标准扩展告警）")
set(_CXX_FLAG_HELP_Werror_uninitialized "使用未初始化变量时视为错误")
set(_CXX_FLAG_HELP_Werror_return_type "有返回值函数缺少 return 时视为错误")
set(_CXX_FLAG_HELP_Wconversion "隐式类型转换告警（金融数值敏感）")
set(_CXX_FLAG_HELP_Wsign_compare "有符号/无符号比较告警")
set(_CXX_FLAG_HELP_Wsign_conversion "有符号/无符号隐式转换告警")
set(_CXX_FLAG_HELP_Werror_unused_result "忽略带 warn_unused_result 的返回值时视为错误")
set(_CXX_FLAG_HELP_Werror_suggest_override "建议对虚函数加 override 时视为错误")
set(_CXX_FLAG_HELP_Wzero_as_null_pointer_constant "用 0 当空指针告警（应使用 nullptr）")
set(_CXX_FLAG_HELP_Wmissing_declarations "缺少前置声明告警")
set(_CXX_FLAG_HELP_Wold_style_cast "旧式 C 强制转换告警")
set(_CXX_FLAG_HELP_Werror_vla "变长数组视为错误")
set(_CXX_FLAG_HELP_Wnon_virtual_dtor "多态基类析构非虚告警")
set(_CXX_FLAG_HELP_Wdouble_promotion "float 隐式提升为 double 告警")
set(_CXX_FLAG_HELP_Wformat_2 "printf/scanf 格式串增强检查")
set(_CXX_FLAG_HELP_Woverloaded_virtual "隐藏基类虚函数告警")
set(_CXX_FLAG_HELP_Wshadow "变量遮蔽告警")
set(_CXX_FLAG_HELP_Wunused "未使用实体告警")
set(_CXX_FLAG_HELP_Wimplicit_fallthrough "switch 隐含 fallthrough 告警")
set(_CXX_FLAG_HELP_Wnull_dereference "空指针解引用倾向告警")
set(_CXX_FLAG_HELP_Wduplicated_cond "重复条件告警（GCC）")
set(_CXX_FLAG_HELP_Wduplicated_branches "重复分支告警（GCC）")
set(_CXX_FLAG_HELP_Wlogical_op "可疑逻辑运算告警（GCC）")
set(_CXX_FLAG_HELP_Wrestrict "restrict 别名违规告警（GCC）")
set(_CXX_FLAG_HELP_Wformat_security "格式串安全告警（Clang）")
set(_CXX_FLAG_HELP_Wstack_protector "建议栈保护相关告警（Clang）")
set(_CXX_FLAG_HELP_Wvla "变长数组告警（Clang）")
set(_CXX_FLAG_HELP_Warray_bounds "数组越界倾向告警（Clang）")
set(_CXX_FLAG_HELP_Wuninitialized "未初始化使用告警（Clang）")
set(_CXX_FLAG_HELP_Wconditional_uninitialized "条件路径未初始化告警（Clang）")

option(CXX_ENABLE_CLANG_TIDY
       "Enable clang-tidy (compile attach + tidy-check target)" ON)
option(CXX_ENABLE_CLANG_FORMAT
       "Enable clang-format (PRE_BUILD gate + syntax-check target)" ON)
option(CXX_SAFETY_LINT
       "JPL/CERT-inspired clang-tidy profile (NOT MISRA/DO-178C certified)" OFF)

set(CXX_CLANG_TIDY_CONFIG_FILE "" CACHE FILEPATH "Active clang-tidy config" FORCE)

# -----------------------------------------------------------------------------
# cxx_flag_help(flag) → 设置 _cxx_flag_help_text
# -----------------------------------------------------------------------------
function(cxx_flag_help flag)
  string(REGEX REPLACE "^-W" "W" _key "${flag}")
  string(REGEX REPLACE "[=.+-]" "_" _key "${_key}")
  if(DEFINED _CXX_FLAG_HELP_${_key})
    set(_cxx_flag_help_text "${_CXX_FLAG_HELP_${_key}}" PARENT_SCOPE)
  else()
    set(_cxx_flag_help_text "编译器警告/选项探测" PARENT_SCOPE)
  endif()
endfunction()

# -----------------------------------------------------------------------------
# cxx_probe_one_flag(flag result_var)
# 作用：try_compile 探测；打印「Performing Test …」与彩色 Success/Failed。
# -----------------------------------------------------------------------------
function(cxx_probe_one_flag flag result_var)
  cxx_flag_help("${flag}")
  string(MAKE_C_IDENTIFIER "CXX_SUPPORTS_${flag}" _cv)

  if(DEFINED CACHE{${_cv}})
    cxx_msg(info "Performing Test ${_cv} — ${_cxx_flag_help_text}")
    if(${_cv})
      cxx_msg(grass "Performing Test ${_cv} - Success")
      set(${result_var} TRUE PARENT_SCOPE)
    else()
      cxx_msg(error "Performing Test ${_cv} - Failed")
      set(${result_var} FALSE PARENT_SCOPE)
    endif()
    return()
  endif()

  cxx_msg(info "Performing Test ${_cv} — ${_cxx_flag_help_text}")

  set(_src "${CMAKE_BINARY_DIR}/CMakeFiles/cxx_flag_probe_${_cv}.cpp")
  set(_bindir "${CMAKE_BINARY_DIR}/CMakeFiles/cxx_flag_probe_${_cv}_dir")
  file(WRITE "${_src}" "int main(){return 0;}\n")
  try_compile(${_cv}
    "${_bindir}"
    "${_src}"
    CMAKE_FLAGS "-DCMAKE_CXX_FLAGS=${flag}"
    OUTPUT_VARIABLE _out
  )

  if(${_cv})
    cxx_msg(grass "Performing Test ${_cv} - Success")
    set(${_cv} TRUE CACHE INTERNAL "Support for ${flag}")
    set(${result_var} TRUE PARENT_SCOPE)
  else()
    if(_out STREQUAL "")
      cxx_msg(warning "Performing Test ${_cv} - Anomaly（无编译器输出）")
    else()
      cxx_msg(error "Performing Test ${_cv} - Failed")
    endif()
    set(${_cv} FALSE CACHE INTERNAL "Support for ${flag}")
    set(${result_var} FALSE PARENT_SCOPE)
  endif()
endfunction()

# -----------------------------------------------------------------------------
# cxx_filter_supported_warning_flags(out_var flag1 flag2 ...)
# 作用：批量探测，仅保留支持的 flag。
# -----------------------------------------------------------------------------
function(cxx_filter_supported_warning_flags out_var)
  set(_ok "")
  foreach(_flag IN LISTS ARGN)
    cxx_probe_one_flag("${_flag}" _supported)
    if(_supported)
      list(APPEND _ok "${_flag}")
    endif()
  endforeach()
  set(${out_var} "${_ok}" PARENT_SCOPE)
endfunction()

# -----------------------------------------------------------------------------
# cxx_apply_warnings(target)
# 作用：探测并注入 -Wall/-Wextra 等警告；不支持的 flag 自动跳过。
# -----------------------------------------------------------------------------
function(cxx_apply_warnings target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "cxx_apply_warnings: target '${target}' does not exist")
  endif()

  set(_candidates
      -Wall
      -Wextra
      -Weffc++
      -Werror=uninitialized
      -Werror=return-type
      -Wconversion
      -Wsign-compare
      -Werror=unused-result
      -Werror=suggest-override
      -Wzero-as-null-pointer-constant
      -Wmissing-declarations
      -Wold-style-cast
      -Werror=vla
      -Wnon-virtual-dtor
      -Wpedantic
      -Wsign-conversion
      -Wdouble-promotion
      -Wformat=2
      -Woverloaded-virtual
      -Wshadow
      -Wunused
      -Wimplicit-fallthrough
      -Wnull-dereference
  )

  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    list(APPEND _candidates
        -Wduplicated-cond
        -Wduplicated-branches
        -Wlogical-op
        -Wrestrict
    )
  endif()

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    list(APPEND _candidates
        -Wformat-security
        -Wstack-protector
        -Warray-bounds
        -Wconditional-uninitialized
    )
  endif()

  cxx_filter_supported_warning_flags(_warnings ${_candidates})
  if(_warnings)
    target_compile_options(${target} PRIVATE ${_warnings})
    list(LENGTH _warnings _n)
    cxx_msg(lint "warnings applied to ${target} (${_n} flags)")
  endif()
endfunction()

# -----------------------------------------------------------------------------
# cxx_resolve_clang_tidy_config()
# 作用：按 CXX_SAFETY_LINT / CXX_ENABLE_INCLUDE_CLEANER 选择 .clang-tidy 配置。
# -----------------------------------------------------------------------------
function(cxx_resolve_clang_tidy_config)
  set(_base "${CMAKE_SOURCE_DIR}/cmake/.clang-tidy")
  set(_safety "${CMAKE_SOURCE_DIR}/cmake/.clang-tidy.safety")
  set(_cleaner "${CMAKE_SOURCE_DIR}/cmake/.clang-tidy.include-cleaner")

  if(CXX_SAFETY_LINT AND EXISTS "${_safety}")
    set(_cfg "${_safety}")
    cxx_msg(lint "clang-tidy profile: SAFETY (JPL-inspired; not certified)")
  elseif(DEFINED CXX_ENABLE_INCLUDE_CLEANER AND CXX_ENABLE_INCLUDE_CLEANER
         AND EXISTS "${_cleaner}")
    set(_cfg "${_cleaner}")
    cxx_msg(lint "clang-tidy profile: include-cleaner")
  else()
    set(_cfg "${_base}")
  endif()
  set(CXX_CLANG_TIDY_CONFIG_FILE "${_cfg}" CACHE FILEPATH "Active clang-tidy config" FORCE)
endfunction()

# -----------------------------------------------------------------------------
# cxx_apply_clang_tidy(target)
# 作用：CXX_ENABLE_CLANG_TIDY=ON 时设置 target 的 CXX_CLANG_TIDY 属性。
# -----------------------------------------------------------------------------
function(cxx_apply_clang_tidy target)
  if(NOT TARGET ${target})
    return()
  endif()
  if(CXX_USING_GCC)
    cxx_msg(gray "clang-tidy skipped for ${target}（GCC 工具链不使用 clang-tidy）")
    return()
  endif()
  if(NOT CXX_ENABLE_CLANG_TIDY)
    cxx_msg(gray "clang-tidy skipped for ${target} (CXX_ENABLE_CLANG_TIDY=OFF)")
    return()
  endif()
  set(_tidy_paths)
  if(CXX_LLVM_BIN)
    list(APPEND _tidy_paths "${CXX_LLVM_BIN}")
  endif()
  find_program(_CXX_CLANG_TIDY NAMES clang-tidy PATHS ${_tidy_paths} NO_DEFAULT_PATH)
  if(NOT _CXX_CLANG_TIDY)
    find_program(_CXX_CLANG_TIDY clang-tidy)
  endif()
  if(_CXX_CLANG_TIDY)
    if(CXX_CLANG_TIDY_CONFIG_FILE)
      set(_cfg "${CXX_CLANG_TIDY_CONFIG_FILE}")
    else()
      set(_cfg "${CMAKE_SOURCE_DIR}/cmake/.clang-tidy")
    endif()
    set_target_properties(${target} PROPERTIES
      CXX_CLANG_TIDY "${_CXX_CLANG_TIDY};--config-file=${_cfg}")
    cxx_msg(grass "clang-tidy: ON → ${target} (${_cfg})")
  else()
    cxx_msg(warning "clang-tidy: enabled but binary not found")
  endif()
endfunction()

# -----------------------------------------------------------------------------
# cxx_apply_clang_format_gate(target)
# 作用：CXX_ENABLE_CLANG_FORMAT=ON 时在 PRE_BUILD 对模板源做 format dry-run。
# -----------------------------------------------------------------------------
function(cxx_apply_clang_format_gate target)
  if(NOT TARGET ${target})
    return()
  endif()
  if(NOT CXX_ENABLE_CLANG_FORMAT)
    cxx_msg(gray "clang-format gate skipped for ${target} (OFF)")
    return()
  endif()
  set(_fmt_paths)
  if(CXX_LLVM_BIN)
    list(APPEND _fmt_paths "${CXX_LLVM_BIN}")
  endif()
  find_program(_CXX_CLANG_FORMAT NAMES clang-format PATHS ${_fmt_paths} NO_DEFAULT_PATH)
  if(NOT _CXX_CLANG_FORMAT)
    find_program(_CXX_CLANG_FORMAT clang-format)
  endif()
  set(_cfg "${CMAKE_SOURCE_DIR}/cmake/.clang-format")
  if(NOT _CXX_CLANG_FORMAT OR NOT EXISTS "${_cfg}")
    cxx_msg(warning "clang-format: enabled but tool/config missing")
    return()
  endif()
  if(DEFINED CXX_LINT_SOURCES AND CXX_LINT_SOURCES)
    set(_srcs ${CXX_LINT_SOURCES})
  else()
    set(_srcs
      "${CMAKE_SOURCE_DIR}/src/tutorial.cxx"
      "${CMAKE_SOURCE_DIR}/src/TutorialMath.cxx"
      "${CMAKE_SOURCE_DIR}/src/TutorialMath.hxx"
      "${CMAKE_SOURCE_DIR}/src/tutorial_gtest.cxx"
      "${CMAKE_SOURCE_DIR}/src/ScenarioLog.cxx"
      "${CMAKE_SOURCE_DIR}/src/ScenarioLog.hxx"
    )
  endif()
  cxx_build_log_echo_commands(_gate_echo "clang-format gate (${target})")
  add_custom_command(TARGET ${target} PRE_BUILD
    ${_gate_echo}
    COMMAND "${_CXX_CLANG_FORMAT}" --dry-run --Werror
      "--style=file:${_cfg}"
      ${_srcs}
    COMMENT ""
    VERBATIM)
  cxx_msg(grass "clang-format: ON → PRE_BUILD gate on ${target}")
endfunction()

# -----------------------------------------------------------------------------
# cxx_register_lint_targets()
# 作用：注册手动目标 syntax-check / tidy-check。
# -----------------------------------------------------------------------------
function(cxx_register_lint_targets)
  set(_tidy_paths)
  set(_fmt_paths)
  if(CXX_LLVM_BIN)
    list(APPEND _tidy_paths "${CXX_LLVM_BIN}")
    list(APPEND _fmt_paths "${CXX_LLVM_BIN}")
  endif()
  find_program(_CXX_CLANG_TIDY NAMES clang-tidy PATHS ${_tidy_paths} NO_DEFAULT_PATH)
  if(NOT _CXX_CLANG_TIDY)
    find_program(_CXX_CLANG_TIDY clang-tidy)
  endif()
  find_program(_CXX_CLANG_FORMAT NAMES clang-format PATHS ${_fmt_paths} NO_DEFAULT_PATH)
  if(NOT _CXX_CLANG_FORMAT)
    find_program(_CXX_CLANG_FORMAT clang-format)
  endif()

  if(DEFINED CXX_LINT_SOURCES AND CXX_LINT_SOURCES)
    set(_srcs ${CXX_LINT_SOURCES})
  else()
    set(_srcs
      "${CMAKE_SOURCE_DIR}/src/tutorial.cxx"
      "${CMAKE_SOURCE_DIR}/src/TutorialMath.cxx"
      "${CMAKE_SOURCE_DIR}/src/TutorialMath.hxx"
      "${CMAKE_SOURCE_DIR}/src/tutorial_gtest.cxx"
      "${CMAKE_SOURCE_DIR}/src/ScenarioLog.cxx"
      "${CMAKE_SOURCE_DIR}/src/ScenarioLog.hxx"
    )
  endif()
  set(_clang_format_cfg "${CMAKE_SOURCE_DIR}/cmake/.clang-format")
  if(CXX_CLANG_TIDY_CONFIG_FILE)
    set(_clang_tidy_cfg "${CXX_CLANG_TIDY_CONFIG_FILE}")
  else()
    set(_clang_tidy_cfg "${CMAKE_SOURCE_DIR}/cmake/.clang-tidy")
  endif()

  if(CXX_ENABLE_CLANG_FORMAT AND _CXX_CLANG_FORMAT AND EXISTS "${_clang_format_cfg}")
    cxx_build_log_echo_commands(_syn_echo "syntax-check (clang-format)")
    add_custom_target(syntax-check
      ${_syn_echo}
      COMMAND "${_CXX_CLANG_FORMAT}" --dry-run --Werror
        "--style=file:${_clang_format_cfg}"
        ${_srcs}
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      COMMENT ""
      VERBATIM)
    cxx_msg(grass "syntax-check target registered")
  elseif(CXX_ENABLE_CLANG_FORMAT)
    cxx_msg(warning "syntax-check not registered (clang-format or config missing)")
  endif()

  if(NOT CXX_USING_GCC AND CXX_ENABLE_CLANG_TIDY AND _CXX_CLANG_TIDY AND EXISTS "${_clang_tidy_cfg}")
    cxx_build_log_echo_commands(_tidy_echo "tidy-check (clang-tidy)")
    add_custom_target(tidy-check
      ${_tidy_echo}
      COMMAND "${_CXX_CLANG_TIDY}" -p "${CMAKE_BINARY_DIR}"
        "--config-file=${_clang_tidy_cfg}"
        ${_srcs}
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      COMMENT ""
      VERBATIM)
    cxx_msg(grass "tidy-check target registered")
  elseif(NOT CXX_USING_GCC AND CXX_ENABLE_CLANG_TIDY)
    cxx_msg(warning "tidy-check not registered (clang-tidy or config missing)")
  endif()
endfunction()
