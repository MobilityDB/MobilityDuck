#!/usr/bin/env python3
"""Flag timezone-pinned expected values in DuckDB sqllogic tests.

A test that hardcodes a timezone offset (`+00`, `+01`, `+02`, …) in an
expected output line is fragile: MEOS renders timestamps using the
process's TZ, and any divergence between the developer's machine and CI
flips the expected offset.  The project policy is to write
timezone-neutral assertions instead — value equality (`= tstzspan
'...'`), accessor functions (`stbox_eq`, `numSpans`, `numValues`,
`startTimestamp() = …`), or `asText(...)` round-trips on both sides.

This script walks `test/sql/**/*.test`, finds every line in an
expected-output block (the lines after `----`) that contains a
`±NN` offset, and prints the file:line and a short snippet.  Returns
non-zero if any are found, so it can be used as a pre-commit gate or a
CI lint step.

Inputs that look like literal-offset SQL (`tstzspan '[2000-01-01
00:00:00+00, ...]'`) are part of a query line, not an expected value,
and are skipped — only lines that follow a `----` separator within a
test block are checked.

Usage:
    python3 scripts/lint-tz-pinned-tests.py [--root test]
"""

import argparse
import glob
import os
import re
import sys


# Match a UTC-offset literal at the tail of a TIMESTAMPTZ rendering.
# Matches `+00`, `+05:30`, `-08`, etc.  The negative lookbehind for
# `[eE]` avoids scientific-notation false positives (`1.5e+00`).
TZ_OFFSET_RE = re.compile(r"(?<![eE])([+\-]\d{2}(?::?\d{2})?)\b")


def scan_test_file(path):
    """Yield (line_number, line_text) for offset-bearing lines in
    expected-output blocks of a sqllogic-style .test file."""
    in_expected = False
    with open(path, encoding="utf-8", errors="replace") as f:
        for lineno, raw in enumerate(f, start=1):
            line = raw.rstrip("\n")
            stripped = line.strip()
            # Empty line ends an expected-output block.
            if not stripped:
                in_expected = False
                continue
            # The `----` separator opens an expected-output block in
            # sqllogic test syntax.
            if stripped == "----":
                in_expected = True
                continue
            if not in_expected:
                continue
            if TZ_OFFSET_RE.search(line):
                yield lineno, line


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--root",
        default="test",
        help="Root directory to scan for .test files (default: test)",
    )
    ap.add_argument(
        "--quiet",
        action="store_true",
        help="Suppress per-line output; only print the summary count.",
    )
    args = ap.parse_args()

    if not os.path.isdir(args.root):
        sys.exit(f"Test root not found: {args.root}")

    pinned_count = 0
    pinned_files = set()
    for path in sorted(glob.glob(f"{args.root}/**/*.test", recursive=True)):
        rel = os.path.relpath(path)
        for lineno, line in scan_test_file(path):
            pinned_count += 1
            pinned_files.add(rel)
            if not args.quiet:
                snippet = line.strip()
                if len(snippet) > 100:
                    snippet = snippet[:97] + "..."
                print(f"{rel}:{lineno}: {snippet}")

    if pinned_count:
        print()
        print(
            f"Found {pinned_count} timezone-pinned expected values in "
            f"{len(pinned_files)} files.  Rewrite as value-equality "
            f"(`= tstzspan '...'`), accessor (`numSpans`, `startTimestamp`), "
            f"or `asText(...)` round-trip assertions per the project "
            f"timezone-neutral policy (PR #111 / commit 9dd765a)."
        )
        return 1

    print("No timezone-pinned expected values found.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
