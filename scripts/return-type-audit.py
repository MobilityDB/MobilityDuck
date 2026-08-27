#!/usr/bin/env python3
"""Flag a registered function whose declared return type no executor writes.

Usage:
    python3 scripts/return-type-audit.py [repo-root]

A hand-written binding declares its result type twice: once in the
`ScalarFunction(... , LogicalType::X, Impl)` registration DuckDB reads,
and once in the `Executor::Execute<..., c_type>` template argument that
decides which C++ type is written into the result vector.  Nothing makes
the two agree.  When they disagree the executor writes one width into a
vector allocated for another -- an `Execute<..., double>` under a
registration declaring INTEGER writes a double into an int32 vector --
and DuckDB asserts, or the value is silently reinterpreted.

The audit pairs them by implementation: for every registration it
collects the declared LogicalTypes, for every implementation body the
C++ types its executors write, and reports the declared types nothing
writes.  A function registered for several types is answered by one
body, so the test is coverage (`declared - written`), not equality:
`Distance_span_span` legitimately declares INTEGER, BIGINT and DOUBLE
and must write all three.

A body reaches a type either directly or through a helper template, and
both count as written: `SpanSpanDistanceExec<int32_t>(args, result,
distance_intspan_intspan)` writes int32 as surely as an inline
`Execute<..., int32_t>`, so a one-level instantiation is read as the
type it names.  Reading only the direct form reports a function that
dispatches through a helper as writing nothing but its last branch.

Only the types in DUCK_TO_C are decidable; a registration returning a
composite, a blob or an extension type names no C++ scalar and is not
compared.  Generated sources are excluded -- their return type and their
executor come from one template, so the two cannot disagree.

Exit status is 1 when anything is flagged, so the audit can gate a
build.
"""
import glob
import os
import re
import sys

# The scalar LogicalTypes whose C++ result type is decidable from the name.
DUCK_TO_C = {
    "LogicalType::BOOLEAN": "bool",
    "LogicalType::INTEGER": "int32_t",
    "LogicalType::BIGINT": "int64_t",
    "LogicalType::UINTEGER": "uint32_t",
    "LogicalType::UBIGINT": "uint64_t",
    "LogicalType::DOUBLE": "double",
}
C_TYPES = set(DUCK_TO_C.values())

REGISTRATION = re.compile(
    r'ScalarFunction\(\s*"([^"]+)"\s*,\s*\{([^{}]*)\}\s*,\s*'
    r'([A-Za-z_:][\w:()]*)\s*,\s*([\w:]+)\s*\)')
IMPLEMENTATION = re.compile(r"void\s+\w+::(\w+)\s*\(DataChunk\s*&args.*?\n\}", re.S)
DIRECT = re.compile(
    r"(?:Unary|Binary|Ternary)Executor::Execute(?:WithNulls)?<[^>]*?,\s*([A-Za-z_0-9]+)\s*>")
INSTANTIATION = re.compile(r"\b\w+<\s*([A-Za-z_0-9]+)\s*>\s*\(")


def sources(root):
    for path in sorted(glob.glob(os.path.join(root, "src", "**", "*.cpp"), recursive=True)):
        if os.sep + "generated" + os.sep not in path:
            yield path


def audit(root):
    """[(implementation, declared, written, unwritten)] for every disagreement."""
    declared = {}
    bodies = {}
    for path in sources(root):
        text = open(path, errors="replace").read()
        for m in REGISTRATION.finditer(text):
            declared.setdefault(m.group(4).split("::")[-1], set()).add(m.group(3))
        for m in IMPLEMENTATION.finditer(text):
            bodies.setdefault(m.group(1), []).append(m.group(0))

    flagged = []
    for impl, types in sorted(declared.items()):
        want = {DUCK_TO_C[t] for t in types if t in DUCK_TO_C}
        body = "\n".join(bodies.get(impl, []))
        got = {t for t in DIRECT.findall(body) if t in C_TYPES}
        got |= {t for t in INSTANTIATION.findall(body) if t in C_TYPES}
        if not want or not got:
            continue
        unwritten = want - got
        if unwritten:
            flagged.append((impl, sorted(types), sorted(got), sorted(unwritten)))
    return flagged


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    flagged = audit(root)
    for impl, types, got, unwritten in flagged:
        print("  %s: declares %s" % (impl, types))
        print("      executors write %s; NO executor writes %s" % (got, unwritten))
    print("[%d function(s) flagged]" % len(flagged))
    return 1 if flagged else 0


if __name__ == "__main__":
    sys.exit(main())
