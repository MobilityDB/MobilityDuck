#!/usr/bin/env python3
"""Compare MobilityDB SQL surface against MobilityDuck registered surface.

Usage:
    python3 scripts/parity-audit.py \
        --mdb /path/to/MobilityDB \
        --mduck /path/to/MobilityDuck \
        --out docs/parity-status.md

A MobilityDB function counts as covered when MobilityDuck registers its
name AND registers every MobilityDB-declared value type the signature
names.  The type test is what separates a function the binding can
answer from one whose arguments it has no type for: matching on the bare
name credits `startValue(tpose)` to the `startValue` registered for
`tint`, and so reports a whole absent type family as most-of-the-way
covered.  Both sides of the test are derived — the value types from
`CREATE TYPE` in MobilityDB's own SQL, the registered ones from
`RegisterType("…")` in MobilityDuck's sources — so neither goes stale
against a hand-maintained list.

Overload granularity is still name-level: a registered name is treated
as covering every overload whose types are registered.

Three scopes:
- ACTIVE: counted in the headline coverage percentage.  Each entry is
  COVERED, MISSING (types registered, name absent), or BLOCKED (the
  signature names a value type the binding does not register).
- OUT_OF_SCOPE: PG-only entries that have no DuckDB equivalent (GiST/
  SPGiST index support, aggregate transition/combine/final/serialize
  helpers, I/O helpers like `_in/_out/_recv/_send`, planner hooks like
  `_sel/_joinsel/_supportfn/_analyze`, typmod helpers, PG geometric
  type constructors, the oid_cache catalog hook).  These appear in a
  separate appendix and do NOT contribute to the headline.
- UNREGISTERED TYPES: the value types behind every BLOCKED entry, listed
  with the sections they block, so the report names the type families
  the binding still owes.
"""

import argparse
import collections
import glob
import os
import re
import sys
from datetime import date


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


# A declaration is read from its head to the closing parenthesis of its
# argument list, matched by balancing rather than by a `[^)]*` run: a typmod
# argument like `geometry(Polygon)` closes an inner parenthesis first, and a
# run-based match ends there, losing both the remaining arguments and the
# RETURNS clause behind them.
SQL_LINE_COMMENT_RE = re.compile(r"--[^\n]*")
FUNC_HEAD_RE = re.compile(
    r"CREATE\s+(?:OR\s+REPLACE\s+)?FUNCTION\s+(\w+)\s*\(", re.IGNORECASE
)
RETURNS_RE = re.compile(r"\s*RETURNS\s+(?:SETOF\s+)?(\w+)", re.IGNORECASE)
CREATE_OP_RE = re.compile(r"CREATE\s+OPERATOR\s+(\S+)\s*\(", re.IGNORECASE)


ARG_DEFAULT_RE = re.compile(r"\bDEFAULT\b.*", re.IGNORECASE | re.DOTALL)
# The type an argument declares is its last identifier, past an optional
# argument name, and under any typmod (`geometry(Polygon)`) or array marker
# (`text[]`) — both of which leave the element type unchanged.
ARG_TYPE_RE = re.compile(r"(\w+)\s*(?:\([^)]*\))?\s*(?:\[\s*\])*\s*$")


def split_top_level(args):
    """Split an argument list on commas outside parentheses."""
    out, depth, cur = [], 0, []
    for ch in args:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and not depth:
            out.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    if "".join(cur).strip():
        out.append("".join(cur))
    return out


def signature_types(args, ret):
    """Return the lowercase type names a signature declares (args + return)."""
    types = set()
    for arg in split_top_level(args):
        arg = ARG_DEFAULT_RE.sub("", arg).strip()
        m = ARG_TYPE_RE.search(arg) if arg else None
        if m:
            types.add(m.group(1).lower())
    if ret:
        types.add(ret.lower())
    return types


def strip_sql_comments(text):
    """Drop `--` line comments so commented-out declarations are not counted."""
    return SQL_LINE_COMMENT_RE.sub("", text)


def iter_create_functions(text):
    """Yield (name, arglist, return_type) for each CREATE FUNCTION in `text`."""
    for head in FUNC_HEAD_RE.finditer(text):
        depth, i = 1, head.end()
        while i < len(text) and depth:
            if text[i] == "(":
                depth += 1
            elif text[i] == ")":
                depth -= 1
            i += 1
        if depth:
            continue
        args = text[head.end():i - 1]
        ret = RETURNS_RE.match(text, i)
        yield head.group(1), args, (ret.group(1) if ret else "")

# `CREATE TYPE x (…)` and `CREATE TYPE x;` declare a VALUE type: something a
# column holds, so a binding has to register a type of its own to answer for
# it.  `CREATE TYPE x AS (…)` declares a composite — the row shape a set-
# returning function yields, which DuckDB expresses as a STRUCT of a table
# function rather than a registered type.  Only the first kind gates coverage.
CREATE_TYPE_RE = re.compile(
    r"CREATE\s+TYPE\s+(\w+)\s*(AS\s*\(|\(|;)", re.IGNORECASE | re.DOTALL
)

# The types MobilityDuck registers with DuckDB.  Every registration reaches
# `ExtensionLoader::RegisterType(<alias>, …)`, hand-written and generated
# alike, so the alias string there is the whole registered set.
REGISTER_TYPE_RE = re.compile(r'RegisterType\s*\(\s*"([^"]+)"')

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
    # Binary / HexWKB / MFJSON parsers registered through
    # TemporalTypes::RegisterWkbFunctions (loops over scalar types).
    "tboolFromBinary", "tintFromBinary", "tfloatFromBinary", "ttextFromBinary",
    "tboolFromHexWKB", "tintFromHexWKB", "tfloatFromHexWKB", "ttextFromHexWKB",
    "tboolFromMFJSON", "tintFromMFJSON", "tfloatFromMFJSON", "ttextFromMFJSON",
    # Set FromBinary / FromHexWKB — registered in the per-set-type loop
    # (set.cpp) as alias + "FromBinary" / + "FromHexWKB".
    "intsetFromBinary",     "intsetFromHexWKB",
    "bigintsetFromBinary",  "bigintsetFromHexWKB",
    "floatsetFromBinary",   "floatsetFromHexWKB",
    "textsetFromBinary",    "textsetFromHexWKB",
    "datesetFromBinary",    "datesetFromHexWKB",
    "tstzsetFromBinary",    "tstzsetFromHexWKB",
    # Span FromBinary / FromHexWKB — registered in the per-span-type loop.
    "intspanFromBinary",     "intspanFromHexWKB",
    "bigintspanFromBinary",  "bigintspanFromHexWKB",
    "floatspanFromBinary",   "floatspanFromHexWKB",
    "datespanFromBinary",    "datespanFromHexWKB",
    "tstzspanFromBinary",    "tstzspanFromHexWKB",
    # Spanset FromBinary / FromHexWKB — registered in the per-spanset loop.
    "intspansetFromBinary",     "intspansetFromHexWKB",
    "bigintspansetFromBinary",  "bigintspansetFromHexWKB",
    "floatspansetFromBinary",   "floatspansetFromHexWKB",
    "datespansetFromBinary",    "datespansetFromHexWKB",
    "tstzspansetFromBinary",    "tstzspansetFromHexWKB",
}


def collect_mobilitydb(mdb_root):
    sql_root = os.path.join(mdb_root, "mobilitydb", "sql")
    if not os.path.isdir(sql_root):
        sys.exit(f"MobilityDB SQL dir not found: {sql_root}")

    section_funcs = collections.OrderedDict()
    section_op_count = collections.OrderedDict()
    section_func_types = collections.OrderedDict()
    all_funcs = set()
    value_types = set()

    for section in sorted(os.listdir(sql_root)):
        full = os.path.join(sql_root, section)
        if not os.path.isdir(full):
            continue
        for sql in sorted(glob.glob(f"{full}/*.in.sql")):
            rel = os.path.relpath(sql, sql_root)
            with open(sql) as f:
                text = strip_sql_comments(f.read())
            funcs = collections.Counter()
            func_types = collections.defaultdict(set)
            for name, args, ret in iter_create_functions(text):
                funcs[name] += 1
                func_types[name] |= signature_types(args, ret)
                all_funcs.add(name)
            for m in CREATE_TYPE_RE.finditer(text):
                if not m.group(2).upper().startswith("AS"):
                    value_types.add(m.group(1).lower())
            section_funcs[rel] = funcs
            section_func_types[rel] = func_types
            section_op_count[rel] = len(CREATE_OP_RE.findall(text))

    return (section_funcs, section_func_types, section_op_count, all_funcs,
            value_types)


def collect_mobilityduck(mduck_root):
    src_root = os.path.join(mduck_root, "src")
    if not os.path.isdir(src_root):
        sys.exit(f"MobilityDuck src dir not found: {src_root}")

    funcs = collections.Counter()
    files_for_func = collections.defaultdict(set)
    types = set()
    for cpp in glob.glob(f"{src_root}/**/*.cpp", recursive=True):
        with open(cpp, errors="replace") as f:
            text = f.read()
        rel = os.path.relpath(cpp, src_root)
        for regex in (REGISTER_SCALAR_RE, REGISTER_AGGR_RE,
                      REGISTER_TABLE_RE, REGISTER_MACRO_RE):
            for m in regex.finditer(text):
                funcs[m.group(1)] += 1
                files_for_func[m.group(1)].add(rel)
        types.update(m.group(1).lower() for m in REGISTER_TYPE_RE.finditer(text))
    # Synthesize known dynamically-registered names so the audit
    # reflects reality (see DYNAMIC_REGISTERED comment above).
    for name in DYNAMIC_REGISTERED:
        if name not in funcs:
            funcs[name] = 1
            files_for_func[name].add("<dynamic registration>")
    return funcs, files_for_func, types


def is_out_of_scope_section(section_relpath):
    return section_relpath in OUT_OF_SCOPE_SECTIONS


def write_report(out_path, mdb_section_funcs, mdb_section_func_types,
                 mdb_section_op_count, all_mdb_funcs, mdb_value_types,
                 mduck_funcs, mduck_types):
    mduck_funcs_lower = {k.lower(): k for k in mduck_funcs}

    active_results = []
    out_of_scope_results = []
    # Value type name -> the sections holding an entry blocked on it.
    blocking_types = collections.defaultdict(set)
    # Track out-of-scope-by-name within active sections separately, so the
    # active row reports only the addressable surface.
    for sec, funcs in mdb_section_funcs.items():
        if not funcs:
            continue
        section_oos = is_out_of_scope_section(sec)
        func_types = mdb_section_func_types[sec]
        covered, missing, blocked, oos_names = [], [], [], []
        for fname, count in sorted(funcs.items()):
            # Whole-section out-of-scope: every entry routed to OOS bucket.
            if section_oos:
                oos_names.append((fname, count))
                continue
            # Per-name out-of-scope (PG helpers): routed to OOS bucket too.
            if is_out_of_scope_name(fname):
                oos_names.append((fname, count))
                continue
            # A value type the binding does not register leaves the entry
            # unanswerable whatever its name: the argument cannot be spelled
            # in DuckDB at all.
            unregistered = sorted(
                t for t in func_types.get(fname, ())
                if t in mdb_value_types and t not in mduck_types
            )
            if unregistered:
                blocked.append((fname, count, unregistered))
                for t in unregistered:
                    blocking_types[t].add(sec)
            elif fname.lower() in mduck_funcs_lower:
                covered.append((fname, count))
            else:
                missing.append((fname, count))
        addressable = len(covered) + len(missing) + len(blocked)
        pct = (len(covered) / addressable * 100) if addressable else 0
        row = (sec, len(funcs), len(covered), len(missing), len(blocked), pct,
               missing, blocked, covered, mdb_section_op_count[sec],
               oos_names, addressable)
        if section_oos:
            out_of_scope_results.append(row)
        else:
            active_results.append(row)

    def totals(results):
        # n: addressable names only (covered + missing + blocked)
        cov = sum(r[2] for r in results)
        miss = sum(r[3] for r in results)
        blk = sum(r[4] for r in results)
        n = cov + miss + blk
        pct = (cov / n * 100) if n else 0
        return n, cov, miss, blk, pct

    a_total, a_cov, a_miss, a_blk, a_pct = totals(active_results)
    a_oos_inside = sum(len(r[10]) for r in active_results)
    section_oos_total = sum(len(r[10]) for r in out_of_scope_results)

    total_oos = a_oos_inside + section_oos_total
    lines = []
    lines.append("# MobilityDuck parity status — surface-level audit")
    lines.append("")
    lines.append(
        f"Generated {date.today().isoformat()}. **Addressable scope** "
        f"(every family, excluding PG-only helpers): "
        f"{a_cov}/{a_total} names covered ({a_pct:.1f}%)."
    )
    lines.append("")
    lines.append(
        f"Of the {a_total - a_cov} not covered, **{a_miss}** name only value "
        f"types MobilityDuck registers and are missing a registration of "
        f"their own, and **{a_blk}** are blocked on a value type the binding "
        f"does not register at all — a type family it does not carry yet. "
        f"The blocking types, and the sections behind them, are in "
        f"appendix C."
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
        "**Methodology**: parsed `CREATE FUNCTION` and `CREATE TYPE` from "
        "`mobilitydb/sql/**/*.in.sql`, and `RegisterFunction(ScalarFunction"
        "(\"name\",...))` plus `RegisterType(\"name\", …)` (with the "
        "aggregate and table-function variants) from "
        "`MobilityDuck/src/**/*.cpp`.  An entry counts as covered when the "
        "binding registers its name AND registers every value type its "
        "signature names.  `CREATE TYPE x AS (…)` composites are the row "
        "shapes a set-returning function yields, which DuckDB expresses as "
        "a STRUCT rather than a registered type, so they do not gate "
        "coverage.  Both type sets are read from the two trees, so neither "
        "is a hand-kept list that can go stale."
    )
    lines.append("")
    lines.append("**Caveats**:")
    lines.append(
        "- Matching stays name-level within a registered type set: a "
        "registered `before` is treated as covering `before(tstzspan, "
        "temporal)` as well as `before(temporal, temporal)`.  A "
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
        "OUT_OF_SCOPE_SECTIONS / OUT_OF_SCOPE_NAME_SUFFIXES sets at the top "
        "of that script control bucketing."
    )
    lines.append("")

    lines.append("## Coverage summary (addressable surface)")
    lines.append("")
    lines.append(
        "Per-section counts: `Addressable` = MDB names minus PG-only "
        "helpers (see appendix B).  `Blocked` counts the entries naming a "
        "value type the binding does not register (appendix C).  PG-only "
        "helper count shown in `OOS` column for transparency."
    )
    lines.append("")
    lines.append("| Section | Addressable | Covered | Missing | Blocked | "
                 "Coverage | OOS | MDB operators |")
    lines.append("|---|---:|---:|---:|---:|---:|---:|---:|")
    for (sec, total, cov, miss, blk, pct, _m, _b, _c, ops, oos_names,
         addressable) in active_results:
        lines.append(
            f"| `{sec}` | {addressable} | {cov} | {miss} | {blk} | "
            f"{pct:.0f}% | {len(oos_names)} | {ops} |"
        )
    lines.append(
        f"| **TOTAL** | **{a_total}** | **{a_cov}** | **{a_miss}** | "
        f"**{a_blk}** | **{a_pct:.0f}%** | **{a_oos_inside}** | — |"
    )
    lines.append("")

    lines.append("## Missing function names per section")
    lines.append("")
    lines.append(
        "Every value type these name is one MobilityDuck already "
        "registers, so each is a registration it can add as it stands."
    )
    lines.append("")
    for (sec, total, cov, miss, blk, pct, missing, _b, _c, _ops, _oos,
         addressable) in active_results:
        if not missing:
            continue
        lines.append(f"### `{sec}` — {miss} missing of {addressable} "
                     f"addressable ({pct:.0f}% covered)")
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
        for row in out_of_scope_results:
            lines.append(f"| `{row[0]}` | {len(row[10])} |")
        lines.append("")
    if a_oos_inside:
        lines.append("### PG helpers inside active sections")
        lines.append("")
        lines.append("| Section | PG helpers |")
        lines.append("|---|---:|")
        for row in active_results:
            if row[10]:
                lines.append(f"| `{row[0]}` | {len(row[10])} |")
        lines.append("")

    # ----- Appendix C: value types the binding does not register -----
    if blocking_types:
        blocked_per_type = collections.Counter()
        for row in active_results:
            for _fname, _count, unregistered in row[7]:
                for t in unregistered:
                    blocked_per_type[t] += 1
        lines.append("## Appendix C — Value types MobilityDuck does not register")
        lines.append("")
        lines.append(
            f"MobilityDB declares {len(mdb_value_types)} value types and "
            f"MobilityDuck registers {len(mduck_types)}.  A signature naming "
            "one of the types below cannot be spelled in DuckDB at all, so "
            "its entries are counted as blocked rather than missing.  A type "
            "whose whole family appears here is a family the binding does "
            "not carry."
        )
        lines.append("")
        lines.append("| Type | Entries blocked | Sections |")
        lines.append("|---|---:|---|")
        for t in sorted(blocking_types, key=lambda x: (-blocked_per_type[x], x)):
            secs = ", ".join(f"`{s}`" for s in sorted(blocking_types[t]))
            lines.append(f"| `{t}` | {blocked_per_type[t]} | {secs} |")
        lines.append("")

    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")

    return a_total, a_cov, a_miss, a_blk, a_pct


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mdb", default="../MobilityDB",
                    help="Path to MobilityDB checkout (default ../MobilityDB)")
    ap.add_argument("--mduck", default=".",
                    help="Path to MobilityDuck checkout (default .)")
    ap.add_argument("--out", default="docs/parity-status.md",
                    help="Output path (default docs/parity-status.md)")
    args = ap.parse_args()

    (mdb_section_funcs, mdb_section_func_types, mdb_section_op_count,
     all_mdb_funcs, mdb_value_types) = collect_mobilitydb(args.mdb)
    mduck_funcs, _files, mduck_types = collect_mobilityduck(args.mduck)

    a_total, a_cov, a_miss, a_blk, a_pct = write_report(
        args.out, mdb_section_funcs, mdb_section_func_types,
        mdb_section_op_count, all_mdb_funcs, mdb_value_types,
        mduck_funcs, mduck_types,
    )
    print(f"Wrote {args.out}")
    print(f"Addressable coverage: {a_cov}/{a_total} ({a_pct:.1f}%) — "
          f"{a_miss} missing, {a_blk} blocked on an unregistered value type")


if __name__ == "__main__":
    main()
