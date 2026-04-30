#!/usr/bin/env python3
"""Compare MobilityDB SQL surface against MobilityDuck registered surface.

Usage:
    python3 scripts/parity-audit.py \
        --mdb /path/to/MobilityDB \
        --mduck /path/to/MobilityDuck \
        --out docs/parity-status.md

The audit matches by **function name only** (case-insensitive). A name
registered in MobilityDuck is treated as covering all its overloads;
per-overload signature parity is *not* verified at this granularity.

Sections under SQL subdirectories listed in DEFERRED_FAMILIES are out
of scope for the active sweep. They still appear in the report (in a
deferred appendix) but do not contribute to the headline coverage
percentage. Re-include by removing the entry from DEFERRED_FAMILIES.
"""

import argparse
import collections
import glob
import os
import re
import sys
from datetime import date


# Type families currently deferred from the active parity sweep.
# Re-included once the temporal/geo surface stabilises.
DEFERRED_FAMILIES = {
    "npoint",
    "cbuffer",
    "pose",
    "rgeo",
}


CREATE_FUNC_RE = re.compile(
    r"CREATE\s+(?:OR\s+REPLACE\s+)?FUNCTION\s+(\w+)\s*\(([^)]*)\)",
    re.IGNORECASE | re.DOTALL,
)
CREATE_OP_RE = re.compile(r"CREATE\s+OPERATOR\s+(\S+)\s*\(", re.IGNORECASE)

REGISTER_SCALAR_RE = re.compile(r'ScalarFunction\s*\(\s*"([^"]+)"', re.IGNORECASE)
REGISTER_AGGR_RE = re.compile(r'AggregateFunction\s*\(\s*"([^"]+)"')
REGISTER_TABLE_RE = re.compile(r'TableFunction\s*\(\s*"([^"]+)"')


def collect_mobilitydb(mdb_root):
    sql_root = os.path.join(mdb_root, "mobilitydb", "sql")
    if not os.path.isdir(sql_root):
        sys.exit(f"MobilityDB SQL dir not found: {sql_root}")

    section_funcs = collections.OrderedDict()
    section_op_count = collections.OrderedDict()
    all_funcs = set()

    for section in sorted(os.listdir(sql_root)):
        full = os.path.join(sql_root, section)
        if not os.path.isdir(full):
            continue
        for sql in sorted(glob.glob(f"{full}/*.in.sql")):
            rel = os.path.relpath(sql, sql_root)
            with open(sql) as f:
                text = f.read()
            funcs = collections.Counter()
            for m in CREATE_FUNC_RE.finditer(text):
                funcs[m.group(1)] += 1
                all_funcs.add(m.group(1))
            section_funcs[rel] = funcs
            section_op_count[rel] = len(CREATE_OP_RE.findall(text))

    return section_funcs, section_op_count, all_funcs


def collect_mobilityduck(mduck_root):
    src_root = os.path.join(mduck_root, "src")
    if not os.path.isdir(src_root):
        sys.exit(f"MobilityDuck src dir not found: {src_root}")

    funcs = collections.Counter()
    files_for_func = collections.defaultdict(set)
    for cpp in glob.glob(f"{src_root}/**/*.cpp", recursive=True):
        with open(cpp, errors="replace") as f:
            text = f.read()
        rel = os.path.relpath(cpp, src_root)
        for regex in (REGISTER_SCALAR_RE, REGISTER_AGGR_RE, REGISTER_TABLE_RE):
            for m in regex.finditer(text):
                funcs[m.group(1)] += 1
                files_for_func[m.group(1)].add(rel)
    return funcs, files_for_func


def is_deferred(section_relpath):
    family = section_relpath.split("/", 1)[0]
    return family in DEFERRED_FAMILIES


def write_report(out_path, mdb_section_funcs, mdb_section_op_count,
                 all_mdb_funcs, mduck_funcs):
    mduck_funcs_lower = {k.lower(): k for k in mduck_funcs}

    active_results = []
    deferred_results = []
    for sec, funcs in mdb_section_funcs.items():
        if not funcs:
            continue
        covered, missing = [], []
        for fname, count in sorted(funcs.items()):
            if fname.lower() in mduck_funcs_lower:
                covered.append((fname, count))
            else:
                missing.append((fname, count))
        pct = (len(covered) / len(funcs)) * 100 if funcs else 0
        row = (sec, len(funcs), len(covered), len(missing), pct,
               missing, covered, mdb_section_op_count[sec])
        if is_deferred(sec):
            deferred_results.append(row)
        else:
            active_results.append(row)

    def totals(results):
        n = sum(r[1] for r in results)
        cov = sum(r[2] for r in results)
        miss = sum(r[3] for r in results)
        pct = (cov / n * 100) if n else 0
        return n, cov, miss, pct

    a_total, a_cov, a_miss, a_pct = totals(active_results)
    d_total, d_cov, d_miss, d_pct = totals(deferred_results)

    lines = []
    lines.append("# MobilityDuck parity status — surface-level audit")
    lines.append("")
    lines.append(
        f"Generated {date.today().isoformat()}. **Active scope** "
        f"(temporal + geo): {a_cov}/{a_total} names covered "
        f"({a_pct:.1f}%). Deferred families "
        f"({', '.join(sorted(DEFERRED_FAMILIES))}) listed in an "
        f"appendix and not counted in headline coverage."
    )
    lines.append("")
    lines.append(
        "**Methodology**: parsed `CREATE FUNCTION` from "
        "`mobilitydb/sql/**/*.in.sql` and `RegisterFunction(ScalarFunction"
        "(\"name\",...))` (plus aggregate / table-function variants) from "
        "`MobilityDuck/src/**/*.cpp`. Match is by **function name only**, "
        "case-insensitive. A name registered in MobilityDuck is treated as "
        "covering all its overloads; per-overload signature parity is not "
        "verified at this granularity."
    )
    lines.append("")
    lines.append("**Caveats**:")
    lines.append(
        "- A name match doesn't prove signature parity. e.g. "
        "`before(temporal, temporal)` registered in MobilityDuck does not "
        "necessarily cover MobilityDB's `before(tstzspan, temporal)`; a "
        "per-overload audit is needed for the full picture."
    )
    lines.append(
        "- Some MobilityDB names are internal helpers (gist/spgist support "
        "functions, transition functions for aggregates) — these never need "
        "user-facing SQL registration but they show as 'missing' here. "
        "Sections dominated by these are flagged in the per-section detail."
    )
    lines.append(
        "- DuckDB rejects multi-character operator tokens (`<<#`, `|>>`, "
        "`<#>`, `|=|`, `~=`); equivalent named functions are registered. "
        "See `docs/DuckDB-Parity-Gaps.md` for the catalogue."
    )
    lines.append("")
    lines.append(
        "Regenerate with `python3 scripts/parity-audit.py --mdb "
        "../MobilityDB --mduck . --out docs/parity-status.md`. Deferred "
        "families are configured at the top of that script."
    )
    lines.append("")

    lines.append("## Active-scope coverage summary")
    lines.append("")
    lines.append("| Section | MDB names | Covered | Missing | Coverage | MDB operators |")
    lines.append("|---|---:|---:|---:|---:|---:|")
    for sec, total, cov, miss, pct, _, _, ops in active_results:
        lines.append(f"| `{sec}` | {total} | {cov} | {miss} | {pct:.0f}% | {ops} |")
    lines.append(
        f"| **TOTAL (active)** | **{a_total}** | **{a_cov}** | "
        f"**{a_miss}** | **{a_pct:.0f}%** | — |"
    )
    lines.append("")

    lines.append("## Missing function names per active section")
    lines.append("")
    for sec, total, cov, miss, pct, missing, _, _ in active_results:
        if not missing:
            continue
        lines.append(f"### `{sec}` — {miss} missing of {total} ({pct:.0f}% covered)")
        lines.append("")
        for fname, count in missing:
            tag = f" ({count} overloads)" if count > 1 else ""
            lines.append(f"- `{fname}`{tag}")
        lines.append("")

    if deferred_results:
        lines.append("## Deferred families (out of scope for current sweep)")
        lines.append("")
        lines.append(
            f"These families ({', '.join(sorted(DEFERRED_FAMILIES))}) are "
            "deferred until the active temporal + geo surface stabilises. "
            "Re-include by editing `DEFERRED_FAMILIES` at the top of "
            "`scripts/parity-audit.py`. Listed here so the picture stays "
            "complete; not counted in headline coverage."
        )
        lines.append("")
        lines.append("| Section | MDB names | Covered | Missing | Coverage |")
        lines.append("|---|---:|---:|---:|---:|")
        for sec, total, cov, miss, pct, _, _, _ in deferred_results:
            lines.append(f"| `{sec}` | {total} | {cov} | {miss} | {pct:.0f}% |")
        lines.append(
            f"| **TOTAL (deferred)** | **{d_total}** | **{d_cov}** | "
            f"**{d_miss}** | **{d_pct:.0f}%** |"
        )
        lines.append("")

    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")

    return a_total, a_cov, a_pct


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mdb", default="../MobilityDB",
                    help="Path to MobilityDB checkout (default ../MobilityDB)")
    ap.add_argument("--mduck", default=".",
                    help="Path to MobilityDuck checkout (default .)")
    ap.add_argument("--out", default="docs/parity-status.md",
                    help="Output path (default docs/parity-status.md)")
    args = ap.parse_args()

    mdb_section_funcs, mdb_section_op_count, all_mdb_funcs = collect_mobilitydb(args.mdb)
    mduck_funcs, _files = collect_mobilityduck(args.mduck)

    a_total, a_cov, a_pct = write_report(
        args.out, mdb_section_funcs, mdb_section_op_count,
        all_mdb_funcs, mduck_funcs,
    )
    total_names = a_total
    total_covered = a_cov
    print(f"Wrote {args.out}")
    print(f"Coverage: {total_covered}/{total_names} "
          f"({(total_covered/total_names*100):.1f}%)")


if __name__ == "__main__":
    main()
