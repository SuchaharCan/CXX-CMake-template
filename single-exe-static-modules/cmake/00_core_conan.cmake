# =============================================================================
# 00_core_conan.cmake — Conan 2 依赖检测（版本锁定的第三方库）
# 分层: core（必选）
# 独立: 是（仅依赖 00_core_color/00_core_log 的 cxx_msg）
# 上游: 00_core_compiler.cmake（CMAKE_PREFIX_PATH 已含 Homebrew 路径）
# 下游: 10_core_setup.cmake（bootstrap 中调用 cxx_conan_detect）
#       CMakeLists / 90_opt_gtest.cmake（消费 CXX_*_FOUND 与 imported target）
# 约定:
#   - brew 只装构建工具（cmake/just/ninja/clang/bqlog/conan 等）
#   - conan 负责需要锁定版本的第三方库（boost/abseil/nlohmann_json/asio/
#     eigen/sqlite_orm/opencv/gtest）
#   - conan install 生成的 CMakeDeps 目录默认是 ${CMAKE_BINARY_DIR}/conan
# =============================================================================

include_guard(GLOBAL)

# 使用源码树下的固定目录，保证不同 preset 的 binaryDir（build/debug、build/release
# 等）都能共享同一份 Conan CMakeDeps，无需为每个 preset 重复 install。
set(CXX_CONAN_GENERATED_DIR "${CMAKE_SOURCE_DIR}/build/conan" CACHE PATH
    "Conan CMakeDeps generated dir（conan install -of 的目标目录）")
option(CXX_ENABLE_CONAN "Discover Conan CMakeDeps packages" ON)

# 让 find_package 优先命中 Conan 生成的包，避免与 Homebrew 同名包混淆。
if(CXX_ENABLE_CONAN)
  list(PREPEND CMAKE_PREFIX_PATH "${CXX_CONAN_GENERATED_DIR}")
endif()

# Conan 默认只安装单一 Release 配置；将 Debug/RelWithDebInfo/MinSizeRel 统一
# 映射到 Release 配置，避免因 CMakeDeps 的 $<CONFIG:Release> 门控导致头文件/链接
# 目录在非 Release 构建下为空（也为重库避免重复编译 Debug 版本）。
set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG Release)
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL Release)
set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE Release)

find_program(CXX_CONAN_PROGRAM conan)

set(CXX_CONAN_VERSION "" CACHE STRING "conan version" FORCE)
set(CXX_NLOHMANN_JSON_FOUND OFF CACHE INTERNAL "" FORCE)
set(CXX_BOOST_FOUND OFF CACHE INTERNAL "" FORCE)
set(CXX_ABSL_FOUND OFF CACHE INTERNAL "" FORCE)
set(CXX_ASIO_FOUND OFF CACHE INTERNAL "" FORCE)
set(CXX_EIGEN_FOUND OFF CACHE INTERNAL "" FORCE)
set(CXX_SQLITE_ORM_FOUND OFF CACHE INTERNAL "" FORCE)
set(CXX_OPENCV_FOUND OFF CACHE INTERNAL "" FORCE)

# -----------------------------------------------------------------------------
# cxx_conan_detect() — 检测 conan 与各依赖库，登记 FOUND/版本/imported target
# -----------------------------------------------------------------------------
function(cxx_conan_detect)
  if(NOT CXX_ENABLE_CONAN)
    cxx_msg(yellow "conan: disabled (CXX_ENABLE_CONAN=OFF)")
    return()
  endif()

  # 清除上一次 find_package 缓存，避免重新配置时沿用 Homebrew 的旧 *_DIR 路径。
  foreach(_pkg IN ITEMS nlohmann_json Boost absl asio Eigen3 sqlite_orm OpenCV)
    unset(${_pkg}_DIR CACHE)
    unset(${_pkg}_FOUND CACHE)
  endforeach()

  if(CXX_CONAN_PROGRAM)
    execute_process(COMMAND "${CXX_CONAN_PROGRAM}" --version
      OUTPUT_VARIABLE _conan_ver OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    string(REGEX REPLACE "^Conan version " "" _conan_ver "${_conan_ver}")
    set(CXX_CONAN_VERSION "${_conan_ver}" CACHE STRING "conan version" FORCE)
    cxx_msg(blue "conan ${_conan_ver} → ${CXX_CONAN_GENERATED_DIR}")
  else()
    cxx_msg(error "conan not found — brew install conan")
  endif()

  # 以下第三方库严格从 Conan CMakeDeps 目录解析（NO_DEFAULT_PATH），
  # 避免被 Homebrew 安装的同名库（boost/eigen/opencv/...）抢先命中。
  # nlohmann_json（项目 JSON 唯一来源；缺失则阻断配置）
  find_package(nlohmann_json CONFIG QUIET
    PATHS "${CXX_CONAN_GENERATED_DIR}" NO_DEFAULT_PATH)
  if(TARGET nlohmann_json::nlohmann_json)
    set(CXX_NLOHMANN_JSON_FOUND ON CACHE INTERNAL "" FORCE)
    cxx_msg(grass "nlohmann_json ${nlohmann_json_VERSION}")
  else()
    cxx_msg(error "nlohmann_json not found — 先运行 just conan")
  endif()

  # boost（header_only 模式，仅头文件）
  find_package(Boost CONFIG QUIET
    PATHS "${CXX_CONAN_GENERATED_DIR}" NO_DEFAULT_PATH)
  if(TARGET Boost::headers)
    set(CXX_BOOST_FOUND ON CACHE INTERNAL "" FORCE)
    cxx_msg(grass "boost ${Boost_VERSION} (header_only)")
  else()
    cxx_msg(gray "boost: not found (conan)")
  endif()

  # abseil（absl 包名）
  find_package(absl CONFIG QUIET
    PATHS "${CXX_CONAN_GENERATED_DIR}" NO_DEFAULT_PATH)
  if(TARGET absl::strings)
    set(CXX_ABSL_FOUND ON CACHE INTERNAL "" FORCE)
    cxx_msg(grass "abseil ${absl_VERSION}")
  else()
    cxx_msg(gray "abseil: not found (conan)")
  endif()

  # asio（standalone，header-only）
  find_package(asio CONFIG QUIET
    PATHS "${CXX_CONAN_GENERATED_DIR}" NO_DEFAULT_PATH)
  if(TARGET asio::asio)
    set(CXX_ASIO_FOUND ON CACHE INTERNAL "" FORCE)
    cxx_msg(grass "asio ${asio_VERSION}")
  else()
    cxx_msg(gray "asio: not found (conan)")
  endif()

  # eigen
  find_package(Eigen3 CONFIG QUIET
    PATHS "${CXX_CONAN_GENERATED_DIR}" NO_DEFAULT_PATH)
  if(TARGET Eigen3::Eigen)
    set(CXX_EIGEN_FOUND ON CACHE INTERNAL "" FORCE)
    cxx_msg(grass "eigen ${Eigen3_VERSION}")
  else()
    cxx_msg(gray "eigen: not found (conan)")
  endif()

  # sqlite_orm
  find_package(sqlite_orm CONFIG QUIET
    PATHS "${CXX_CONAN_GENERATED_DIR}" NO_DEFAULT_PATH)
  if(TARGET sqlite_orm::sqlite_orm)
    set(CXX_SQLITE_ORM_FOUND ON CACHE INTERNAL "" FORCE)
    cxx_msg(grass "sqlite_orm ${sqlite_orm_VERSION}")
  else()
    cxx_msg(gray "sqlite_orm: not found (conan)")
  endif()

  # opencv（core/imgcodecs/imgproc；重依赖已在 conanfile 关闭）
  find_package(OpenCV CONFIG QUIET
    PATHS "${CXX_CONAN_GENERATED_DIR}" NO_DEFAULT_PATH)
  if(TARGET opencv::opencv_core OR TARGET opencv_core)
    set(CXX_OPENCV_FOUND ON CACHE INTERNAL "" FORCE)
    cxx_msg(grass "OpenCV ${OpenCV_VERSION}")
  else()
    cxx_msg(gray "OpenCV: not found (conan)")
  endif()
endfunction()

# -----------------------------------------------------------------------------
# cxx_link_conan_deps(target)
# 作用：为 target 链接已在 Conan 中发现的依赖（按需；OpenCV 只链 core）。
# -----------------------------------------------------------------------------
function(cxx_link_conan_deps target)
  if(NOT TARGET ${target})
    return()
  endif()
  if(CXX_NLOHMANN_JSON_FOUND)
    target_link_libraries(${target} PUBLIC nlohmann_json::nlohmann_json)
  endif()
  if(CXX_BOOST_FOUND)
    target_link_libraries(${target} PUBLIC Boost::headers)
  endif()
  if(CXX_ABSL_FOUND)
    target_link_libraries(${target} PUBLIC absl::strings absl::status)
  endif()
  if(CXX_ASIO_FOUND)
    target_link_libraries(${target} PUBLIC asio::asio)
  endif()
  if(CXX_EIGEN_FOUND)
    target_link_libraries(${target} PUBLIC Eigen3::Eigen)
  endif()
  if(CXX_SQLITE_ORM_FOUND)
    target_link_libraries(${target} PUBLIC sqlite_orm::sqlite_orm)
  endif()
  if(CXX_OPENCV_FOUND)
    if(TARGET opencv::opencv_core)
      target_link_libraries(${target} PUBLIC opencv::opencv_core)
    elseif(TARGET opencv_core)
      target_link_libraries(${target} PUBLIC opencv_core)
    endif()
  endif()
endfunction()
