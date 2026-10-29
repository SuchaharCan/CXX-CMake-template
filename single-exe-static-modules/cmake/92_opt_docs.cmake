# =============================================================================
# 92_opt_docs.cmake — 可选文档（clang-doc 与 Doxygen 双后端，可同时启用）
# 分层: opt（可选，手动开启）
# 独立: 是
# 下游: 10_core_setup.cmake（自注册 finish 钩子 cxx_register_docs_targets）
# 启用: cmake .. -DCXX_ENABLE_DOCS=ON
#       [-DCXX_DOC_CLANGDOC=OFF] [-DCXX_DOC_DOXYGEN=OFF]
# 目标: docs（聚合）| docs-clangdoc（LLVM clang-doc）| docs-doxygen
# =============================================================================

include_guard(GLOBAL)

option(CXX_ENABLE_DOCS "Generate API docs targets (clang-doc / Doxygen)" OFF)
option(CXX_DOC_CLANGDOC "clang-doc backend (LLVM, reads compile_commands.json)" ON)
option(CXX_DOC_DOXYGEN "Doxygen backend (docs/Doxyfile.in)" ON)

# -----------------------------------------------------------------------------
# cxx_register_docs_targets()
# 作用：CXX_ENABLE_DOCS=ON 时注册 docs / docs-clangdoc / docs-doxygen。
# -----------------------------------------------------------------------------
function(cxx_register_docs_targets)
  if(NOT CXX_ENABLE_DOCS)
    return()
  endif()

  set(_docs_deps "")

  # ---- clang-doc（LLVM 自带，消费 compile_commands.json）----
  if(CXX_DOC_CLANGDOC)
    set(_paths)
    if(CXX_LLVM_BIN)
      list(APPEND _paths "${CXX_LLVM_BIN}")
    endif()
    find_program(_clang_doc NAMES clang-doc PATHS ${_paths} NO_DEFAULT_PATH)
    if(NOT _clang_doc)
      find_program(_clang_doc clang-doc)
    endif()
    # compile_commands.json 由 Ninja 在首次构建时生成，故此处只检查工具本身
    if(_clang_doc)
      add_custom_target(docs-clangdoc
        COMMAND "${_clang_doc}" --executor=all-TUs --format=html
          "--output=${CMAKE_BINARY_DIR}/docs/clang-doc"
          "${CMAKE_BINARY_DIR}/compile_commands.json"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "clang-doc → ${CMAKE_BINARY_DIR}/docs/clang-doc"
        VERBATIM)
      list(APPEND _docs_deps docs-clangdoc)
      cxx_msg(grass "docs-clangdoc target (LLVM clang-doc)")
    else()
      cxx_msg(yellow "clang-doc skipped (brew install llvm)")
    endif()
  endif()

  # ---- Doxygen（docs/Doxyfile.in）----
  if(CXX_DOC_DOXYGEN)
    find_package(Doxygen QUIET)
    if(NOT DOXYGEN_FOUND)
      find_program(DOXYGEN_EXECUTABLE NAMES doxygen)
    endif()
    set(_doxy_in "${CMAKE_SOURCE_DIR}/docs/Doxyfile.in")
    set(_doxy_out "${CMAKE_BINARY_DIR}/docs/Doxyfile")
    set(_doxy_html "${CMAKE_BINARY_DIR}/docs/doxygen/html")
    if(DOXYGEN_EXECUTABLE AND EXISTS "${_doxy_in}")
      file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/docs")
      configure_file("${_doxy_in}" "${_doxy_out}" @ONLY)
      add_custom_target(docs-doxygen
        COMMAND "${DOXYGEN_EXECUTABLE}" "${_doxy_out}"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/docs"
        COMMENT "Doxygen → ${_doxy_html}"
        VERBATIM)
      list(APPEND _docs_deps docs-doxygen)
      cxx_msg(grass "docs-doxygen target (Doxygen)")
    else()
      cxx_msg(yellow "Doxygen skipped (brew install doxygen; need docs/Doxyfile.in)")
    endif()
  endif()

  # ---- 聚合目标 docs ----
  if(_docs_deps)
    add_custom_target(docs DEPENDS ${_docs_deps}
      COMMENT "Generate API docs: ${_docs_deps}")
    cxx_msg(grass "docs target → ${_docs_deps}")
  else()
    cxx_msg(yellow "docs: no backend available (clang-doc / doxygen)")
  endif()
endfunction()

cxx_register_finish_hook(cxx_register_docs_targets)
