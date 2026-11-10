#!/usr/bin/env bash
# =============================================================================
# ci/build-pages-docs.sh — 构建并汇总 GitHub Pages Doxygen + clang-doc 站点
#
# 结构：
#   <site-dir>/
#   ├── .nojekyll
#   ├── index.html
#   ├── single-exe-classic/
#   │   └── clang-doc/
#   ├── single-exe-modules/
#   │   └── clang-doc/
#   └── single-exe-static-modules/
#       └── clang-doc/
#
# Doxygen 保留在模板根路径，clang-doc 放在稳定的 /<template>/clang-doc/。
# C++ 模块模板会先构建 clang-doc 登记的目标以生成 .modmap；不运行测试。
# 模板清单位于 ci/pages-templates.tsv。
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SITE_DIR="${1:-${ROOT}/build/pages-site}"
BUILD_ROOT="${PAGES_BUILD_ROOT:-${ROOT}/build/pages-build}"
MANIFEST="${ROOT}/ci/pages-templates.tsv"
SITE_NAV_CSS="${ROOT}/ci/site-nav.css"
SITE_NAV_HTML="${ROOT}/ci/site-nav.html"
SITE_UMAMI_HTML="${ROOT}/ci/umami.html"
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
if [[ ! -f "$SITE_UMAMI_HTML" ]]; then
  echo "error: missing Umami fragment: $SITE_UMAMI_HTML" >&2
  exit 1
fi

cmake -E remove_directory "$SITE_DIR"
cmake -E remove_directory "$BUILD_ROOT"
mkdir -p "$SITE_DIR" "$BUILD_ROOT"
cp "$SITE_NAV_CSS" "${SITE_DIR}/site-nav.css"

inject_template_navigation() {
  local destination="$1"

  find "$destination" -type f -name '*.html' -exec \
    env SITE_ROOT="$SITE_DIR" NAV_FRAGMENT="$SITE_NAV_HTML" perl -0pi -e '
      use File::Spec;
      use File::Basename qw(dirname);
      BEGIN {
        open my $fh, q{<}, $ENV{NAV_FRAGMENT} or die "cannot read navigation fragment: $ENV{NAV_FRAGMENT}\n";
        local $/;
        $nav = <$fh>;
        close $fh;
      }
      my $absolute_file = File::Spec->rel2abs($ARGV);
      my $file_dir = dirname($absolute_file);
      my $css_href = File::Spec->abs2rel("$ENV{SITE_ROOT}/site-nav.css", $file_dir);
      my $root_href = File::Spec->abs2rel($ENV{SITE_ROOT}, $file_dir);
      my $local_nav = $nav;
      $local_nav =~ s{href="../"}{href="$root_href/"}g;
      my $site_relative_file = File::Spec->abs2rel($absolute_file, $ENV{SITE_ROOT});
      my ($template_name) = split m{/}, $site_relative_file, 2;
      my $backend_link = "";
      if (defined $template_name && $template_name =~ /^single-exe-/) {
        my $in_clangdoc = $site_relative_file =~ m{^\Q$template_name\E/clang-doc(?:/|$)};
        my $backend_target = "$ENV{SITE_ROOT}/$template_name" . ($in_clangdoc ? "" : "/clang-doc");
        my $backend_href = File::Spec->abs2rel($backend_target, $file_dir);
        $backend_href =~ s{/+\z}{};
        $backend_href .= "/";
        my $backend_key = $in_clangdoc ? "doxygen" : "clang-doc";
        my $backend_label = $in_clangdoc ? "Doxygen 文档" : "clang-doc 文档";
        $backend_link = qq{<a class="omni-detail-nav__link" data-doc-backend-switch="$backend_key" href="$backend_href" title="切换到$backend_label" aria-label="切换到$backend_label">}
          . qq{<svg viewBox="0 0 24 24" aria-hidden="true" focusable="false" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M5 4.5h10A2.5 2.5 0 0 1 17.5 7v10A2.5 2.5 0 0 1 15 19.5H5A2.5 2.5 0 0 1 2.5 17V7A2.5 2.5 0 0 1 5 4.5Z"/><path d="M7 8h6M7 11h6M7 14h4M19 8h2.5M19 11h2.5M19 14h2.5"/></svg>}
          . qq{<span>$backend_label</span></a>};
      }
      $local_nav =~ s{<!-- DOC_BACKEND_SWITCH -->}{$backend_link};
      s{</head>}{<link rel="stylesheet" href="$css_href"/>\n</head>}
        or die "missing </head> in $ARGV\n";
      s{<body([^>]*)>}{<body$1>\n$local_nav}
        or die "missing <body> in $ARGV\n";
    ' {} +
}

inject_umami_tracking() {
  local destination="$1"

  find "$destination" -type f -name '*.html' -exec \
    env UMAMI_FRAGMENT="$SITE_UMAMI_HTML" perl -0pi -e '
      BEGIN {
        open my $fh, q{<}, $ENV{UMAMI_FRAGMENT} or die "cannot read Umami fragment: $ENV{UMAMI_FRAGMENT}\n";
        local $/;
        $umami = <$fh>;
        close $fh;
        chomp $umami;
      }
      next if m{cloud\.umami\.is/script\.js};
      s{</head>}{\n$umami\n</head>}
        or die "missing </head> in $ARGV\n";
    ' {} +
}

build_template() {
  local template_name="$1"
  local template_dir="${ROOT}/${template_name}"
  local build_dir="${BUILD_ROOT}/${template_name}"
  local destination="${SITE_DIR}/${template_name}"
  local doxygen_html_dir="${build_dir}/docs/doxygen/html"
  local clangdoc_html_dir="${build_dir}/docs/clang-doc/html"
  local clangdoc_log="${build_dir}/clang-doc-build.log"

  if [[ ! -f "${template_dir}/CMakeLists.txt" ]]; then
    echo "error: template has no CMakeLists.txt: ${template_name}" >&2
    return 1
  fi

  echo
  echo "============================================================"
  echo "Building API docs: ${template_name}"
  echo "============================================================"

  cmake -S "$template_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCXX_PREFERRED_COMPILER=auto \
    -DCXX_STDLIB=auto \
    -DCXX_ENABLE_CCACHE=ON \
    -DCXX_ENABLE_DOCS=ON \
    -DCXX_DOC_CLANGDOC=ON \
    -DCXX_DOC_DOXYGEN=ON \
    -DCXX_ENABLE_CLANG_TIDY=OFF \
    -DCXX_ENABLE_CLANG_FORMAT=OFF \
    -DCXX_ENABLE_GTEST=OFF \
    -DCXX_ENABLE_BENCHMARK=OFF \
    -DCXX_ENABLE_COVERAGE=OFF \
    -DCXX_ENABLE_FUZZERS=OFF

  cmake --build "$build_dir" --target docs-doxygen -j"$JOBS"
  cmake --build "$build_dir" --target docs-clangdoc -j"$JOBS" 2>&1 | tee "$clangdoc_log"

  if grep -Eq 'no such file or directory: .*\.modmap|Failed to run action on|fatal error:' \
    "$clangdoc_log"; then
    echo "error: clang-doc reported incomplete source processing: ${clangdoc_log}" >&2
    return 1
  fi
  if [[ ! -f "${doxygen_html_dir}/index.html" ]]; then
    echo "error: Doxygen index missing: ${doxygen_html_dir}/index.html" >&2
    return 1
  fi
  if [[ ! -f "${clangdoc_html_dir}/index.html" ]]; then
    echo "error: clang-doc index missing: ${clangdoc_html_dir}/index.html" >&2
    return 1
  fi

  mkdir -p "$destination" "${destination}/clang-doc"
  cp -R "${doxygen_html_dir}/." "$destination/"
  cp -R "${clangdoc_html_dir}/." "${destination}/clang-doc/"
  inject_template_navigation "$destination"
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
  <title>00CMake-Template · API 文档</title>
  <link rel="stylesheet" href="site-nav.css">
  <style>
    :root { color-scheme: light dark; --accent: #0969da; --card: rgba(127,127,127,.12); }
    body { max-width: 920px; margin: 0 auto; padding: 3rem 1.25rem 5rem;
           font: 16px/1.65 system-ui, -apple-system, BlinkMacSystemFont, sans-serif; }
    h1 { margin-bottom: .35rem; }
    .lead { margin-top: 0; opacity: .72; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(240px, 1fr));
            gap: 1rem; margin-top: 2rem; }
    article.card { padding: 1.2rem; border-radius: 14px; background: var(--card);
                   border: 1px solid rgba(127,127,127,.2); }
    article.card:hover { border-color: var(--accent); transform: translateY(-1px); }
    article.card h2 { margin: 0 0 .45rem; color: var(--accent); font-size: 1.08rem; }
    .doc-links { display: flex; flex-wrap: wrap; gap: .5rem; margin-top: .9rem; }
    .doc-links a { padding: .38rem .7rem; border: 1px solid rgba(127,127,127,.28);
                   border-radius: 999px; color: var(--accent); font-weight: 600;
                   text-decoration: none; }
    .doc-links a:hover { border-color: var(--accent); }
    .doc-links a.primary { background: var(--accent); color: #fff; border-color: var(--accent); }
    code { font-family: ui-monospace, SFMono-Regular, Menlo, monospace; }
    footer { margin-top: 3rem; opacity: .65; font-size: .9rem; }
  </style>
</head>
<body>
  <header class="collection-header">
    <div>
      <h1>00CMake-Template</h1>
      <p class="lead">C++ 模板集合的在线 Doxygen 与 clang-doc API 文档。</p>
    </div>
    <a class="collection-blog-link" href="https://suchaharcan.github.io/03Share_blog/" title="打开 03Share_blog 博客" aria-label="打开 03Share_blog 博客">
      <svg viewBox="0 0 24 24" aria-hidden="true" focusable="false" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
        <path d="M5 4h10.5A1.5 1.5 0 0 1 17 5.5V8h1.5A1.5 1.5 0 0 1 20 9.5v9a1.5 1.5 0 0 1-1.5 1.5h-13A1.5 1.5 0 0 1 4 18.5V5.5A1.5 1.5 0 0 1 5.5 4H5Z"/>
        <path d="M7 7.5h7M7 10.5h4M7 13.5h3M16 11h1.5M16 14h1.5"/>
      </svg>
      <span>博客</span>
    </a>
  </header>
  <main class="grid">
HTML_HEAD

  while IFS=$'\t' read -r template_name description; do
    [[ -z "$template_name" || "$template_name" == \#* ]] && continue
    printf '    <article class="card">\n'
    printf '      <h2>%s</h2><code>%s/</code><p>%s</p>\n' \
      "$template_name" "$template_name" "$description"
    printf '      <div class="doc-links"><a class="primary" href="%s/">Doxygen</a><a href="%s/clang-doc/">clang-doc</a></div>\n' \
      "$template_name" "$template_name"
    printf '    </article>\n'
  done <"$MANIFEST"

  cat <<'HTML_TAIL'
  </main>
  <footer>
    文档由 Doxygen 与 clang-doc 自动生成。
    <a href="https://github.com/SuchaharCan/CXX-CMake-template">查看模板源码</a>。
    <a href="https://codeberg.org/SuzhaharCan/sc-cmake">sc-cmake 本地拉取应用上述模板或你个人仓库内模板的工具</a>。
    <p class="lead">brew tap SuzhaharCan/sc-cmake https://codeberg.org/SuzhaharCan/sc-cmake</p>
    <p class="lead">brew install sc-cmake。</p>
  </footer>
</body>
</html>
HTML_TAIL
} >"${SITE_DIR}/index.html"

inject_umami_tracking "$SITE_DIR"

while IFS=$'\t' read -r template_name _description; do
  [[ -z "$template_name" || "$template_name" == \#* ]] && continue
  for index_path in \
    "${SITE_DIR}/${template_name}/index.html" \
    "${SITE_DIR}/${template_name}/clang-doc/index.html"; do
    if [[ ! -s "$index_path" ]]; then
      echo "error: generated documentation index missing: ${index_path}" >&2
      exit 1
    fi
  done
done <"$MANIFEST"

bash "${ROOT}/ci/verify-pages-site.sh" "$SITE_DIR"

echo
echo "Pages site ready: ${SITE_DIR}"
find "$SITE_DIR" -mindepth 1 -maxdepth 3 -type f -name index.html -print | sort
