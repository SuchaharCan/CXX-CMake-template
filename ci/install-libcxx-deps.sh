#!/usr/bin/env bash
# =============================================================================
# ci/install-libcxx-deps.sh — 用 libc++ 构建第三方依赖（Linux modules 专用）
# 背景：Ubuntu apt 的 benchmark/gtest/spdlog/fmt 都用 libstdc++ 构建，
#       与 libc++ 目标混链会出现 CXXABI / DSO 缺失并存在 ABI 风险。
# 产物：${CXX_LIBCXX_DEPS_PREFIX}（默认 <repo>/.deps-libcxx）
# 版本：fmt 10.2.1 / spdlog 1.15.3 / googletest 1.16.0 / benchmark 1.9.1
# =============================================================================
set -euo pipefail

PREFIX="${CXX_LIBCXX_DEPS_PREFIX:-$(pwd)/.deps-libcxx}"
SRC="${LIBCXX_DEPS_SRC:-/tmp/libcxx-deps-src}"
mkdir -p "$PREFIX" "$SRC"
if [ -f "${PREFIX}/.installed" ]; then
  echo "libc++ deps cache hit: ${PREFIX}"
  exit 0
fi
cd "$SRC"

CLANG_ROOT="/usr/lib/llvm-23"
LINK_FLAGS="-stdlib=libc++ -L${CLANG_ROOT}/lib"
COMMON=(
  -G Ninja
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_CXX_COMPILER=clang++
  -DCMAKE_CXX_FLAGS=-stdlib=libc++
  -DCMAKE_EXE_LINKER_FLAGS="$LINK_FLAGS"
  -DCMAKE_SHARED_LINKER_FLAGS="$LINK_FLAGS"
  -DCMAKE_INSTALL_PREFIX="$PREFIX"
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

fetch() {
  local name="$1" url="$2"
  curl -fsSL -o "${name}.tar.gz" "$url"
  tar -xzf "${name}.tar.gz"
}

build_install() {
  local name="$1" src="$2"
  shift 2
  cmake -S "$src" -B "build-${name}" "${COMMON[@]}" "$@"
  cmake --build "build-${name}" --target install -j"$(nproc)"
}

fetch fmt "https://github.com/fmtlib/fmt/archive/refs/tags/10.2.1.tar.gz"
build_install fmt "fmt-10.2.1" \
  -DFMT_TEST=OFF -DFMT_DOC=OFF

fetch spdlog "https://github.com/gabime/spdlog/archive/refs/tags/v1.15.3.tar.gz"
build_install spdlog "spdlog-1.15.3" \
  -DSPDLOG_FMT_EXTERNAL=ON -DSPDLOG_BUILD_EXAMPLE=OFF -DSPDLOG_BUILD_TESTS=OFF \
  -DCMAKE_PREFIX_PATH="$PREFIX"

fetch googletest "https://github.com/google/googletest/archive/refs/tags/v1.16.0.tar.gz"
build_install googletest "googletest-1.16.0" \
  -DBUILD_GMOCK=OFF -DINSTALL_GTEST=ON

fetch benchmark "https://github.com/google/benchmark/archive/refs/tags/v1.9.1.tar.gz"
build_install benchmark "benchmark-1.9.1" \
  -DBENCHMARK_ENABLE_TESTING=OFF -DBENCHMARK_ENABLE_GTEST_TESTS=OFF \
  -DCMAKE_PREFIX_PATH="$PREFIX"

echo "libc++ deps installed to ${PREFIX}"
touch "${PREFIX}/.installed"
ls "${PREFIX}/lib"/cmake 2>/dev/null | head
