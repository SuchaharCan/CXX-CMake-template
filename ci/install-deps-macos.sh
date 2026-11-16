#!/usr/bin/env bash
# =============================================================================
# ci/install-deps-macos.sh — GitHub macOS runner / 自建 macOS 27 依赖安装
# 用途：Homebrew 补齐 cmake≥4.5.0 / clang++≥23.1 / ninja / just / grc
#       / spdlog / fmt / googletest / google-benchmark / doxygen / ccache / bqlog
# 模式：默认缺失则 brew install；版本不足则 brew upgrade；
#       CI_DEPS_CHECK_ONLY=1 只检查不安装。
# 环境：macOS 26/27（arm64），已装 Homebrew + Xcode Command Line Tools。
# =============================================================================
set -euo pipefail

CHECK_ONLY="${CI_DEPS_CHECK_ONLY:-0}"
SUDO_PREFIX=""
if [ "$(id -u)" -ne 0 ]; then
  SUDO_PREFIX="sudo"
fi

if ! command -v brew >/dev/null 2>&1; then
  echo "error: Homebrew not found (https://brew.sh)" >&2
  exit 1
fi
if ! xcode-select -p >/dev/null 2>&1; then
  echo "error: Xcode Command Line Tools not installed (xcode-select --install)" >&2
  exit 1
fi

export HOMEBREW_NO_AUTO_UPDATE=1

FORMULAE="cmake ninja just grc ccache spdlog fmt googletest google-benchmark doxygen graphviz llvm"

missing=()
for f in $FORMULAE; do
  if ! brew list --versions "$f" >/dev/null 2>&1; then
    missing+=("$f")
  fi
done

if [ "${#missing[@]}" -gt 0 ]; then
  if [ "$CHECK_ONLY" = "1" ]; then
    echo "error: missing Homebrew formulae: ${missing[*]}" >&2
    exit 1
  fi
  echo "installing: ${missing[*]}"
  brew install "${missing[@]}"
fi

# HOMEBREW_NO_AUTO_UPDATE 会保留跑器镜像里的旧索引；cmake 4.5.0 需要新索引。
# 先显式刷新索引，避免 `brew upgrade cmake` 因索引过旧而空转。
if [ "$CHECK_ONLY" = "1" ]; then
  echo "info: CI_DEPS_CHECK_ONLY — 跳过 Homebrew 索引刷新"
else
  echo "refreshing Homebrew index"
  brew update --quiet || echo "warn: brew update failed（沿用现有索引）"
fi

check_min() {
  local actual="$1" minimum="$2" label="$3"
  if [ "$(printf '%s\n%s\n' "$minimum" "$actual" | sort -V | head -1)" != "$minimum" ]; then
    echo "error: ${label} ${actual} < ${minimum}" >&2
    return 1
  fi
  return 0
}

cmake_ver="$(cmake --version | head -1 | awk '{print $3}')"
just_ver="$(just --version | awk '{print $2}')"
clang_ver="$(/opt/homebrew/opt/llvm/bin/clang++ --version | head -1 | sed -E 's/.* version ([0-9.]+).*/\1/')"

upgrade=()
check_min "$cmake_ver" "4.5.0" cmake || upgrade+=(cmake)
check_min "$just_ver" "1.20.0" just || upgrade+=(just)
check_min "$clang_ver" "23.1.0" clang++ || upgrade+=(llvm)

if [ "${#upgrade[@]}" -gt 0 ]; then
  if [ "$CHECK_ONLY" = "1" ]; then
    echo "error: upgrade required for: ${upgrade[*]}" >&2
    exit 1
  fi
  echo "upgrading: ${upgrade[*]}"
  brew upgrade "${upgrade[@]}"
  cmake_ver="$(cmake --version | head -1 | awk '{print $3}')"
  just_ver="$(just --version | awk '{print $2}')"
  clang_ver="$(/opt/homebrew/opt/llvm/bin/clang++ --version | head -1 | sed -E 's/.* version ([0-9.]+).*/\1/')"
fi

# ---- cmake 回退：Homebrew 若仍无 4.5.0 bottle，安装 Kitware 官方二进制 ----
cmake_ver="$(cmake --version | head -1 | awk '{print $3}')"
if [ "$(printf '%s\n%s\n' "4.5.0" "$cmake_ver" | sort -V | head -1)" != "4.5.0" ]; then
  if [ "$CHECK_ONLY" = "1" ]; then
    echo "error: cmake ${cmake_ver} < 4.5.0（CHECK_ONLY 模式不安装官方二进制）" >&2
    exit 1
  fi
  CMAKE_VERSION_PIN="${CMAKE_VERSION_PIN:-4.5.0}"
  CMAKE_TARBALL="cmake-${CMAKE_VERSION_PIN}-macos-universal.tar.gz"
  echo "installing official cmake ${CMAKE_VERSION_PIN}（Homebrew bottle 不可用）"
  curl -fL --silent --show-error --retry 3 --retry-delay 2 \
    -o "/tmp/${CMAKE_TARBALL}" \
    "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION_PIN}/${CMAKE_TARBALL}"
  $SUDO_PREFIX mkdir -p /opt
  $SUDO_PREFIX tar -xzf "/tmp/${CMAKE_TARBALL}" -C /opt
  for tool in cmake ctest cpack; do
    $SUDO_PREFIX ln -sf "/opt/cmake-${CMAKE_VERSION_PIN}-macos-universal/CMake.app/Contents/bin/${tool}" \
      "/usr/local/bin/${tool}"
  done
  hash -r
  cmake_ver="$(cmake --version | head -1 | awk '{print $3}')"
fi

# bqlog 是可选依赖：GitHub 的 Homebrew 可能没有该 formula，不应阻断 CI
if ! brew list --versions bqlog >/dev/null 2>&1; then
  if [ "$CHECK_ONLY" = "1" ]; then
    brew info --formula bqlog >/dev/null 2>&1 \
      || echo "warn: bqlog missing / local brew index stale (optional)"
  else
    # GitHub runner 的 Homebrew 索引可能较旧；官方安装方式就是 brew install bqlog
    if ! brew info --formula bqlog >/dev/null 2>&1; then
      echo "Homebrew index missing bqlog — running brew update"
      brew update --quiet || true
    fi
    brew install bqlog || echo "warn: bqlog unavailable on this Homebrew (optional)"
  fi
fi

check_min "$cmake_ver" "4.5.0" cmake
check_min "$just_ver" "1.20.0" just
check_min "$clang_ver" "23.1.0" "clang++"

echo "cmake ${cmake_ver}"
echo "just ${just_ver}"
echo "clang++ ${clang_ver} (/opt/homebrew/opt/llvm/bin/clang++)"
ninja --version | sed 's/^/ninja /'

# ---- 让 clang 工具链命令在 PATH 中可用（Homebrew llvm 是 keg-only）----
if [ "$CHECK_ONLY" != "1" ]; then
  for tool in clang-format clang-tidy clang-doc clang-scan-deps; do
    if [ -x "/opt/homebrew/opt/llvm/bin/${tool}" ]; then
      ln -sf "/opt/homebrew/opt/llvm/bin/${tool}" "/usr/local/bin/${tool}" 2>/dev/null \
        || sudo -n ln -sf "/opt/homebrew/opt/llvm/bin/${tool}" "/usr/local/bin/${tool}" 2>/dev/null \
        || echo "warn: cannot symlink ${tool} into /usr/local/bin"
    fi
  done
fi

# ---- Conan：确保安装 + 生成本机构建 profile（compiler.version 跟随本机 clang）----
if ! command -v conan >/dev/null 2>&1; then
  brew install conan
fi
command -v conan >/dev/null 2>&1 || { echo "error: conan unavailable after install"; exit 1; }

llvm_major="$(/opt/homebrew/opt/llvm/bin/clang++ --version | head -1 | sed -E 's/.*version ([0-9]+).*/\1/')"
for profile in single-exe-*/conan/profiles/macos-arm64; do
  [ -e "$profile" ] || continue
  mkdir -p "$(dirname "$profile")"
  cat > "$profile" <<EOF
[settings]
arch=armv8
build_type=Release
compiler=clang
compiler.cppstd=gnu17
compiler.libcxx=libc++
compiler.version=${llvm_major}
os=Macos
EOF
done
echo "conan profile regenerated (clang ${llvm_major})"

# 为含 conanfile 的模板一次性安装锁定依赖（必选 + 可选；后续 just 构建直接复用）
for conanfile in single-exe-*/conanfile.txt; do
  [ -e "$conanfile" ] || continue
  d="$(dirname "$conanfile")"
  echo "conan install @ ${d}"
  (cd "$d" && conan install . -of build/conan \
    --profile:host=conan/profiles/macos-arm64 \
    --profile:build=conan/profiles/macos-arm64 \
    -r conancenter --build=missing)
done

# ---- 可选 cask：SonarQube CLI / CodeQL（缺失则尝试安装，不阻断 CI）----
for cask in sonarqube-cli codeql; do
  if ! brew list --cask --versions "$cask" >/dev/null 2>&1; then
    if [ "$CHECK_ONLY" = "1" ]; then
      echo "warn: missing cask ${cask}"
      continue
    fi
    brew install --cask "$cask" || echo "warn: cask ${cask} unavailable"
  fi
done

echo "macOS $(sw_vers -productVersion) — deps ok"
