#!/usr/bin/env python3
"""Update expected values in DuckDB sqllogictest files to match actual output.

Key observations about DuckDB unittest output:
  - Failure details (Wrong result, Expected/Actual result) are on STDERR
  - Progress output is on STDOUT (ignored)
  - Each failure in stderr is: N. file:line / === / Wrong result / === / SQL / ===
      / Mismatch / === / Expected result: / === / <expected> / blank
      / === / Actual result: / === / <actual> / blank
  - The actual result block ends at the FIRST blank line (not a === separator)
  - Reported line number is the "query I" line; search FORWARD to find "----"

Strategy:
  1. Run test binary, parse STDERR for failures
  2. For each failure, extract (file, query_line, actual_lines)
  3. Fix each file: search FORWARD from query_line to find "----", replace expected
  4. Apply fixes bottom-to-top within each file to keep line numbers stable
  5. Repeat until zero failures
"""

import subprocess
import sys
import re
import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_DIR = os.path.dirname(SCRIPT_DIR)
TEST_BINARY = os.path.join(REPO_DIR, "build", "release", "test", "unittest")
ENV = {**os.environ, 'TZ': 'Europe/Brussels'}


def run_tests(pattern):
    result = subprocess.run(
        [TEST_BINARY, pattern],
        env=ENV,
        capture_output=True,
        text=True,
        cwd=REPO_DIR,
    )
    return result.returncode == 0, result.stderr


def parse_failures(stderr):
    """Return list of (file_path, query_line_num, actual_lines) for each failure."""
    failures = []
    lines = stderr.split('\n')
    i = 0

    while i < len(lines):
        m = re.match(r'Wrong result in query! \((.+):(\d+)\)!', lines[i].strip())
        if not m:
            i += 1
            continue

        file_path = m.group(1)
        line_num = int(m.group(2))

        # Scan forward to find "Actual result:" section
        j = i + 1
        actual_lines = []
        found = False

        while j < len(lines):
            if lines[j].strip() == 'Actual result:':
                # Skip the === separator after "Actual result:"
                j += 1
                while j < len(lines) and set(lines[j].strip()) <= {'='} and lines[j].strip():
                    j += 1
                # Collect lines until the first blank line (end of actual block)
                while j < len(lines) and lines[j].strip():
                    actual_lines.append(lines[j].rstrip('\n'))
                    j += 1
                found = True
                break
            j += 1

        if found:
            failures.append((file_path, line_num, actual_lines))

        i = j

    return failures


def fix_file(file_path, fixes):
    """Apply (line_num, actual_lines) fixes to file_path, bottom-to-top."""
    full_path = os.path.join(REPO_DIR, file_path)
    with open(full_path, 'r') as f:
        content = f.readlines()

    # Apply from last line to first so earlier indices stay valid
    for line_num, actual_lines in sorted(fixes, key=lambda x: x[0], reverse=True):
        idx = line_num - 1  # 0-indexed, points to "query I" line

        # Search FORWARD for '----' separator
        sep_idx = None
        for k in range(idx, min(idx + 20, len(content))):
            if content[k].strip() == '----':
                sep_idx = k
                break

        if sep_idx is None:
            print(f"  WARNING: no '----' found within 20 lines of {file_path}:{line_num}")
            continue

        # Find end of current expected block (stop at blank line or EOF)
        end_idx = sep_idx + 1
        while end_idx < len(content) and content[end_idx].strip():
            end_idx += 1

        # Replace the expected block
        new_block = [line + '\n' for line in actual_lines]
        content[sep_idx + 1:end_idx] = new_block

        preview = (actual_lines[0] if actual_lines else '(empty)')[:70]
        print(f"  {file_path}:{line_num} → {preview}")

    with open(full_path, 'w') as f:
        f.writelines(content)


def update_all(pattern, max_rounds=60):
    for rnd in range(1, max_rounds + 1):
        ok, stderr = run_tests(pattern)
        if ok:
            print(f"Round {rnd}: ALL PASS")
            return True

        failures = parse_failures(stderr)
        if not failures:
            print(f"Round {rnd}: tests failed but no wrong-result failures found.")
            # Print parse errors or other issues
            for line in stderr.split('\n'):
                if 'FAILED' in line or 'Error' in line or 'error' in line:
                    print(f"  {line}")
            return False

        print(f"Round {rnd}: {len(failures)} failure(s)")

        # Group fixes by file
        by_file: dict = {}
        for fp, ln, actual in failures:
            by_file.setdefault(fp, []).append((ln, actual))

        for fp, fixes in by_file.items():
            fix_file(fp, fixes)

    print(f"Did not converge after {max_rounds} rounds.")
    return False


if __name__ == '__main__':
    pattern = sys.argv[1] if len(sys.argv) > 1 else "test/sql/*"
    success = update_all(pattern)
    sys.exit(0 if success else 1)
