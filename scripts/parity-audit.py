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

Three scopes:
- ACTIVE: counted in the headline coverage percentage.
- OUT_OF_SCOPE: PG-only entries that have no DuckDB equivalent (GiST/
  SPGiST index support, aggregate transition/combine/final/serialize
  helpers, I/O helpers like `_in/_out/_recv/_send`, planner hooks like
  `_sel/_joinsel/_supportfn/_analyze`, typmod helpers, PG geometric
  type constructors, the oid_cache catalog hook).  These appear in a
  separate appendix and do NOT contribute to the headline.
- DEFERRED_FAMILIES: type families parked for a later sweep (cbuffer,
  npoint, pose, rgeo, etc.).  Listed in their own appendix.
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


# Whole SQL sections that are PG-only (no DuckDB equivalent exists).
# Match by tail of the relpath under mobilitydb/sql/.
OUT_OF_SCOPE_SECTIONS = {
    "temporal/011_span_indexes.in.sql",       # GiST/SPGiST opclasses
    "temporal/012_spanset_indexes.in.sql",    # GiST/SPGiST opclasses
    "temporal/013_set_indexes.in.sql",        # GiST/SPGiST opclasses
    "temporal/019_geo_constructors.in.sql",   # PG geometric types (point/line/box…)
    "temporal/043_temporal_gist.in.sql",      # GiST support
    "temporal/044_temporal_spgist.in.sql",    # SPGiST support
    "temporal/999_oid_cache.in.sql",          # PG catalog hook
    "geo/073_tgeo_gist.in.sql",               # GiST support
    "geo/073_tpoint_gist.in.sql",             # GiST support
    "geo/074_tgeo_spgist.in.sql",             # SPGiST support
}


# Function-name suffixes that mark PG-only helpers (no DuckDB analog).
# Matched against the tail of the function name, case-insensitive.
OUT_OF_SCOPE_NAME_SUFFIXES = (
    # Aggregate plumbing — user-facing aggregate name is what we register.
    "_transfn",
    "_combinefn",
    "_finalfn",
    "_serialize",
    "_deserialize",
    # PG planner hooks.
    "_sel",
    "_joinsel",
    "_supportfn",
    "_analyze",
    # PG type modifier helpers (DuckDB types are unparameterized).
    "_typmod_in",
    "_typmod_out",
    # PG text/binary I/O helpers — DuckDB uses casts and binders.
    "_in",
    "_out",
    "_recv",
    "_send",
)


def is_out_of_scope_name(fname):
    """Return True for PG-only helper names (suffix match)."""
    lower = fname.lower()
    # All suffixes start with `_`, so a non-empty prefix means the suffix
    # matched a "<base>_<suffix>" shape (e.g. tnumber_in, temporal_sel).
    for suf in OUT_OF_SCOPE_NAME_SUFFIXES:
        if lower.endswith(suf) and len(lower) > len(suf):
            return True
    return False


CREATE_FUNC_RE = re.compile(
    r"CREATE\s+(?:OR\s+REPLACE\s+)?FUNCTION\s+(\w+)\s*\(([^)]*)\)",
    re.IGNORECASE | re.DOTALL,
)
CREATE_OP_RE = re.compile(r"CREATE\s+OPERATOR\s+(\S+)\s*\(", re.IGNORECASE)

REGISTER_SCALAR_RE = re.compile(r'ScalarFunction\s*\(\s*"([^"]+)"', re.IGNORECASE)
REGISTER_AGGR_RE = re.compile(r'AggregateFunction\s*\(\s*"([^"]+)"')
REGISTER_TABLE_RE = re.compile(r'TableFunction\s*\(\s*"([^"]+)"')

# Project macros that wrap registration calls under a fixed-name first
# argument (e.g. `REG_EA("ever_eq", Ever_eq)` registers "ever_eq" via a
# generic ScalarFunction template).  Without these, the audit misses the
# names because the underlying ScalarFunction call uses the macro
# parameter, not a literal string.
REGISTER_MACRO_RE = re.compile(
    r'\b(?:REG_EA|REG_EA_ORD|REG_SPATIAL_EA|REG_SPATIAL_TCMP|'
    r'REG_TSPATIAL_OP|REG_TSPATIAL_TOPO|POS_REG|TIME_POS_REG|BOX_REG|'
    r'TREL_REG|EA_DWITHIN_REG|EA_REG_[A-Z_]*)\s*\(\s*"([^"]+)"'
)


# Names registered through dynamic patterns the static scan can't see —
# e.g. `StringUtil::Lower(type.GetAlias())` that resolves at runtime to
# "tbool"/"tint"/"tfloat"/"ttext", or per-type accessors registered by
# `RegisterTemporalDatumAccessor` template helpers (`minValue`,
# `maxValue`, `getValue`, `startValue`, `endValue`).  These ARE
# registered (verified by tests passing); listed here so the audit
# reflects reality.
DYNAMIC_REGISTERED = {
    # Per-subtype constructors registered through the
    # TemporalTypes::RegisterScalarFunctions loop.
    "tbool", "tint", "tfloat", "ttext",
    # Accessors registered through RegisterTemporalDatumAccessor.
    "minValue", "maxValue", "getValue", "startValue", "endValue",
}


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
        for regex in (REGISTER_SCALAR_RE, REGISTER_AGGR_RE,
                      REGISTER_TABLE_RE, REGISTER_MACRO_RE):
            for m in regex.finditer(text):
                funcs[m.group(1)] += 1
                files_for_func[m.group(1)].add(rel)
    # Synthesize known dynamically-registered names so the audit
    # reflects reality (see DYNAMIC_REGISTERED comment above).
    for name in DYNAMIC_REGISTERED:
        if name not in funcs:
            funcs[name] = 1
            files_for_func[name].add("<dynamic registration>")
    return funcs, files_for_func


def is_deferred(section_relpath):
    family = section_relpath.split("/", 1)[0]
    return family in DEFERRED_FAMILIES


def is_out_of_scope_section(section_relpath):
    return section_relpath in OUT_OF_SCOPE_SECTIONS


def write_report(out_path, mdb_section_funcs, mdb_section_op_count,
                 all_mdb_funcs, mduck_funcs):
    mduck_funcs_lower = {k.lower(): k for k in mduck_funcs}

    active_results = []
    deferred_results = []
    out_of_scope_results = []
    # Track out-of-scope-by-name within active sections separately, so the
    # active row reports only the addressable surface.
    for sec, funcs in mdb_section_funcs.items():
        if not funcs:
            continue
        section_oos = is_out_of_scope_section(sec)
        section_deferred = is_deferred(sec)
        covered, missing, oos_names = [], [], []
        for fname, count in sorted(funcs.items()):
            # Whole-section out-of-scope: every entry routed to OOS bucket.
            if section_oos:
                oos_names.append((fname, count))
                continue
            # Per-name out-of-scope (PG helpers): routed to OOS bucket too.
            if not section_deferred and is_out_of_scope_name(fname):
                oos_names.append((fname, count))
                continue
            if fname.lower() in mduck_funcs_lower:
                covered.append((fname, count))
            else:
                missing.append((fname, count))
        addressable = len(covered) + len(missing)
        pct = (len(covered) / addressable * 100) if addressable else 0
        row = (sec, len(funcs), len(covered), len(missing), pct,
               missing, covered, mdb_section_op_count[sec],
               oos_names, addressable)
        if section_oos:
            out_of_scope_results.append(row)
        elif section_deferred:
            deferred_results.append(row)
        else:
            active_results.append(row)

    def totals(results):
        # n: addressable names only (covered + missing)
        cov = sum(r[2] for r in results)
        miss = sum(r[3] for r in results)
        n = cov + miss
        pct = (cov / n * 100) if n else 0
        return n, cov, miss, pct

    def oos_total(results):
        return sum(len(r[8]) for r in results)

    a_total, a_cov, a_miss, a_pct = totals(active_results)
    d_total, d_cov, d_miss, d_pct = totals(deferred_results)
    a_oos_inside = oos_total(active_results)
    section_oos_total = sum(len(r[8]) for r in out_of_scope_results)

    total_oos = a_oos_inside + section_oos_total
    lines = []
    lines.append("# MobilityDuck parity status — surface-level audit")
    lines.append("")
    lines.append(
        f"Generated {date.today().isoformat()}. **Active addressable scope** "
        f"(temporal + geo, excluding PG-only helpers): "
        f"{a_cov}/{a_total} names covered ({a_pct:.1f}%)."
    )
    lines.append("")
    lines.append(
        f"**Out of scope** (PG-only — no DuckDB equivalent exists): "
        f"{total_oos} names skipped — {section_oos_total} from PG-only "
        f"sections (GiST/SPGiST opclasses, set/span/spanset index files, "
        f"`019_geo_constructors.in.sql` PG geometric types, "
        f"`999_oid_cache.in.sql`) plus {a_oos_inside} PG helper functions "
        f"inside active sections (`*_in/_out/_recv/_send`, `*_transfn/"
        f"_combinefn/_finalfn/_serialize/_deserialize`, `*_sel/_joinsel/"
        f"_supportfn/_analyze`, `*_typmod_in/_typmod_out`).  Listed in "
        f"appendix B; not counted in the headline."
    )
    lines.append("")
    lines.append(
        f"**Deferred families** ({', '.join(sorted(DEFERRED_FAMILIES))}) "
        "appear in appendix C and are also excluded from the headline."
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
        "- DuckDB rejects multi-character operator tokens (`<<#`, `|>>`, "
        "`<#>`, `|=|`, `~=`); equivalent named functions are registered. "
        "See `docs/DuckDB-Parity-Gaps.md` for the catalogue."
    )
    lines.append("")
    lines.append(
        "Regenerate with `python3 scripts/parity-audit.py --mdb "
        "../MobilityDB --mduck . --out docs/parity-status.md`. The "
        "OUT_OF_SCOPE_SECTIONS / OUT_OF_SCOPE_NAME_SUFFIXES / "
        "DEFERRED_FAMILIES sets at the top of that script control bucketing."
    )
    lines.append("")

    lines.append("## Active-scope coverage summary (addressable surface)")
    lines.append("")
    lines.append(
        "Per-section counts: `Addressable` = MDB names minus PG-only "
        "helpers (see appendix B).  PG-only helper count shown in `OOS` "
        "column for transparency."
    )
    lines.append("")
    lines.append("| Section | Addressable | Covered | Missing | Coverage | OOS | MDB operators |")
    lines.append("|---|---:|---:|---:|---:|---:|---:|")
    for sec, total, cov, miss, pct, _, _, ops, oos_names, addressable in active_results:
        lines.append(
            f"| `{sec}` | {addressable} | {cov} | {miss} | {pct:.0f}% | "
            f"{len(oos_names)} | {ops} |"
        )
    lines.append(
        f"| **TOTAL (active)** | **{a_total}** | **{a_cov}** | "
        f"**{a_miss}** | **{a_pct:.0f}%** | **{a_oos_inside}** | — |"
    )
    lines.append("")

    lines.append("## Missing function names per active section")
    lines.append("")
    for sec, total, cov, miss, pct, missing, _, _, _, addressable in active_results:
        if not missing:
            continue
        lines.append(f"### `{sec}` — {miss} missing of {addressable} addressable ({pct:.0f}% covered)")
        lines.append("")
        for fname, count in missing:
            tag = f" ({count} overloads)" if count > 1 else ""
            lines.append(f"- `{fname}`{tag}")
        lines.append("")

    # ----- Appendix B: out-of-scope (PG-only) -----
    lines.append("## Appendix B — Out of scope (PG-only, no DuckDB equivalent)")
    lines.append("")
    lines.append(
        "These entries are PG-specific helpers — index opclasses, "
        "aggregate transition/combine/final/serialize callbacks, planner "
        "hooks (`_sel`, `_joinsel`, `_supportfn`, `_analyze`), text/binary "
        "I/O helpers (`_in`, `_out`, `_recv`, `_send`), type modifier "
        "helpers, the `999_oid_cache` PG catalog hook, and PG geometric "
        "type constructors (`019_geo_constructors`).  None of them have "
        "DuckDB equivalents and they should not be implemented; listed "
        "here only for completeness."
    )
    lines.append("")
    if out_of_scope_results:
        lines.append("### Whole sections excluded")
        lines.append("")
        lines.append("| Section | Names |")
        lines.append("|---|---:|")
        for sec, total, _, _, _, _, _, _, oos_names, _ in out_of_scope_results:
            lines.append(f"| `{sec}` | {len(oos_names)} |")
        lines.append("")
    if a_oos_inside:
        lines.append("### PG helpers inside active sections")
        lines.append("")
        lines.append("| Section | PG helpers |")
        lines.append("|---|---:|")
        for sec, _, _, _, _, _, _, _, oos_names, _ in active_results:
            if oos_names:
                lines.append(f"| `{sec}` | {len(oos_names)} |")
        lines.append("")

    # ----- Appendix C: deferred families -----
    if deferred_results:
        lines.append("## Appendix C — Deferred families")
        lines.append("")
        lines.append(
            f"These families ({', '.join(sorted(DEFERRED_FAMILIES))}) are "
            "deferred until the active temporal + geo surface stabilises. "
            "Re-include by editing `DEFERRED_FAMILIES` at the top of "
            "`scripts/parity-audit.py`. Listed here so the picture stays "
            "complete; not counted in headline coverage."
        )
        lines.append("")
        lines.append("| Section | Addressable | Covered | Missing | Coverage |")
        lines.append("|---|---:|---:|---:|---:|")
        for sec, total, cov, miss, pct, _, _, _, _, addressable in deferred_results:
            lines.append(
                f"| `{sec}` | {addressable} | {cov} | {miss} | {pct:.0f}% |"
            )
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
    print(f"Wrote {args.out}")
    print(f"Active addressable coverage: {a_cov}/{a_total} ({a_pct:.1f}%)")


if __name__ == "__main__":
    main()
