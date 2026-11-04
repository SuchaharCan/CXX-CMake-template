#!/usr/bin/env python3
"""Keep object compile commands for selected targets and project sources."""

from __future__ import annotations

import json
import os
import sys


def main() -> int:
    if len(sys.argv) < 6 or "--" not in sys.argv:
        print(
            "usage: filter_clang_doc_compile_commands.py INPUT OUTPUT TARGET... -- SOURCE...",
            file=sys.stderr,
        )
        return 2

    separator = sys.argv.index("--")
    input_path, output_path, *targets = sys.argv[1:separator]
    source_paths = sys.argv[separator + 1 :]
    if not targets or not source_paths:
        print("clang-doc filter requires at least one target and one source", file=sys.stderr)
        return 2

    sources = {os.path.realpath(path) for path in source_paths}
    target_markers = {f"CMakeFiles/{target}.dir/" for target in targets}

    with open(input_path, encoding="utf-8") as source:
        commands = json.load(source)

    filtered = [
        entry
        for entry in commands
        if str(entry.get("output", "")).endswith(".o")
        and any(marker in str(entry.get("output", "")) for marker in target_markers)
        and os.path.realpath(entry.get("file", "")) in sources
    ]

    with open(output_path, "w", encoding="utf-8") as destination:
        json.dump(filtered, destination, ensure_ascii=False, indent=2)
        destination.write("\n")

    expected = {os.path.basename(path) for path in sources}
    found = {os.path.basename(entry.get("file", "")) for entry in filtered}
    missing = sorted(expected - found)
    if missing:
        print(
            "clang-doc compile command filter missed: " + ", ".join(missing),
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
