#!/usr/bin/env bash
# Verify the stable Pages URL contract before uploading a deployment artifact.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SITE_DIR="${1:-${ROOT}/build/pages-site}"
MANIFEST="${ROOT}/ci/pages-templates.tsv"

if [[ ! -f "${SITE_DIR}/index.html" ]]; then
  echo "error: missing Pages root index: ${SITE_DIR}/index.html" >&2
  exit 1
fi

while IFS=$'\t' read -r template_name _description; do
  [[ -z "$template_name" || "$template_name" == \#* ]] && continue

  doxygen_index="${SITE_DIR}/${template_name}/index.html"
  clangdoc_index="${SITE_DIR}/${template_name}/clang-doc/index.html"

  if [[ ! -s "$doxygen_index" ]] || ! grep -Fq 'doxygen.css' "$doxygen_index"; then
    echo "error: stable Doxygen URL is unavailable: /${template_name}/" >&2
    exit 1
  fi
  if ! grep -Eq 'data-doc-backend-switch="clang-doc"[^>]*href="clang-doc/"' \
    "$doxygen_index"; then
    echo "error: Doxygen entry cannot switch to clang-doc: /${template_name}/" >&2
    exit 1
  fi
  if [[ ! -s "$clangdoc_index" ]] \
    || ! grep -Fq 'clang-doc-mustache.css' "$clangdoc_index"; then
    echo "error: clang-doc URL is unavailable: /${template_name}/clang-doc/" >&2
    exit 1
  fi
  if ! grep -Eq 'data-doc-backend-switch="doxygen"[^>]*href="\.\./"' \
    "$clangdoc_index"; then
    echo "error: clang-doc entry cannot switch back to Doxygen: /${template_name}/clang-doc/" >&2
    exit 1
  fi

  while IFS= read -r -d '' html_file; do
    if ! grep -Fq 'data-doc-backend-switch="clang-doc"' "$html_file"; then
      echo "error: Doxygen page missing clang-doc switch: ${html_file}" >&2
      exit 1
    fi
  done < <(
    find "${SITE_DIR}/${template_name}" -path "*/clang-doc" -prune -o \
      -type f -name '*.html' -print0
  )
  while IFS= read -r -d '' html_file; do
    if ! grep -Fq 'data-doc-backend-switch="doxygen"' "$html_file"; then
      echo "error: clang-doc page missing Doxygen switch: ${html_file}" >&2
      exit 1
    fi
  done < <(
    find "${SITE_DIR}/${template_name}/clang-doc" \
      -type f -name '*.html' -print0
  )
  if ! grep -Fq "href=\"${template_name}/\"" "${SITE_DIR}/index.html"; then
    echo "error: Pages root lost the stable Doxygen link: /${template_name}/" >&2
    exit 1
  fi
  if ! grep -Fq "href=\"${template_name}/clang-doc/\"" "${SITE_DIR}/index.html"; then
    echo "error: Pages root missing clang-doc link: /${template_name}/clang-doc/" >&2
    exit 1
  fi
done <"$MANIFEST"

echo "stable Pages URL contract ok: ${SITE_DIR}"
