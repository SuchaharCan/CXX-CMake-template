#!/usr/bin/env bash
# =============================================================================
# ci/build-pages-docs.sh — 构建并汇总 GitHub Pages Doxygen 站点
#
# 结构：
#   <site-dir>/
#   ├── .nojekyll
#   ├── index.html
#   ├── single-exe-classic/
#   ├── single-exe-modules/
#   └── single-exe-static-modules/
#
# 只构建 Doxygen，不构建程序、不运行测试；static-modules 的 C++ 模块映射问题
# 因此不会进入 Pages 流水线。模板清单位于 ci/pages-templates.tsv。
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SITE_DIR="${1:-${ROOT}/build/pages-site}"
BUILD_ROOT="${PAGES_BUILD_ROOT:-${ROOT}/build/pages-build}"
MANIFEST="${ROOT}/ci/pages-templates.tsv"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

if [[ "$SITE_DIR" == "/" || -z "$SITE_DIR" ]]; then
  echo "error: refusing unsafe site directory: ${SITE_DIR}" >&2
  exit 2
fi
if [[ "$BUILD_ROOT" == "/" || -z "$BUILD_ROOT" ]]; then
  echo "error: refusing unsafe build directory: ${BUILD_ROOT}" >&2
  exit 2
fi
if [[ ! -f "$MANIFEST" ]]; then
  echo "error: missing Pages template manifest: $MANIFEST" >&2
  exit 1
fi

cmake -E remove_directory "$SITE_DIR"
cmake -E remove_directory "$BUILD_ROOT"
mkdir -p "$SITE_DIR" "$BUILD_ROOT"

build_template() {
  local template_name="$1"
  local template_dir="${ROOT}/${template_name}"
  local build_dir="${BUILD_ROOT}/${template_name}"
  local html_dir="${build_dir}/docs/doxygen/html"
  local destination="${SITE_DIR}/${template_name}"

  if [[ ! -f "${template_dir}/CMakeLists.txt" ]]; then
    echo "error: template has no CMakeLists.txt: ${template_name}" >&2
    return 1
  fi

  echo
  echo "============================================================"
  echo "Building Doxygen: ${template_name}"
  echo "============================================================"

  cmake -S "$template_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCXX_PREFERRED_COMPILER=auto \
    -DCXX_STDLIB=auto \
    -DCXX_ENABLE_CCACHE=ON \
    -DCXX_ENABLE_DOCS=ON \
    -DCXX_DOC_CLANGDOC=OFF \
    -DCXX_ENABLE_GTEST=OFF \
    -DCXX_ENABLE_COVERAGE=OFF \
    -DCXX_ENABLE_FUZZERS=OFF

  cmake --build "$build_dir" --target docs-doxygen -j"$JOBS"

  if [[ ! -f "${html_dir}/index.html" ]]; then
    echo "error: Doxygen index missing: ${html_dir}/index.html" >&2
    return 1
  fi

  mkdir -p "$destination"
  cp -R "${html_dir}/." "$destination/"
}

: >"${SITE_DIR}/.nojekyll"

while IFS=$'\t' read -r template_name description; do
  [[ -z "$template_name" || "$template_name" == \#* ]] && continue
  build_template "$template_name"
done <"$MANIFEST"

{
  cat <<'HTML_HEAD'
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>00CMake-Template · Doxygen 文档</title>
  <style>
    :root { color-scheme: light dark; --accent: #0969da; --card: rgba(127,127,127,.12); }
    body { max-width: 920px; margin: 0 auto; padding: 3rem 1.25rem 5rem;
           font: 16px/1.65 system-ui, -apple-system, BlinkMacSystemFont, sans-serif; }
    h1 { margin-bottom: .35rem; }
    .lead { margin-top: 0; opacity: .72; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(240px, 1fr));
            gap: 1rem; margin-top: 2rem; }
    a.card { display: block; padding: 1.2rem; border-radius: 14px; background: var(--card);
             color: inherit; text-decoration: none; border: 1px solid rgba(127,127,127,.2); }
    a.card:hover { border-color: var(--accent); transform: translateY(-1px); }
    a.card h2 { margin: 0 0 .45rem; color: var(--accent); font-size: 1.08rem; }
    code { font-family: ui-monospace, SFMono-Regular, Menlo, monospace; }
    footer { margin-top: 3rem; opacity: .65; font-size: .9rem; }
  </style>
</head>
<body>
  <h1>00CMake-Template</h1>
  <p class="lead">C++ 模板集合的在线 Doxygen API 文档。</p>
  <main class="grid">
HTML_HEAD

  while IFS=$'\t' read -r template_name description; do
    [[ -z "$template_name" || "$template_name" == \#* ]] && continue
    printf '    <a class="card" href="%s/"><h2>%s</h2><code>%s/</code><p>%s</p></a>\n' \
      "$template_name" "$template_name" "$template_name" "$description"
  done <"$MANIFEST"

  cat <<'HTML_TAIL'
  </main>
  <footer>
    文档由 Doxygen 自动生成。
    <a href="https://github.com/SuchaharCan/CXX-CMake-template">查看模板源码</a>。
  </footer>
</body>
</html>
HTML_TAIL
} >"${SITE_DIR}/index.html"

echo
echo "Pages site ready: ${SITE_DIR}"
find "$SITE_DIR" -mindepth 1 -maxdepth 2 -type f -name index.html -print | sort
