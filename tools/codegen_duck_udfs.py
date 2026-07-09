#!/usr/bin/env python3
"""
MobilityDuck UDF generator — projects the MEOS-API catalog (meos-idl.json) into
DuckDB C++ scalar-UDF registrations. MIRRORS the MobilitySpark generator
(tools/codegen_spark_udfs.py, which mirrors JMEOS FunctionsGenerator) — same
catalog, same classify()/supported() selection logic — but emits DuckDB C++
instead of Java, and marshals via the in-process BLOB path (BlobToTemporal /
TemporalToBlob) instead of hex-WKB, since MobilityDuck links MEOS in-process.

North star: bindings are GENERATED from the catalog; this replaces hand-written
UDFs (generate-then-retire). POC scope = scalar functions with the tractable
arg/return shapes; table functions, casts, aggregates, and full per-type
polymorphism expansion are the documented next increments.

Usage: python3 tools/codegen_duck_udfs.py <path-to-meos-idl.json> [out.cpp]
"""
import json, sys, re, os
from collections import defaultdict, Counter

def norm(c):
    # Strip the `const` and `struct` keywords (the 14l catalog renders canonical
    # types as e.g. `const struct Temporal *`); word-boundary so type names that
    # merely contain the substring are untouched.
    return re.sub(r'\s+', ' ', re.sub(r'\b(const|struct)\b', '', c or '')).strip()

def base(canon):
    t = norm(canon)
    if "(*" in t or "()" in t or t.endswith("**"):
        return "__INTERNAL__"
    return t.replace("*", "").strip()

# ---- DuckDB type + marshalling maps (the Duck-specific part) ----
# Pointer args/returns that travel as the DuckDB BLOB-backed custom type and
# marshal in-process via malloc+memcpy (BlobToTemporal) / Value::BLOB(TemporalToBlob).
# %s = the per-row input string_t (arg) / the MEOS pointer (ret).
PTR_IN  = {  # MEOS base type -> (DuckDB arg LogicalType, "C++ expr producing the MEOS ptr from string_t `%s`")
    "Temporal":     ("MD_TEMPORAL", "BlobToTemporal(%s)"),
    "TInstant":     ("MD_TEMPORAL", "BlobToTemporal(%s)"),
    "TSequence":    ("MD_TEMPORAL", "BlobToTemporal(%s)"),
    "TSequenceSet": ("MD_TEMPORAL", "BlobToTemporal(%s)"),
    "Set":          ("LogicalType::BLOB", "BlobToSet(%s)"),
    "Span":         ("LogicalType::BLOB", "BlobToSpan(%s)"),
    "SpanSet":      ("LogicalType::BLOB", "BlobToSpanSet(%s)"),
    "STBox":        ("LogicalType::BLOB", "BlobToStbox(%s)"),
    "TBox":         ("LogicalType::BLOB", "BlobToTbox(%s)"),
}
PTR_RET = {  # MEOS base type -> (DuckDB ret LogicalType, "C++ expr producing string_t from MEOS ptr `%s` in `result`")
    "Temporal":     ("MD_TEMPORAL", "TemporalToBlob(result, %s)"),
    "TInstant":     ("MD_TEMPORAL", "TemporalToBlob(result, %s)"),
    "TSequence":    ("MD_TEMPORAL", "TemporalToBlob(result, %s)"),
    "TSequenceSet": ("MD_TEMPORAL", "TemporalToBlob(result, %s)"),
    "Set":          ("LogicalType::BLOB", "SetToBlob(result, %s)"),
    "Span":         ("LogicalType::BLOB", "SpanToBlob(result, %s)"),
    "SpanSet":      ("LogicalType::BLOB", "SpanSetToBlob(result, %s)"),
    "STBox":        ("LogicalType::BLOB", "StboxToBlob(result, %s)"),
    "TBox":         ("LogicalType::BLOB", "TboxToBlob(result, %s)"),
}
# Scalar (by-value) args/returns -> (DuckDB LogicalType, C++ scalar type)
SCALAR = {
    "int":          ("LogicalType::INTEGER",  "int32_t"),
    "int32":        ("LogicalType::INTEGER",  "int32_t"),
    "int32_t":      ("LogicalType::INTEGER",  "int32_t"),
    "int64":        ("LogicalType::BIGINT",   "int64_t"),
    "int64_t":      ("LogicalType::BIGINT",   "int64_t"),
    "double":       ("LogicalType::DOUBLE",   "double"),
    "float8":       ("LogicalType::DOUBLE",   "double"),
    "bool":         ("LogicalType::BOOLEAN",  "bool"),
    "TimestampTz":  ("LogicalType::TIMESTAMP_TZ", "timestamp_tz_t"),
    "DateADT":      ("LogicalType::DATE",     "date_t"),
}
OUTPRIM = {"int *": "LogicalType::INTEGER", "int64_t *": "LogicalType::BIGINT",
           "double *": "LogicalType::DOUBLE", "bool *": "LogicalType::BOOLEAN"}

def classify(f):
    """Mirror of the Spark generator: split params into (in_params, out_canon|None).
    Drops a trailing size_t* buffer-length out-param; on bool/void return with a
    single trailing writable pointer, that pointer is the out-param."""
    params = list(f["params"])
    if params and "const" not in params[-1]["canonical"] and norm(params[-1]["canonical"]) == "size_t *":
        params = params[:-1]
    rt = norm(f["returnType"]["canonical"])
    if rt in ("bool", "void") and params:
        lastc = params[-1]["canonical"]; lastn = norm(lastc)
        writable = "const" not in lastc and lastn.endswith("*")
        others = [p for p in params[:-1] if "const" not in p["canonical"]
                  and norm(p["canonical"]).endswith("*")
                  and (norm(p["canonical"]) in OUTPRIM or base(p["canonical"]) in PTR_RET)]
        if writable and not others and (lastn in OUTPRIM or base(lastc) in PTR_RET):
            return params[:-1], lastc
    return params, None

def arg_type(canon):
    """(duck_type, marshal_expr_or_None) for an input param, else None if unmappable."""
    nc = norm(canon); b = base(canon)
    if b in PTR_IN and nc.endswith("*"):
        return PTR_IN[b]
    if b in SCALAR and "*" not in nc:
        dt, _ = SCALAR[b]
        return (dt, None)               # by-value scalar
    if b == "char" and nc.count("*") == 1:
        return ("LogicalType::VARCHAR", None)
    if b == "text" and nc.endswith("*"):
        return ("LogicalType::VARCHAR", None)
    return None

def ret_type(f, out_canon):
    """(duck_ret_type, kind) where kind in {ptr,scalar,outprim,outptr}. None if unmappable."""
    if out_canon is not None:
        n = norm(out_canon)
        if n in OUTPRIM:           return (OUTPRIM[n], "outprim")
        if base(out_canon) in PTR_RET:  return (PTR_RET[base(out_canon)][0], "outptr")
        return None
    rc = f["returnType"]["canonical"]; nc = norm(rc); b = base(rc)
    if b in PTR_RET and nc.endswith("*"):  return (PTR_RET[b][0], "ptr")
    if b in SCALAR and "*" not in nc:      return (SCALAR[b][0], "scalar")
    if b == "Interval" and nc.endswith("*"):  # owned ptr -> by-value interval_t (TakeInterval)
        return ("LogicalType::INTERVAL", "scalar")
    if b == "text" and nc.endswith("*"):       # owned text* varlena -> VARCHAR (TakeText)
        return ("LogicalType::VARCHAR", "scalar")
    if b == "char" and nc.endswith("*"):       # owned C-string -> VARCHAR (TakeCString)
        return ("LogicalType::VARCHAR", "scalar")
    return None

# The temporal type families the BINDING registers as DuckDB types (SoT = the binding's
# RegisterType calls in src/). A generated UDF is in scope only when EVERY type family its
# name references is registered; a name that references an as-yet-unregistered family
# (e.g. tcbuffer_to_tfloat, tbigint_to_th3index, tgeompoint_to_tnpoint, ttext_to_tjsonb)
# would register under the wrong DuckDB type / be uncallable, and would drag an unused MEOS
# module — so it is OUT OF SCOPE until that family's fast-follow wave registers the type,
# at which point moving the token into REGISTERED_FAMILIES re-includes it automatically.
# CODEGEN-REGULAR-EXCEPTION: registered-type-scope CLASSIFICATION — a positive allowlist
# (= the binding's RegisterType set), reason-audited in main() as "unregistered-family";
# NOT a skip-to-pass family filter. When a family is registered, add its token here.
REGISTERED_FAMILIES = {
    "temporal", "tnumber", "tint", "tbigint", "tfloat", "tbool", "ttext",
    "tgeompoint", "tgeogpoint", "tgeometry", "tgeography", "tgeo", "tspatial", "tquadbin",
}
# Every temporal family token the catalog function names use; those NOT in REGISTERED_FAMILIES
# are the fast-follow families whose DuckDB type the binding does not register yet.
KNOWN_FAMILIES = REGISTERED_FAMILIES | {
    "th3index", "tcbuffer", "tnpoint", "tpose", "trgeometry", "trgeo", "tpcpoint", "tpcpatch",
    "tjsonb", "cbuffer", "npoint", "nsegment", "pose", "pcpoint", "pcpatch", "jsonb", "h3index",
}
def unregistered_family_ref(name):
    """The first unregistered family token the name references, else None (in scope)."""
    bad = (set(name.split("_")) & KNOWN_FAMILIES) - REGISTERED_FAMILIES
    return sorted(bad)[0] if bad else None

def supported(f):
    """Reason string if NOT emittable (mirrors Spark's supported()), else None."""
    name = f["name"]
    if name.startswith("meos_internal") or (f.get("group") or "").startswith("meos_internal"):
        return "internal"
    if not f.get("sqlfn"):
        return "no-sqlfn"            # not user-facing SQL surface
    # type I/O machinery (in/out/send/recv) — these are the VARCHAR/binary CASTS, registered
    # via RegisterCastFunctions, NOT standalone scalar UDFs. The real text fns are asText/interp.
    if re.search(r'_(out|in|send|recv)$', f.get("sqlfn") or ""):
        return "io-cast"
    # registered-type scope: a UDF touching a family whose DuckDB type is not registered yet
    # is out of scope until its fast-follow wave (see REGISTERED_FAMILIES above).
    u = unregistered_family_ref(name)
    if u is not None:
        return "unregistered-family:" + u
    in_params, out = classify(f)
    if ret_type(f, out) is None:
        return "ret:" + norm(f["returnType"]["canonical"])
    for p in in_params:
        if arg_type(p["canonical"]) is None:
            return "arg:" + norm(p["canonical"])
    return None

def family(f):
    """Type-family bucket for file organization (compile-flag gating). Mirrors the
    by-family convention; uses the @ingroup group if present, else the source file."""
    g = f.get("group") or ""
    if g.startswith("meos_"): return g[len("meos_"):]
    return (f.get("file") or "meos").replace(".h", "")

# ---------------- Full executor-body emit (COMPILABLE POC subset) ----------------
# Subset guaranteed to compile against the hand binding's helpers:
#   - exactly ONE input param,
#   - arg is a generic `const Temporal *`  (marshalled via BlobToTemporal),
#   - return is Temporal* (TemporalToBlob) or a by-value scalar (int/int64/double/bool).
# Registered for EVERY temporal DuckDB type via TemporalTypes::AllTypes() — the same
# polymorphism the hand binding uses. (Spans/sets/boxes/binary/out-params = next increment.)
SCALAR_RET_CPP = {"int": ("int32_t", "LogicalType::INTEGER"),
                  "int32_t": ("int32_t", "LogicalType::INTEGER"),
                  "int64_t": ("int64_t", "LogicalType::BIGINT"),
                  "double": ("double", "LogicalType::DOUBLE"),
                  "bool": ("bool", "LogicalType::BOOLEAN")}
# By-value scalar returns the TEMPORAL detectors accept: the identity-marshalled ones
# (SCALAR_RET_CPP) plus the time types that need a conversion on return (TimestampTz via
# TakeTimestamp, DateADT via FromMeosDate — handled in scalar_emit3/scalar_ret_duck).
BYVAL_RET = set(SCALAR_RET_CPP) | {"TimestampTz", "DateADT"}

def is_pred_int(f):
    """ever_*/always_* return MEOS `int` but are semantically BOOLEAN (everEq answers
    'is it EVER equal?' = a single bool, vs teq = a pointwise tbool). The spatial
    ever/always relationships (catalog group `*_rel_ever`/`*_rel_always`: aDisjoint,
    eContains, eCovers, eIntersects, eTouches, eDwithin, ...) are the same shape — a
    scalar `int` 0/1 that is a SQL BOOLEAN — but their names are the bare a*/e* form, so
    key on the catalog group too (mirrors the increment-18 geo-comparison BOOLEAN rule).
    The `*Pairs` array variants return `int *` (a SETOF) and are excluded by the no-`*`
    guard."""
    rc = f["returnType"]["canonical"]
    if base(rc) not in ("int", "int32_t") or "*" in norm(rc):
        return False
    return (re.match(r'(ever|always)_', f["name"]) is not None
            or re.search(r'_rel_(ever|always)$', f.get("group") or "") is not None)

def scalar_ret_duck(f):
    """DuckDB registration return type for a by-value scalar return."""
    if is_pred_int(f):
        return "LogicalType::BOOLEAN"
    rb = base(f["returnType"]["canonical"])
    if rb == "Interval":     return "LogicalType::INTERVAL"
    if rb == "TimestampTz":  return "LogicalType::TIMESTAMP_TZ"
    if rb == "DateADT":      return "LogicalType::DATE"
    if rb in ("text", "char"): return "LogicalType::VARCHAR"
    return SCALAR_RET_CPP[rb][1]

def scalar_emit3(f):
    """(call_var_ctype, executor_RET, return_expr) — for ever_/always_ the MEOS call
    yields int but the UDF returns bool via (r != 0); an owned MEOS Interval* is
    converted+freed via TakeInterval (preamble helper)."""
    if is_pred_int(f):
        return ("int32_t", "bool", "(r != 0)")
    rb = base(f["returnType"]["canonical"])
    if rb == "Interval":     return ("MeosInterval *", "interval_t", "TakeInterval(r)")
    if rb == "TimestampTz":  return ("TimestampTz", "timestamp_tz_t", "TakeTimestamp(r)")
    if rb == "DateADT":      return ("DateADT", "date_t", "FromMeosDate((int32_t) r)")
    if rb == "text":         return ("text *", "string_t", "TakeText(result, r)")
    if rb == "char":         # const char* = borrowed/static (no free); char* = owned (free)
        if "const" in (f["returnType"]["canonical"] or ""):
            return ("const char *", "string_t", "StringVector::AddString(result, r)")
        return ("char *", "string_t", "TakeCString(result, r)")
    ct = SCALAR_RET_CPP[rb][0]
    return (ct, ct, "r")

# By-value/owned-scalar return marshalling keyed by the MEOS return base type — used by the
# container (set/span) u_scalar branches so they handle time/Interval returns like the
# temporal detectors do. (call_var_ctype, executor_RET, return_expr).
def byval_ret3(e):
    if e == "Interval":     return ("MeosInterval *", "interval_t", "TakeInterval(r)")
    if e == "TimestampTz":  return ("TimestampTz", "timestamp_tz_t", "TakeTimestamp(r)")
    if e == "DateADT":      return ("DateADT", "date_t", "FromMeosDate((int32_t) r)")
    if e == "text":         return ("text *", "string_t", "TakeText(result, r)")
    if e == "char":         return ("char *", "string_t", "TakeCString(result, r)")
    return (SCALAR_RET_CPP[e][0], SCALAR_RET_CPP[e][0], "r")
def byval_ret_duck(e):
    return {"Interval": "LogicalType::INTERVAL", "TimestampTz": "LogicalType::TIMESTAMP_TZ",
            "DateADT": "LogicalType::DATE", "text": "LogicalType::VARCHAR"}.get(
                e, SCALAR_RET_CPP[e][1] if e in SCALAR_RET_CPP else None)

# C-name prefix -> the concrete DuckDB temporal type(s) a fn registers for.
# Core binding temporal types are TINT/TFLOAT/TBOOL/TTEXT (TBIGINT/geo = separate
# families, gated elsewhere). "all" = the generic AllTypes() loop.
CORE_TYPES = {
    "tint":   ["TemporalTypes::tint()"],
    "tbigint":["TemporalTypes::tbigint()"],
    "tfloat": ["TemporalTypes::tfloat()"],
    "tbool":  ["TemporalTypes::tbool()"],
    "ttext":  ["TemporalTypes::ttext()"],
    "tnumber":["TemporalTypes::tint()", "TemporalTypes::tfloat()"],
}
# Geo TEMPORAL types are Temporal* blobs (BlobToTemporal works). Accessor class names
# are inconsistent in the hand binding (verified live: TgeompointType/TgeogpointType/
# TGeometryTypes/TGeographyTypes) — used as-is; canonical type names from meos_catalog.c.
# Functions with GSERIALIZED*/Geo* args or returns auto-EXCLUDE (supported() rejects them),
# so only the Temporal-only geo shapes (tgeo×tgeo comparison) emit here.
GEO_TYPES = {
    "tgeompoint": "TgeompointType::tgeompoint()", "tgeogpoint": "TgeogpointType::tgeogpoint()",
    "tgeometry":  "TGeometryTypes::tgeometry()",  "tgeography": "TGeographyTypes::tgeography()",
}
GEO_ALLTYPES = list(GEO_TYPES.values())
def reg_scope(name):
    """('all', None) generic | ('types', [accessors]) specific | None = not core family.
    Resolves the temporal type from the MobilityDB naming convention: a PREFIX
    (temporal_*/tint_*/...) OR a same-type SUFFIX (e.g. tand_tbool_tbool,
    distance_tfloat_tfloat, ever_eq_temporal_temporal). Mixed-type suffixes are
    skipped (ambiguous)."""
    # Bounding-box TOPOLOGICAL ops (same/contains/contained/overlaps/adjacent) have
    # TYPE-DEPENDENT bbox semantics, so MobilityDB backs each bbox shape with a distinct
    # MEOS function: the generic *_temporal_temporal compares ONLY the time bbox, *_tnumber_
    # tnumber the value+time (tbox), *_tspatial_tspatial the space+time (stbox). The generic
    # C signature is `(Temporal *, Temporal *)`, so the plain name-token heuristic below would
    # scope *_temporal_temporal to ("all") temporal types — registering the TIME-ONLY op for
    # number and spatial types too. Since DuckDB overload resolution is last-registration-wins
    # among the alias-BLOB overloads, that generic then SHADOWS the correct value-/space-aware
    # backing (proven: ~=/same_bbox/@>/… on tgeompoint × tgeompoint silently compared only time).
    # Scope each explicitly to the types whose bbox it actually models:
    #   • *_temporal_temporal  -> tbool, ttext            (bbox = tstzspan, purely temporal)
    #   • *_tnumber_tnumber     -> tint, tfloat, tbigint   (bbox = tbox; all three are tnumber_type
    #                                                        per meos_catalog.c:1234, tbigint incl.)
    #   • *_tspatial_tspatial   -> the 4 geo types         (handled by the tspatial/tgeo rules below)
    _bbox_topo = re.match(r'^(?:same|contains|contained|overlaps|adjacent)_(\w+)$', name)
    if _bbox_topo:
        base = _bbox_topo.group(1)
        if base == "temporal_temporal":
            return ("types", CORE_TYPES["tbool"] + CORE_TYPES["ttext"])
        if base == "tnumber_tnumber":
            return ("types", CORE_TYPES["tint"] + CORE_TYPES["tfloat"] + CORE_TYPES["tbigint"])
    if name.startswith("temporal_"):
        return ("all", None)
    for pre, accs in CORE_TYPES.items():
        if name.startswith(pre + "_"):
            return ("types", accs)
    # geo temporal types: a specific <geotype>_* prefix, or the generic "tgeo" token
    # (ever_eq_tgeo_tgeo) -> all 4 geo types. geometry-coupled variants auto-exclude.
    for pre, acc in GEO_TYPES.items():
        if name.startswith(pre + "_"):
            return ("types", [acc])
    # the abstract point supertype tpoint_* covers both point subtypes (getX/azimuth/
    # speed/... live under this MEOS name); geometry-coupled variants auto-exclude.
    if name.startswith("tpoint_"):
        return ("types", [GEO_TYPES["tgeompoint"], GEO_TYPES["tgeogpoint"]])
    # the abstract spatial supertype tspatial_* covers all four geo temporal types with
    # type-preserving results (setSRID/transform/transformPipeline preserve the operand
    # type; asText/asEWKT return text); geometry-coupled variants auto-exclude.
    if name.startswith("tspatial_"):
        return ("types", GEO_ALLTYPES)
    if re.search(r'_(tgeo|tspatial)(?=_|$)', name):
        return ("types", GEO_ALLTYPES)
    # temporal-type token ANYWHERE in the name (always_eq_tint_int, ever_lt_tfloat_tfloat,
    # tdistance_tfloat_tfloat, teq_temporal_temporal). Skip if >1 DISTINCT temporal type
    # appears (mixed/ambiguous) — geo tokens (tgeompoint/tgeo/th3index/tnpoint) aren't in
    # the set, so geo/extended fns correctly fall through to None (their own gated family).
    toks = re.findall(r'_(temporal|tnumber|tint|tbigint|tfloat|tbool|ttext)(?=_|$)', name)
    if toks:
        if len(set(toks)) > 1:
            return None
        t1 = toks[0]
        if t1 == "temporal":
            return ("all", None)
        return ("types", CORE_TYPES[t1])
    return None  # tbigint_*/tgeompoint_*/etc. belong to their own (gated) family file

# Output DuckDB type for a TEMPORAL-returning fn (the return-type-from-name lever):
# a `*_to_t<x>` conversion CHANGES type -> the target accessor; otherwise the fn
# PRESERVES its input type -> the arg's accessor (passed in).
TO_TYPE = {"tint": "TemporalTypes::tint()", "tbigint": "TemporalTypes::tbigint()",
           "tfloat": "TemporalTypes::tfloat()",
           "tbool": "TemporalTypes::tbool()", "ttext": "TemporalTypes::ttext()",
           "tgeometry": "TGeometryTypes::tgeometry()", "tgeography": "TGeographyTypes::tgeography()",
           "tgeompoint": "TgeompointType::tgeompoint()", "tgeogpoint": "TgeogpointType::tgeogpoint()"}
def ret_temporal_type(name, arg_acc, group="", sql_ret=None):
    # A single, unambiguous SQL return subtype from the catalog names the output
    # temporal type directly (the catalog is the SoT). The name heuristics below are
    # the fallback for functions the catalog leaves input-polymorphic (sqlReturnTypeAll),
    # where the return preserves the input type via arg_acc.
    if sql_ret and sql_ret in TO_TYPE:
        return TO_TYPE[sql_ret]
    # temporal comparison ops (teq/tne/tlt/tle/tgt/tge_*) return a tbool, NOT the input type
    if re.match(r't(eq|ne|lt|le|gt|ge)_', name):
        return "TemporalTypes::tbool()"
    # temporal spatial relationships (catalog group `*_rel_temp`: tContains/tDisjoint/
    # tIntersects/tTouches/tDwithin) answer a pointwise predicate over time -> a tbool, not
    # the operand geo type (mirrors the teq->tbool rule and the *_rel_ever scalar-BOOLEAN one).
    if re.search(r'_rel_temp$', group or ""):
        return "TemporalTypes::tbool()"
    # the temporal distance tDistance(a,b) is always a tfloat — whatever the operands
    # (tnumber or tgeo); the generic MEOS `Temporal *` return doesn't carry that, so the
    # name carries it (mirrors the teq->tbool rule above).
    if re.match(r'tdistance_', name):
        return "TemporalTypes::tfloat()"
    # `<x>_to_<y>` conversions CHANGE type to the target -> the `_to_` suffix names it
    # (geo targets tgeometry/tgeography/tgeompoint/tgeogpoint added alongside the base ones).
    m = re.search(r'_to_(tint|tbigint|tfloat|tbool|ttext|tgeometry|tgeography|tgeompoint|tgeogpoint)$', name)
    return TO_TYPE[m.group(1)] if m else arg_acc

# ---------------- SET family (additive; the temporal path is left untouched) ----------------
# Self-contained Blob<->Set marshalling reuses the hand binding's exact method
# (malloc+memcpy in; set_mem_size out). Per-element accessors mirror CORE_TYPES.
SET_TYPES = {
    "intset":   "SetTypes::intset()",   "bigintset": "SetTypes::bigintset()",
    "floatset": "SetTypes::floatset()", "textset":   "SetTypes::textset()",
    "dateset":  "SetTypes::dateset()",  "tstzset":   "SetTypes::tstzset()",
}
def set_reg_scope(name):
    """('all',None) for generic set_* | ('types',[acc]) for <elem>set_* | None."""
    if name.startswith("set_"):
        return ("all", None)
    for pre, acc in SET_TYPES.items():
        if name.startswith(pre + "_") or name == pre:
            return ("types", [acc])
    return None
def ret_set_type(name, arg_acc):
    # `*_to_<settype>` conversion CHANGES type -> target; else preserve the arg's set type.
    m = re.search(r'_to_(intset|bigintset|floatset|textset|dateset|tstzset)$', name)
    return SET_TYPES[m.group(1)] if m else arg_acc

# Element scalar type -> the Set type it implies (for contains/contained/left/...
# predicates the set type is picked by the element, not the name). Only the
# already-marshalled scalars (text/date deferred — need new marshalling).
ELEM_TO_SET = {
    "int": "SetTypes::intset()", "int32_t": "SetTypes::intset()",
    "int64_t": "SetTypes::bigintset()", "double": "SetTypes::floatset()",
    "TimestampTz": "SetTypes::tstzset()", "DateADT": "SetTypes::dateset()",
    "text": "SetTypes::textset()",
}
def selem(p):
    """The set-element key for a predicate scalar arg, or None. By-value scalars
    (int/double/date/tstz) OR an owned `text *` varlena."""
    b = base(p["canonical"]); ptr = norm(p["canonical"]).endswith("*")
    if b == "text" and ptr: return "text"
    if b in ELEM_TO_SET and not ptr: return b
    return None
def poc_set(f):
    """Set-family shapes. Returns (kind, duck_ret) or None.
      - element-typed predicates (accessor from the scalar element type, NOT the name):
        (Set,scalar)->bool / (scalar,Set)->bool  [contains/contained/left/right/over*]
      - name-scoped (set_reg_scope): unary (Set)->Set|scalar, binary (Set,Set)->bool|Set"""
    if supported(f) is not None: return None
    # aggregate transition/final/combine internals are NOT scalar UDFs (separate shape)
    if re.search(r'_(transfn|finalfn|combinefn)$', f["name"]): return None
    ins, out = classify(f)
    if out is not None: return None
    rb = base(f["returnType"]["canonical"]); rn = norm(f["returnType"]["canonical"])
    setp = lambda p: base(p["canonical"]) == "Set" and norm(p["canonical"]).endswith("*")
    if len(ins) == 2 and rb == "bool" and "*" not in rn:
        e0, e1 = selem(ins[0]), selem(ins[1])
        if setp(ins[0]) and e1: return ("setsc:" + e1, "LogicalType::BOOLEAN")
        if setp(ins[1]) and e0: return ("scset:" + e0, "LogicalType::BOOLEAN")
    # generic (Set,Set)->Set|bool — base-typed (register over AllTypes), NOT name-scoped:
    # the C names are <op>_set_set (union/minus/intersection/contains/overlaps/...), which the
    # name-based set_reg_scope misses. Mirror poc_span's contp detection.
    if len(ins) == 2 and setp(ins[0]) and setp(ins[1]):
        if rb == "Set" and rn.endswith("*"):  return ("b_set", "LogicalType::BLOB")
        if rb == "bool" and "*" not in rn:    return ("b_bool", "LogicalType::BOOLEAN")
    # (Set, scalar element) -> Set : setUnion/setMinus/setIntersection with an element value
    if len(ins) == 2 and rb == "Set" and rn.endswith("*"):
        e1 = selem(ins[1])
        if setp(ins[0]) and e1: return ("setsc_set:" + e1, "LogicalType::BLOB")
    if set_reg_scope(f["name"]) is None: return None
    if len(ins) == 1 and setp(ins[0]):
        if rb == "Set" and rn.endswith("*"):       return ("u_set", "LogicalType::BLOB")
        if rb in BYVAL_RET and "*" not in rn: return ("u_scalar:" + rb, byval_ret_duck(rb))
        if rb in ("text", "char") and rn.endswith("*"): return ("u_scalar:" + rb, "LogicalType::VARCHAR")
        return None
    return None

def emit_set(f, kind):
    name = f["name"]
    if kind.startswith("setsc:"):   # (Set, scalar) -> bool
        e = kind.split(':')[1]
        if e == "text":
            return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                    f"    EnsureMeosThreadInitialized();\n"
                    f"    BinaryExecutor::Execute<string_t, string_t, bool>(args.data[0], args.data[1], result, args.size(),\n"
                    f"        [&](string_t a, string_t a2) {{\n"
                    f"            Set *s = BlobToSet(a);\n            text *t2 = MakeText(a2);\n"
                    f"            bool r = {name}(s, t2);\n            free(t2); free(s);\n"
                    f"            return r;\n        }});\n}}\n")
        _dt, cpp2, marsh = SCALAR_ARG[e]
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::Execute<string_t, {cpp2}, bool>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t a, {cpp2} a2) {{\n"
                f"            Set *s = BlobToSet(a);\n            bool r = {name}(s, {marsh});\n            free(s);\n"
                f"            return r;\n        }});\n}}\n")
    if kind.startswith("setsc_set:"):   # (Set, scalar element) -> Set
        e = kind.split(':')[1]
        if e == "text":
            return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                    f"    EnsureMeosThreadInitialized();\n"
                    f"    BinaryExecutor::Execute<string_t, string_t, string_t>(args.data[0], args.data[1], result, args.size(),\n"
                    f"        [&](string_t a, string_t a2) {{\n"
                    f"            Set *s = BlobToSet(a);\n            text *t2 = MakeText(a2);\n"
                    f"            Set *r = {name}(s, t2);\n            free(t2); free(s);\n"
                    f"            return SetToBlob(result, r);\n        }});\n}}\n")
        _dt, cpp2, marsh = SCALAR_ARG[e]
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::Execute<string_t, {cpp2}, string_t>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t a, {cpp2} a2) {{\n"
                f"            Set *s = BlobToSet(a);\n            Set *r = {name}(s, {marsh});\n            free(s);\n"
                f"            return SetToBlob(result, r);\n        }});\n}}\n")
    if kind.startswith("scset:"):   # (scalar, Set) -> bool
        e = kind.split(':')[1]
        if e == "text":
            return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                    f"    EnsureMeosThreadInitialized();\n"
                    f"    BinaryExecutor::Execute<string_t, string_t, bool>(args.data[0], args.data[1], result, args.size(),\n"
                    f"        [&](string_t a1, string_t b) {{\n"
                    f"            text *t1 = MakeText(a1);\n            Set *s = BlobToSet(b);\n"
                    f"            bool r = {name}(t1, s);\n            free(t1); free(s);\n"
                    f"            return r;\n        }});\n}}\n")
        _dt, cpp1, marsh = SCALAR_ARG[e]; marsh = marsh.replace("a2", "a1")
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::Execute<{cpp1}, string_t, bool>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&]({cpp1} a1, string_t b) {{\n"
                f"            Set *s = BlobToSet(b);\n            bool r = {name}({marsh}, s);\n            free(s);\n"
                f"            return r;\n        }});\n}}\n")
    if kind == "u_set":             # (Set) -> Set (pointer return; MEOS NULL -> SQL NULL)
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(args.data[0], result, args.size(),\n"
                f"        [&](string_t in, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            Set *s = BlobToSet(in);\n            Set *r = {name}(s);\n            free(s);\n"
                f"            return SetToBlobN(result, r, mask, idx);\n        }});\n}}\n")
    if kind.startswith("u_scalar:"):
        cct, rett, rexpr = byval_ret3(kind.split(':')[1])
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    UnaryExecutor::Execute<string_t, {rett}>(args.data[0], result, args.size(),\n"
                f"        [&](string_t in) {{\n"
                f"            Set *s = BlobToSet(in);\n            {cct} r = {name}(s);\n            free(s);\n"
                f"            return {rexpr};\n        }});\n}}\n")
    if kind == "b_set":             # (Set,Set) -> Set (pointer return; MEOS NULL -> SQL NULL)
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            Set *s1 = BlobToSet(a);\n            Set *s2 = BlobToSet(b);\n"
                f"            Set *r = {name}(s1, s2);\n            free(s1); free(s2);\n"
                f"            return SetToBlobN(result, r, mask, idx);\n        }});\n}}\n")
    else:  # b_bool
        inner = (f"            bool r = {name}(s1, s2);\n            free(s1); free(s2);\n            return r;")
        rett = "bool"
    return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    BinaryExecutor::Execute<string_t, string_t, {rett}>(args.data[0], args.data[1], result, args.size(),\n"
            f"        [&](string_t a, string_t b) {{\n"
            f"            Set *s1 = BlobToSet(a);\n            Set *s2 = BlobToSet(b);\n{inner}\n        }});\n}}\n")

def poc_emittable(f):
    """Guaranteed-compilable AND correctly-scoped subset, or None.
    To avoid emitting a wrong registration we take only the UNAMBIGUOUS shapes:
      - scalar return (int/int64/double/bool) — correct for any temporal type; OR
      - Temporal return from a GENERIC `temporal_*` transform — preserves temptype,
        so ret==arg type is correct (conversions *_to_t* that CHANGE type are
        excluded here; they need return-type-from-name = a later increment)."""
    if supported(f) is not None:
        return None
    ins, out = classify(f)
    if out is not None or len(ins) != 1:
        return None
    if base(ins[0]["canonical"]) != "Temporal" or not norm(ins[0]["canonical"]).endswith("*"):
        return None
    sc = reg_scope(f["name"])
    if sc is None:
        return None
    rb = base(f["returnType"]["canonical"]); rn = norm(f["returnType"]["canonical"])
    if rb == "Temporal" and rn.endswith("*"):
        return ("temporal", "MD_TEMPORAL")   # ret type resolved per-accessor via ret_temporal_type
    if rb in BYVAL_RET and "*" not in rn:
        return ("scalar:" + rb, scalar_ret_duck(f))
    if rb in ("Interval", "text", "char") and rn.endswith("*"):   # owned-pointer scalar return (duration / text / cstring)
        return ("scalar:" + rb, scalar_ret_duck(f))
    return None

def emit_body(f, kind):
    name, sqlfn = f["name"], f["sqlfn"]
    if kind == "temporal":          # (Temporal) -> Temporal (pointer return; MEOS NULL -> SQL NULL)
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(args.data[0], result, args.size(),\n"
                f"        [&](string_t in, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            Temporal *t = BlobToTemporal(in);\n"
                f"            Temporal *r = {name}(t);\n"
                f"            free(t);\n"
                f"            return TemporalToBlobN(result, r, mask, idx);\n"
                f"        }});\n}}\n")
    cct, rett, rexpr = scalar_emit3(f)
    return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    UnaryExecutor::Execute<string_t, {rett}>(args.data[0], result, args.size(),\n"
            f"        [&](string_t in) {{\n"
            f"            Temporal *t = BlobToTemporal(in);\n"
            f"            {cct} r = {name}(t);\n"
            f"            free(t);\n"
            f"            return {rexpr};\n"
            f"        }});\n}}\n")

# 2nd-arg scalar marshalling for binary Temporal+scalar fns: base -> (duck arg type, cpp type, MEOS-call expr from `a2`)
SCALAR_ARG = {
    "int":         ("LogicalType::INTEGER", "int32_t", "a2"),
    "int32_t":     ("LogicalType::INTEGER", "int32_t", "a2"),
    "int64_t":     ("LogicalType::BIGINT",  "int64_t", "a2"),
    "double":      ("LogicalType::DOUBLE",  "double",  "a2"),
    "bool":        ("LogicalType::BOOLEAN", "bool",    "a2"),
    "TimestampTz": ("LogicalType::TIMESTAMP_TZ", "timestamp_tz_t", "DuckDBToMeosTimestamp(a2).value"),
    "DateADT":     ("LogicalType::DATE", "date_t", "ToMeosDate(a2)"),  # single-expr, no lifecycle
}
def poc_binary(f):
    """Binary Temporal + by-value-scalar shape (BinaryExecutor). Same correctness
    rules as poc_emittable: scalar return OR generic same-type temporal return."""
    if supported(f) is not None: return None
    ins, out = classify(f)
    if out is not None or len(ins) != 2: return None
    if base(ins[0]["canonical"]) != "Temporal" or not norm(ins[0]["canonical"]).endswith("*"): return None
    b2 = base(ins[1]["canonical"]); n2 = norm(ins[1]["canonical"])
    is_text2 = (b2 == "text" and n2.endswith("*"))   # owned text* arg via MakeText
    if not is_text2 and (b2 not in SCALAR_ARG or "*" in n2): return None
    sc = reg_scope(f["name"])
    if sc is None: return None
    arg2 = ("LogicalType::VARCHAR", "string_t", "__TEXT__") if is_text2 else SCALAR_ARG[b2]
    rb = base(f["returnType"]["canonical"]); rn = norm(f["returnType"]["canonical"])
    if rb == "Temporal" and rn.endswith("*"):
        return ("temporal", "MD_TEMPORAL", arg2)
    if rb in BYVAL_RET and "*" not in rn:
        return ("scalar:" + rb, scalar_ret_duck(f), arg2)
    if rb in ("Interval", "text", "char") and rn.endswith("*"):   # owned-pointer scalar return
        return ("scalar:" + rb, scalar_ret_duck(f), arg2)
    return None

def emit_body_binary(f, kind, arg2):
    name = f["name"]; dt2, cpp2, marsh = arg2
    is_text = (marsh == "__TEXT__")
    pre = "text *a2t = MakeText(a2);\n            " if is_text else ""
    call2 = "a2t" if is_text else marsh
    post = "free(a2t); " if is_text else ""
    if kind == "temporal":          # pointer return -> NULL-safe (MEOS NULL -> SQL NULL)
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::ExecuteWithNulls<string_t, {cpp2}, string_t>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t in, {cpp2} a2, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            Temporal *t = BlobToTemporal(in);\n"
                f"            {pre}Temporal *r = {name}(t, {call2});\n            {post}free(t);\n"
                f"            return TemporalToBlobN(result, r, mask, idx);\n"
                f"        }});\n}}\n")
    ctype, rett, _rx = scalar_emit3(f)
    inner = f"{pre}{ctype} r = {name}(t, {call2});\n            {post}free(t);\n            return {_rx};"
    return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    BinaryExecutor::Execute<string_t, {cpp2}, {rett}>(args.data[0], args.data[1], result, args.size(),\n"
            f"        [&](string_t in, {cpp2} a2) {{\n"
            f"            Temporal *t = BlobToTemporal(in);\n"
            f"            {inner}\n"
            f"        }});\n}}\n")

def header_symbols(incl_dir):
    """Set of MEOS function names DECLARED in the target headers. The required
    pin/ABI gate ([[meos-idl-for-binding-codegen]]): the catalog is version-fluid,
    so only emit functions actually present at the BUILD pin (else link/compile
    fails on catalog-ahead-of-pin functions like 14l tfloat_sin on an older pin)."""
    import os
    # Scan ONLY the MEOS C headers the generated TU actually #includes (its include
    # closure). Globbing every *.h admits symbols from family headers we do NOT
    # include — they pass the gate but are undeclared at compile time. Keep this list
    # in lockstep with the preamble's C #includes: meos_wrapper_simple.hpp now pulls in
    # every per-family public header (cross-family conversions like tbigint_to_th3index,
    # tgeometry_to_tcbuffer, ttext_to_tjsonb live there, not in the meos.h umbrella —
    # the port builds all families so all are installed), so the gate must see them too.
    TU_C_HEADERS = ("meos.h", "meos_catalog.h", "meos_internal.h",
                    "meos_geo.h", "meos_internal_geo.h",
                    "meos_cbuffer.h", "meos_h3.h", "meos_json.h", "meos_npoint.h",
                    "meos_pointcloud.h", "meos_pose.h", "meos_quadbin.h", "meos_rgeo.h",
                    "pg_date.h", "pg_timestamp.h")
    syms = set()
    for name in TU_C_HEADERS:
        h = os.path.join(incl_dir, name)
        if not os.path.exists(h):
            continue
        try:
            for line in open(h, errors="ignore"):
                m = re.search(r'\b([a-z][a-z0-9_]+)\s*\(', line)
                if m and ("extern" in line or line.lstrip().startswith(("int ", "bool ", "double ", "Temporal ", "void "))):
                    syms.add(m.group(1))
        except OSError:
            pass
    return syms

def poc_ternary(f):
    """Ternary Temporal + 2 by-value scalars (TernaryExecutor). Same correctness rules."""
    if supported(f) is not None: return None
    ins, out = classify(f)
    if out is not None or len(ins) != 3: return None
    if base(ins[0]["canonical"]) != "Temporal" or not norm(ins[0]["canonical"]).endswith("*"): return None
    b2 = base(ins[1]["canonical"]); b3 = base(ins[2]["canonical"])
    if b2 not in SCALAR_ARG or "*" in norm(ins[1]["canonical"]): return None
    if b3 not in SCALAR_ARG or "*" in norm(ins[2]["canonical"]): return None
    if reg_scope(f["name"]) is None: return None
    rb = base(f["returnType"]["canonical"]); rn = norm(f["returnType"]["canonical"])
    if rb == "Temporal" and rn.endswith("*"):
        return ("temporal", "MD_TEMPORAL", SCALAR_ARG[b2], SCALAR_ARG[b3])
    if rb in BYVAL_RET and "*" not in rn:
        return ("scalar:" + rb, scalar_ret_duck(f), SCALAR_ARG[b2], SCALAR_ARG[b3])
    return None

def emit_body_ternary(f, kind, arg2, arg3):
    name = f["name"]; dt2, cpp2, _ = arg2; dt3, cpp3, _ = arg3
    e2 = arg2[2]; e3 = arg3[2].replace("a2", "a3")   # SCALAR_ARG marshal is templated on "a2"
    if kind == "temporal":          # ternary -> Temporal (NULL-safe pointer return)
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    TernaryExecutor::ExecuteWithNulls<string_t, {cpp2}, {cpp3}, string_t>("
                f"args.data[0], args.data[1], args.data[2], result, args.size(),\n"
                f"        [&](string_t in, {cpp2} a2, {cpp3} a3, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            Temporal *t = BlobToTemporal(in);\n"
                f"            Temporal *r = {name}(t, {e2}, {e3});\n            free(t);\n"
                f"            return TemporalToBlobN(result, r, mask, idx);\n        }});\n}}\n")
    ctype, rett, _rx = scalar_emit3(f)
    inner = f"{ctype} r = {name}(t, {e2}, {e3});\n            free(t);\n            return {_rx};"
    return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    TernaryExecutor::Execute<string_t, {cpp2}, {cpp3}, {rett}>("
            f"args.data[0], args.data[1], args.data[2], result, args.size(),\n"
            f"        [&](string_t in, {cpp2} a2, {cpp3} a3) {{\n"
            f"            Temporal *t = BlobToTemporal(in);\n"
            f"            {inner}\n"
            f"        }});\n}}\n")

def poc_binary_tt(f):
    """Binary Temporal + Temporal (both via BlobToTemporal) — the big 2-arg shape
    (comparisons, boolean ops, distance, etc.). Both args same temporal type."""
    if supported(f) is not None: return None
    ins, out = classify(f)
    if out is not None or len(ins) != 2: return None
    if not all(base(p["canonical"]) == "Temporal" and norm(p["canonical"]).endswith("*") for p in ins):
        return None
    if reg_scope(f["name"]) is None: return None
    rb = base(f["returnType"]["canonical"]); rn = norm(f["returnType"]["canonical"])
    if rb == "Temporal" and rn.endswith("*"):
        return ("temporal", "MD_TEMPORAL")
    if rb in BYVAL_RET and "*" not in rn:
        return ("scalar:" + rb, scalar_ret_duck(f))
    return None

def emit_binary_tt(f, kind):
    name = f["name"]
    if kind == "temporal":          # (Temporal,Temporal) -> Temporal (NULL-safe pointer return)
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t in1, string_t in2, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            Temporal *t1 = BlobToTemporal(in1);\n            Temporal *t2 = BlobToTemporal(in2);\n"
                f"            Temporal *r = {name}(t1, t2);\n            free(t1); free(t2);\n"
                f"            return TemporalToBlobN(result, r, mask, idx);\n        }});\n}}\n")
    ctype, rett, _rx = scalar_emit3(f)
    inner = f"{ctype} r = {name}(t1, t2);\n            free(t1); free(t2);\n            return {_rx};"
    return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    BinaryExecutor::Execute<string_t, string_t, {rett}>(args.data[0], args.data[1], result, args.size(),\n"
            f"        [&](string_t in1, string_t in2) {{\n"
            f"            Temporal *t1 = BlobToTemporal(in1);\n"
            f"            Temporal *t2 = BlobToTemporal(in2);\n"
            f"            {inner}\n"
            f"        }});\n}}\n")

def poc_binary_tt_scalar(f):
    """Two Temporal blobs + one trailing by-value scalar (TernaryExecutor over two
    Temporal args and a scalar). E.g. temporal_lcss_distance(Temporal,Temporal,double)
    — the similarity distance with a threshold. Same correctness rules as binary_tt."""
    if supported(f) is not None: return None
    ins, out = classify(f)
    if out is not None or len(ins) != 3: return None
    if not all(base(ins[i]["canonical"]) == "Temporal" and norm(ins[i]["canonical"]).endswith("*")
               for i in (0, 1)):
        return None
    b3 = base(ins[2]["canonical"])
    if b3 not in SCALAR_ARG or "*" in norm(ins[2]["canonical"]): return None
    if reg_scope(f["name"]) is None: return None
    rb = base(f["returnType"]["canonical"]); rn = norm(f["returnType"]["canonical"])
    if rb == "Temporal" and rn.endswith("*"):
        return ("temporal", "MD_TEMPORAL", SCALAR_ARG[b3])
    if rb in BYVAL_RET and "*" not in rn:
        return ("scalar:" + rb, scalar_ret_duck(f), SCALAR_ARG[b3])
    return None

def emit_binary_tt_scalar(f, kind, arg3):
    name = f["name"]; dt3, cpp3, e3 = arg3     # SCALAR_ARG marshal is templated on "a2"
    if kind == "temporal":          # (Temporal,Temporal,scalar) -> Temporal (NULL-safe)
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    TernaryExecutor::ExecuteWithNulls<string_t, string_t, {cpp3}, string_t>("
                f"args.data[0], args.data[1], args.data[2], result, args.size(),\n"
                f"        [&](string_t in1, string_t in2, {cpp3} a2, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            Temporal *t1 = BlobToTemporal(in1);\n            Temporal *t2 = BlobToTemporal(in2);\n"
                f"            Temporal *r = {name}(t1, t2, {e3});\n            free(t1); free(t2);\n"
                f"            return TemporalToBlobN(result, r, mask, idx);\n        }});\n}}\n")
    ctype, rett, _rx = scalar_emit3(f)
    inner = f"{ctype} r = {name}(t1, t2, {e3});\n            free(t1); free(t2);\n            return {_rx};"
    return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    TernaryExecutor::Execute<string_t, string_t, {cpp3}, {rett}>("
            f"args.data[0], args.data[1], args.data[2], result, args.size(),\n"
            f"        [&](string_t in1, string_t in2, {cpp3} a2) {{\n"
            f"            Temporal *t1 = BlobToTemporal(in1);\n"
            f"            Temporal *t2 = BlobToTemporal(in2);\n"
            f"            {inner}\n"
            f"        }});\n}}\n")

def poc_geo_temporal(f):
    """Geometry-argument spatial relationships: one Temporal blob + one GSERIALIZED
    (geometry) blob, optional trailing double. The geometry is marshalled
    GeometryToGSerialized(blob, tspatial_srid(t)) — its SRID comes from the sibling
    temporal arg (the hand geo executor pattern, tgeompoint_functions.cpp). Covers the
    geo spatial relationships eContains/eIntersects/eDwithin/... (group *_rel_ever: MEOS
    `int` tri-state, ret<0 -> SQL NULL, else BOOLEAN) and tContains/tIntersects/tDwithin/...
    (group *_rel_temp: Temporal tbool). supported() rejects the GSERIALIZED arg, so the
    non-arg eligibility checks are applied directly here."""
    name = f["name"]
    if name.startswith("meos_internal") or (f.get("group") or "").startswith("meos_internal"):
        return None
    if not f.get("sqlfn") or re.search(r'_(out|in|send|recv)$', f.get("sqlfn") or ""):
        return None
    if unregistered_family_ref(name) is not None:
        return None
    ins, out = classify(f)
    if out is not None or len(ins) not in (2, 3):
        return None
    bs = [base(p["canonical"]) for p in ins]
    # first two args = exactly one Temporal* and one GSERIALIZED* (either order), both ptrs
    if sorted(bs[:2]) != ["GSERIALIZED", "Temporal"]:
        return None
    if not all(norm(ins[i]["canonical"]).endswith("*") for i in (0, 1)):
        return None
    geo_first = (bs[0] == "GSERIALIZED")
    has_dbl = len(ins) == 3
    if has_dbl and (base(ins[2]["canonical"]) != "double" or "*" in norm(ins[2]["canonical"])):
        return None
    if reg_scope(name) is None:
        return None
    rb = base(f["returnType"]["canonical"]); rn = norm(f["returnType"]["canonical"])
    if rb == "Temporal" and rn.endswith("*"):
        return ("temporal", "MD_TEMPORAL", geo_first, has_dbl)
    if rb in ("int", "int32_t") and "*" not in rn:      # ea tri-state -> nullable BOOLEAN
        return ("scalar", scalar_ret_duck(f), geo_first, has_dbl)
    return None

def emit_geo_temporal(f, kind, geo_first, has_dbl):
    name = f["name"]
    decl = "string_t in_g, string_t in_t" if geo_first else "string_t in_t, string_t in_g"
    call_args = "gs, t" if geo_first else "t, gs"
    marshal = ("            Temporal *t = BlobToTemporal(in_t);\n"
               "            GSERIALIZED *gs = GeometryToGSerialized(in_g, tspatial_srid(t));\n")
    freeing = "            free(t); free(gs);\n"
    if has_dbl:               # (Temporal,geometry,double) or (geometry,Temporal,double)
        exec_head = ("TernaryExecutor::ExecuteWithNulls<string_t, string_t, double, {ret}>("
                     "args.data[0], args.data[1], args.data[2], result, args.size(),")
        lam_decl = decl + ", double d, ValidityMask &mask, idx_t idx"
        call = f"{name}({call_args}, d)"
    else:                     # (Temporal,geometry) or (geometry,Temporal)
        exec_head = ("BinaryExecutor::ExecuteWithNulls<string_t, string_t, {ret}>("
                     "args.data[0], args.data[1], result, args.size(),")
        lam_decl = decl + ", ValidityMask &mask, idx_t idx"
        call = f"{name}({call_args})"
    if kind == "temporal":    # -> Temporal tbool (NULL-safe pointer return)
        head = exec_head.format(ret="string_t"); rett = "string_t"
        body = (f"            Temporal *r = {call};\n{freeing}"
                f"            return TemporalToBlobN(result, r, mask, idx);\n")
    else:                     # ea int -> nullable BOOLEAN (hand semantics: ret<0 -> SQL NULL)
        head = exec_head.format(ret="bool"); rett = "bool"
        body = (f"            int r = {call};\n{freeing}"
                f"            if (r < 0) {{ mask.SetInvalid(idx); return false; }}\n"
                f"            return r != 0;\n")
    return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    {head}\n"
            f"        [&]({lam_decl}) -> {rett} {{\n{marshal}{body}        }});\n}}\n")

# ---- array-return struct shape: C array-of-structs + int* count -> DuckDB LIST(STRUCT) ----
# Driven ENTIRELY by the catalog: shape.arrayReturn (from MEOS-API shapeinfer) marks the
# array return, lengthFrom.name identifies the count out-param, and the returned struct's
# fields come from the catalog `structs` table. The similarity *Path fns (frechet/dtw) are
# the instance: (Temporal, Temporal) -> Match* + int* count -> LIST(STRUCT(i,j)). The body
# mirrors the hand RunSimilarityPath; the type mirrors temporal.cpp's path_type. STRUCTS is
# populated from the catalog in main().
STRUCTS = {}
PATH_LT  = {"int": "LogicalType::INTEGER", "int64": "LogicalType::BIGINT",
            "double": "LogicalType::DOUBLE", "bool": "LogicalType::BOOLEAN"}
PATH_CPP = {"int": "int32_t", "int64": "int64_t", "double": "double", "bool": "bool"}

def path_logical_type(flds):
    members = ", ".join('{"%s", %s}' % (fl["name"], PATH_LT.get(fl["cType"], "LogicalType::INTEGER"))
                        for fl in flds)
    return "LogicalType::LIST(LogicalType::STRUCT({%s}))" % members

def poc_path(f):
    """Binary Temporal+Temporal whose catalog shape.arrayReturn returns a C array of a known
    struct (+ int* count out-param) -> a DuckDB LIST(STRUCT(...)). The frechet/dtw *Path fns.
    NOTE: this shape's struct-pointer return is exactly what supported() rejects ('ret:Match *'),
    so we apply only the user-facing eligibility checks here, not the standard ret/arg gate."""
    name = f["name"]
    if name.startswith("meos_internal") or (f.get("group") or "").startswith("meos_internal"):
        return None
    if not f.get("sqlfn") or re.search(r'_(out|in|send|recv)$', f.get("sqlfn") or ""):
        return None
    ar = (f.get("shape") or {}).get("arrayReturn")
    if not ar: return None
    rb = base(f["returnType"]["canonical"])
    flds = (STRUCTS.get(rb) or {}).get("fields")
    if not flds: return None                       # only struct-element arrays handled here
    lf = ar.get("lengthFrom") or {}
    if lf.get("kind") != "param" or not lf.get("name"): return None
    ins = [p for p in f["params"] if p.get("name") != lf["name"]]   # drop the count out-param
    if len(ins) != 2: return None
    if not all(base(p["canonical"]) == "Temporal" and norm(p["canonical"]).endswith("*") for p in ins):
        return None
    if reg_scope(f["name"]) is None: return None
    return ("path", path_logical_type(flds))

def emit_path_table(f):
    """DuckDB TABLE function for a catalog array-return-struct fn (the canonical SETOF surface,
    e.g. frechetDistancePath -> RETURNS SETOF warp). Mirrors the hand SimilarityPath bind/init/exec;
    return schema + per-row marshalling are field-driven from the catalog struct (warp/Match)."""
    name = f["name"]; sqlfn = f["sqlfn"]
    rb = base(f["returnType"]["canonical"]); flds = STRUCTS[rb]["fields"]
    state_cols = "".join(f"    std::vector<{PATH_CPP.get(fl['cType'],'int32_t')}> col{i};\n"
                         for i, fl in enumerate(flds))
    ret_types  = ", ".join(PATH_LT.get(fl["cType"], "LogicalType::INTEGER") for fl in flds)
    names_list = ", ".join('"%s"' % fl["name"] for fl in flds)
    reserve = "".join(f"            state->col{i}.reserve(count);\n" for i in range(len(flds)))
    fill    = "".join(f"                state->col{i}.push_back(path[k].{fl['name']});\n"
                      for i, fl in enumerate(flds))
    out_decls = "".join(f"    auto out{i} = FlatVector::GetData<{PATH_CPP.get(fl['cType'],'int32_t')}>(output.data[{i}]);\n"
                        for i, fl in enumerate(flds))
    out_fill  = "".join(f"        out{i}[k] = state.col{i}[state.idx + k];\n" for i in range(len(flds)))
    return (
f"struct PathBind_{name} : public TableFunctionData {{ string_t a, b; }};\n"
f"struct PathState_{name} : public GlobalTableFunctionState {{\n"
f"    idx_t idx = 0;\n{state_cols}}};\n"
f"static unique_ptr<FunctionData> PathBindFn_{name}(ClientContext &, TableFunctionBindInput &input,\n"
f"        vector<LogicalType> &return_types, vector<string> &names) {{\n"
f"    if (input.inputs.size() != 2 || input.inputs[0].IsNull() || input.inputs[1].IsNull())\n"
f"        throw BinderException(\"{sqlfn}: expects two non-null temporal arguments\");\n"
f"    auto bind = make_uniq<PathBind_{name}>();\n"
f"    bind->a = StringValue::Get(input.inputs[0]);\n"
f"    bind->b = StringValue::Get(input.inputs[1]);\n"
f"    return_types = {{{ret_types}}};\n"
f"    names = {{{names_list}}};\n"
f"    return std::move(bind);\n}}\n"
f"static unique_ptr<GlobalTableFunctionState> PathInit_{name}(ClientContext &, TableFunctionInitInput &input) {{\n"
f"    EnsureMeosThreadInitialized();\n"
f"    auto &bind = input.bind_data->Cast<PathBind_{name}>();\n"
f"    auto state = make_uniq<PathState_{name}>();\n"
f"    Temporal *t1 = BlobToTemporal(bind.a);\n"
f"    Temporal *t2 = BlobToTemporal(bind.b);\n"
f"    int count = 0;\n"
f"    {rb} *path = {name}(t1, t2, &count);\n"
f"    free(t1); free(t2);\n"
f"    if (path) {{\n{reserve}"
f"        for (int k = 0; k < count; k++) {{\n{fill}        }}\n"
f"        free(path);\n    }}\n"
f"    return std::move(state);\n}}\n"
f"static void PathExec_{name}(ClientContext &, TableFunctionInput &input, DataChunk &output) {{\n"
f"    auto &state = input.global_state->Cast<PathState_{name}>();\n"
f"    idx_t total = state.col0.size();\n"
f"    idx_t count = MinValue<idx_t>(STANDARD_VECTOR_SIZE, total - state.idx);\n"
f"{out_decls}    for (idx_t k = 0; k < count; k++) {{\n{out_fill}    }}\n"
f"    state.idx += count;\n    output.SetCardinality(count);\n}}\n")

# Temporal + box (STBox/TBox) -> bool: the spatiotemporal/numeric topological predicates
# (contains/overlaps/contained/adjacent/same/left/right/... between a temporal and a box).
# Mixed-arg shape (one Temporal blob + one box blob); temporal scope from reg_scope
# (tspatial->geo for STBox, tnumber->{tint,tfloat} for TBox).
BOX_MARSH = {"STBox": ("BlobToStbox", "StboxType::stbox()"),
             "TBox":  ("BlobToTbox",  "TboxType::tbox()")}
def poc_temporal_box(f):
    if supported(f) is not None: return None
    ins, out = classify(f)
    if out is not None or len(ins) != 2: return None
    rb = base(f["returnType"]["canonical"]); rn = norm(f["returnType"]["canonical"])
    if rb != "bool" or "*" in rn: return None
    b0 = base(ins[0]["canonical"]); n0 = norm(ins[0]["canonical"])
    b1 = base(ins[1]["canonical"]); n1 = norm(ins[1]["canonical"])
    if not (n0.endswith("*") and n1.endswith("*")): return None
    sc = reg_scope(f["name"])
    if sc is None or sc[0] != "types": return None
    if b0 == "Temporal" and b1 in BOX_MARSH: return ("tb_r:" + b1, sc[1])  # (Temporal, Box)
    if b1 == "Temporal" and b0 in BOX_MARSH: return ("tb_l:" + b0, sc[1])  # (Box, Temporal)
    return None

def emit_temporal_box(f, kind):
    name = f["name"]; side, box = kind.split(":"); blobto = BOX_MARSH[box][0]
    if side == "tb_r":   # (Temporal, Box)
        body = (f"            Temporal *t = BlobToTemporal(a);\n            {box} *bx = {blobto}(b);\n"
                f"            bool r = {name}(t, bx);\n            free(t); free(bx);\n            return r;")
    else:                # (Box, Temporal)
        body = (f"            {box} *bx = {blobto}(a);\n            Temporal *t = BlobToTemporal(b);\n"
                f"            bool r = {name}(bx, t);\n            free(bx); free(t);\n            return r;")
    return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    BinaryExecutor::Execute<string_t, string_t, bool>(args.data[0], args.data[1], result, args.size(),\n"
            f"        [&](string_t a, string_t b) {{\n{body}\n        }});\n}}\n")

# Temporal + span -> bool: topological predicates across the value (numspan) or time
# (tstzspan) dimension. numspan PAIRS to the tnumber's value type; tstzspan is fixed and
# applies to every temporal type. ALL_TEMPORAL_ACCS = the full temporal type set (core+geo).
NUMSPAN_PAIR = {"TemporalTypes::tint()": "SpanTypes::intspan()",
                "TemporalTypes::tfloat()": "SpanTypes::floatspan()"}
ALL_TEMPORAL_ACCS = (["TemporalTypes::tint()", "TemporalTypes::tbigint()", "TemporalTypes::tbool()",
                      "TemporalTypes::tfloat()", "TemporalTypes::ttext()"] + GEO_ALLTYPES)
def poc_temporal_span(f):
    if supported(f) is not None: return None
    ins, out = classify(f)
    if out is not None or len(ins) != 2: return None
    rb = base(f["returnType"]["canonical"]); rn = norm(f["returnType"]["canonical"])
    if rb == "bool" and "*" not in rn:        retk = "bool"
    elif rb == "Temporal" and rn.endswith("*"): retk = "T"   # at/minus span -> same temporal type
    else: return None
    b0 = base(ins[0]["canonical"]); n0 = norm(ins[0]["canonical"])
    b1 = base(ins[1]["canonical"]); n1 = norm(ins[1]["canonical"])
    if not (n0.endswith("*") and n1.endswith("*")): return None
    CONT = ("Span", "Set", "SpanSet")
    if b0 == "Temporal" and b1 in CONT:   side, cont = "ts_r", b1
    elif b1 == "Temporal" and b0 in CONT: side, cont = "ts_l", b0
    else: return None
    nm = f["name"]
    flav = "tstz" if "tstz" in nm else ("num" if re.search(r'(numspan|intspan|floatspan)', nm) else None)
    if flav is None: return None
    if flav == "num" and cont != "Span": return None   # value-pairing only built for value spans
    return (side + ":" + cont + ":" + flav + ":" + retk, None)

CONT_BLOB = {"Span": "BlobToSpan", "Set": "BlobToSet", "SpanSet": "BlobToSpanSet"}
def emit_temporal_span(f, kind):
    name = f["name"]; side, cont, _flav, retk = kind.split(":"); blob = CONT_BLOB[cont]
    if retk == "T": ret_c, ret_s, rett = "Temporal *r = ", "return TemporalToBlob(result, r);", "string_t"
    else:           ret_c, ret_s, rett = "bool r = ", "return r;", "bool"
    if side == "ts_r":   # (Temporal, Container)
        body = (f"            Temporal *t = BlobToTemporal(a);\n            {cont} *cc = {blob}(b);\n"
                f"            {ret_c}{name}(t, cc);\n            free(t); free(cc);\n            {ret_s}")
    else:                # (Container, Temporal)
        body = (f"            {cont} *cc = {blob}(a);\n            Temporal *t = BlobToTemporal(b);\n"
                f"            {ret_c}{name}(cc, t);\n            free(cc); free(t);\n            {ret_s}")
    return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    BinaryExecutor::Execute<string_t, string_t, {rett}>(args.data[0], args.data[1], result, args.size(),\n"
            f"        [&](string_t a, string_t b) {{\n{body}\n        }});\n}}\n")

def poc_scalar_first(f):
    """(by-value scalar, Temporal) — the mirror of poc_binary. Covers the scalar-first
    overloads the hand registers (ever_eq_int_tint, teq_int_tint, …)."""
    if supported(f) is not None: return None
    ins, out = classify(f)
    if out is not None or len(ins) != 2: return None
    b1 = base(ins[0]["canonical"]); n1 = norm(ins[0]["canonical"])
    is_text1 = (b1 == "text" and n1.endswith("*"))   # owned text* arg via MakeText
    if not is_text1 and (b1 not in SCALAR_ARG or "*" in n1): return None
    if base(ins[1]["canonical"]) != "Temporal" or not norm(ins[1]["canonical"]).endswith("*"): return None
    if reg_scope(f["name"]) is None: return None
    arg1 = ("LogicalType::VARCHAR", "string_t", "__TEXT__") if is_text1 else SCALAR_ARG[b1]
    rb = base(f["returnType"]["canonical"]); rn = norm(f["returnType"]["canonical"])
    if rb == "Temporal" and rn.endswith("*"):       return ("temporal", "MD_TEMPORAL", arg1)
    if rb in BYVAL_RET and "*" not in rn:           return ("scalar:" + rb, scalar_ret_duck(f), arg1)
    return None

def emit_scalar_first(f, kind, arg1):
    name = f["name"]; _dt, cpp1, marsh = arg1; marsh = marsh.replace("a2", "a1")
    is_text = (marsh == "__TEXT__")
    pre = "text *a1t = MakeText(a1);\n            " if is_text else ""
    call1 = "a1t" if is_text else marsh
    post = "free(a1t); " if is_text else ""
    if kind == "temporal":          # (scalar,Temporal) -> Temporal (NULL-safe pointer return)
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::ExecuteWithNulls<{cpp1}, string_t, string_t>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&]({cpp1} a1, string_t in, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            Temporal *t = BlobToTemporal(in);\n"
                f"            {pre}Temporal *r = {name}({call1}, t);\n            {post}free(t);\n"
                f"            return TemporalToBlobN(result, r, mask, idx);\n        }});\n}}\n")
    ctype, rett, _rx = scalar_emit3(f)
    inner = f"{pre}{ctype} r = {name}({call1}, t);\n            {post}free(t);\n            return {_rx};"
    return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    BinaryExecutor::Execute<{cpp1}, string_t, {rett}>(args.data[0], args.data[1], result, args.size(),\n"
            f"        [&]({cpp1} a1, string_t in) {{\n"
            f"            Temporal *t = BlobToTemporal(in);\n"
            f"            {inner}\n"
            f"        }});\n}}\n")

# ---------------- SPAN family (additive; Set/temporal paths untouched) ----------------
# Span is a FIXED-size struct (sizeof(Span), verified span_functions.cpp). This increment
# = the generic (Span,Span)->bool|Span surface over SpanTypes::AllTypes() (span topology /
# comparison / union-intersection-minus). Element predicates (Span,scalar) = next increment.
# Element scalar -> the Span type it implies (accessors verified lowercase, span.hpp;
# canonical names from meos_catalog.c: intspan/bigintspan/floatspan/datespan/tstzspan).
ELEM_TO_SPAN = {
    "int": "SpanTypes::intspan()", "int32_t": "SpanTypes::intspan()",
    "int64_t": "SpanTypes::bigintspan()", "double": "SpanTypes::floatspan()",
    "DateADT": "SpanTypes::datespan()", "TimestampTz": "SpanTypes::tstzspan()",
}
SPAN_TYPES = {
    "intspan": "SpanTypes::intspan()", "bigintspan": "SpanTypes::bigintspan()",
    "floatspan": "SpanTypes::floatspan()", "datespan": "SpanTypes::datespan()",
    "tstzspan": "SpanTypes::tstzspan()",
}
def span_reg_scope(name):
    if name.startswith("span_"): return ("all", None)
    for pre, acc in SPAN_TYPES.items():
        if name.startswith(pre + "_") or name == pre: return ("types", [acc])
    return None
def ret_span_type(name, arg_acc):
    m = re.search(r'_to_(intspan|bigintspan|floatspan|datespan|tstzspan)$', name)
    return SPAN_TYPES[m.group(1)] if m else arg_acc

# SpanSet mirrors Span (varlena -> spanset_mem_size; SpansetTypes accessors verified
# lowercase spanset.hpp + meos_catalog.c). Same shapes -> handled by the SAME collection
# machinery (poc_coll/emit_coll) via a descriptor, so SpanSet is added without duplication.
SPANSET_TYPES = {
    "intspanset": "SpansetTypes::intspanset()", "bigintspanset": "SpansetTypes::bigintspanset()",
    "floatspanset": "SpansetTypes::floatspanset()", "textspanset": "SpansetTypes::textspanset()",
    "datespanset": "SpansetTypes::datespanset()", "tstzspanset": "SpansetTypes::tstzspanset()",
}
ELEM_TO_SPANSET = {
    "int": "SpansetTypes::intspanset()", "int32_t": "SpansetTypes::intspanset()",
    "int64_t": "SpansetTypes::bigintspanset()", "double": "SpansetTypes::floatspanset()",
    "DateADT": "SpansetTypes::datespanset()", "TimestampTz": "SpansetTypes::tstzspanset()",
}
def spanset_reg_scope(name):
    if name.startswith("spanset_"): return ("all", None)
    for pre, acc in SPANSET_TYPES.items():
        if name.startswith(pre + "_") or name == pre: return ("types", [acc])
    return None
def ret_spanset_type(name, arg_acc):
    m = re.search(r'_to_(intspanset|bigintspanset|floatspanset|textspanset|datespanset|tstzspanset)$', name)
    return SPANSET_TYPES[m.group(1)] if m else arg_acc

# Collection-family descriptors (Span + SpanSet share ALL shape logic).
SPAN_C    = dict(cbase="Span",    blobto="BlobToSpan",    toblob="SpanToBlob",
                 elem=ELEM_TO_SPAN,    scope=span_reg_scope,    ret=ret_span_type,
                 alltypes="SpanTypes::AllTypes()")
SPANSET_C = dict(cbase="SpanSet", blobto="BlobToSpanSet", toblob="SpanSetToBlob",
                 elem=ELEM_TO_SPANSET, scope=spanset_reg_scope, ret=ret_spanset_type,
                 alltypes="SpansetTypes::AllTypes()")
# Box families: SINGLE type each (no AllTypes loop, no element predicates). Fixed-size
# struct -> sizeof(STBox)/sizeof(TBox) (verified single_tile_getters.cpp). Reuse the same
# (X,X)->bool|X shape machinery via the descriptor; the loop registers the concrete accessor.
STBOX_C = dict(cbase="STBox", blobto="BlobToStbox", toblob="StboxToBlob",
               elem={}, scope=None, ret=lambda n, a: a, single="StboxType::stbox()")
TBOX_C  = dict(cbase="TBox",  blobto="BlobToTbox",  toblob="TboxToBlob",
               elem={}, scope=None, ret=lambda n, a: a, single="TboxType::tbox()")
def poc_span(f, C=SPAN_C):
    if supported(f) is not None: return None
    if re.search(r'_(transfn|finalfn|combinefn)$', f["name"]): return None
    ins, out = classify(f)
    if out is not None: return None
    cb = C["cbase"]
    rb = base(f["returnType"]["canonical"]); rn = norm(f["returnType"]["canonical"])
    contp = lambda p: base(p["canonical"]) == cb and norm(p["canonical"]).endswith("*")
    sel = lambda p: base(p["canonical"]) if (base(p["canonical"]) in C["elem"] and "*" not in norm(p["canonical"])) else None
    # unary (X)->X | scalar  (lower/upper/width, ceil/floor; name-scoped; boxes have no scope)
    if len(ins) == 1 and contp(ins[0]):
        if C["scope"] is None or C["scope"](f["name"]) is None: return None
        if rb == cb and rn.endswith("*"):           return ("u_span", "LogicalType::BLOB")
        if rb in BYVAL_RET and "*" not in rn:       return ("u_scalar:" + rb, byval_ret_duck(rb))
        if rb == "Interval" and rn.endswith("*"):   return ("u_scalar:Interval", "LogicalType::INTERVAL")
        return None
    if len(ins) != 2: return None
    # element predicates: (X, scalar)->bool / (scalar, X)->bool (contains/left/...)
    if rb == "bool" and "*" not in rn:
        if contp(ins[0]) and sel(ins[1]): return ("setsc:" + sel(ins[1]), "LogicalType::BOOLEAN")
        if contp(ins[1]) and sel(ins[0]): return ("scset:" + sel(ins[0]), "LogicalType::BOOLEAN")
    # (X, by-value scalar) -> owned Interval (e.g. duration(spanset, bool)); scope-gated.
    if (contp(ins[0]) and base(ins[1]["canonical"]) in SCALAR_ARG
            and "*" not in norm(ins[1]["canonical"]) and rb == "Interval" and rn.endswith("*")
            and C["scope"] is not None and C["scope"](f["name"]) is not None):
        return ("u2iv:" + base(ins[1]["canonical"]), "LogicalType::INTERVAL")
    # generic (X,X) -> bool|X
    if not (contp(ins[0]) and contp(ins[1])): return None
    if rb == cb and rn.endswith("*"):  return ("b_span", "type")
    if rb == "bool" and "*" not in rn:     return ("b_bool", "LogicalType::BOOLEAN")
    return None

def emit_span(f, kind, C=SPAN_C):
    name = f["name"]; cb, bt, tb = C["cbase"], C["blobto"], C["toblob"]
    if kind.startswith("setsc:"):   # (X, scalar) -> bool
        _dt, cpp2, marsh = SCALAR_ARG[kind.split(':')[1]]
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::Execute<string_t, {cpp2}, bool>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t a, {cpp2} a2) {{\n"
                f"            {cb} *s = {bt}(a);\n            bool r = {name}(s, {marsh});\n            free(s);\n"
                f"            return r;\n        }});\n}}\n")
    if kind.startswith("scset:"):   # (scalar, X) -> bool
        _dt, cpp1, marsh = SCALAR_ARG[kind.split(':')[1]]; marsh = marsh.replace("a2", "a1")
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::Execute<{cpp1}, string_t, bool>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&]({cpp1} a1, string_t b) {{\n"
                f"            {cb} *s = {bt}(b);\n            bool r = {name}({marsh}, s);\n            free(s);\n"
                f"            return r;\n        }});\n}}\n")
    if kind == "u_span":            # (X) -> X (pointer return; MEOS NULL = empty -> SQL NULL)
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(args.data[0], result, args.size(),\n"
                f"        [&](string_t in, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            {cb} *s = {bt}(in);\n            {cb} *r = {name}(s);\n            free(s);\n"
                f"            if (!r) {{ mask.SetInvalid(idx); return string_t(); }}\n"
                f"            return {tb}(result, r);\n        }});\n}}\n")
    if kind.startswith("u_scalar:"):  # (X) -> scalar (by-value, time-converted, or owned Interval*)
        cct, rett, rexpr = byval_ret3(kind.split(':')[1])
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    UnaryExecutor::Execute<string_t, {rett}>(args.data[0], result, args.size(),\n"
                f"        [&](string_t in) {{\n"
                f"            {cb} *s = {bt}(in);\n            {cct} r = {name}(s);\n            free(s);\n"
                f"            return {rexpr};\n        }});\n}}\n")
    if kind.startswith("u2iv:"):    # (X, by-value scalar) -> owned Interval (duration(spanset,bool))
        _dt, cpp2, marsh = SCALAR_ARG[kind.split(':')[1]]
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::Execute<string_t, {cpp2}, interval_t>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t in, {cpp2} a2) {{\n"
                f"            {cb} *s = {bt}(in);\n            MeosInterval *r = {name}(s, {marsh});\n            free(s);\n"
                f"            return TakeInterval(r);\n        }});\n}}\n")
    if kind == "b_span":            # (X,X) -> X (pointer return; MEOS NULL = empty -> SQL NULL)
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            {cb} *s1 = {bt}(a);\n            {cb} *s2 = {bt}(b);\n"
                f"            {cb} *r = {name}(s1, s2);\n            free(s1); free(s2);\n"
                f"            if (!r) {{ mask.SetInvalid(idx); return string_t(); }}\n"
                f"            return {tb}(result, r);\n        }});\n}}\n")
    return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    BinaryExecutor::Execute<string_t, string_t, bool>(args.data[0], args.data[1], result, args.size(),\n"
            f"        [&](string_t a, string_t b) {{\n"
            f"            {cb} *s1 = {bt}(a);\n            {cb} *s2 = {bt}(b);\n"
            f"            bool r = {name}(s1, s2);\n            free(s1); free(s2);\n            return r;\n"
            f"        }});\n}}\n")

# ---- canonical names only — NO coexistence prefix, ever ----
# The North Star binding has ZERO hand-written UDFs, so there is nothing to coexist
# with: the generator OWNS every canonical name it emits, and the hand registration it
# would duplicate is DELETED in the same change (generate + delete-hand, suite-proved).
# There is no g_ transition prefix — a generated function IS the binding's function.
# RETIRED_GROUPS lists @ingroup families whose hand registrations are fully deleted, so
# the retire-safety check below verifies the generator covers every @sqlfn of each
# (dropping none). It gates SAFETY (per-family, suite-verified), not naming; naming is
# always the canonical @sqlfn.
RETIRED_GROUPS = {"meos_temporal_analytics_similarity", "meos_temporal_comp_temp",
                  "meos_geo_rel_ever", "meos_geo_rel_temp",
                  "meos_geo_bbox_topo"}
# @sqlfn names in a RETIRED group that the generator legitimately does NOT emit and that the
# hand keeps on purpose (a documented generator-shape gap, NOT a silent drop). Anything else
# uncovered in a retired group is a build-FATAL retire-safety error (see the validation below).
# The LIST-returning *Pairs relations (ever/always int*->LIST(bool); temporal SpanSet***)
# take array returns the generator does not marshal yet AND were never in the hand layer
# either (git grep: 0 hand regs) - so retiring meos_geo_rel_ever / meos_geo_rel_temp drops
# none of them; they are additive future work, not a regression. (*Path LIST(STRUCT) returns
# ARE generated now via poc_path, so they are not listed here.)
RETIRE_UNCOVERED_OK = {
    "aDisjointPairs", "aDwithinPairs", "aIntersectsPairs", "aTouchesPairs",
    "eDisjointPairs", "eDwithinPairs", "eIntersectsPairs", "eTouchesPairs",
    "tDisjointPairs", "tDwithinPairs", "tIntersectsPairs", "tTouchesPairs",
}
def retired(f):
    return (f.get("group") or "") in RETIRED_GROUPS
def reg_name(nm, f):
    return nm            # canonical always; the g_ coexistence prefix is removed for good
def reg_names(f, sqlfn, aliases):
    """The SQL names a function registers under: its sqlfn, the portable bare
    alias for its operator (the cross-engine RFC dialect), and the operator symbol
    itself so DuckDB exposes the operator like MobilityDB. The doxygen `@`-escape on
    the operator (`\\@>`) is normalized to the bare symbol (`@>`)."""
    op = (f.get("sqlop") or "").replace("\\", "")
    # A backing-only @sqlfn (the shared bbox-topological tag same_bbox/contains_bbox/…,
    # classified in the catalog by MEOS-API) is NOT a deployed SQL name — MobilityDB exposes
    # only the operator's bare portable alias + the operator. Register the public bare name,
    # never the `_bbox` backing tag. (catalog SoT: sqlfnBackingOnly / publicSqlName.)
    names = [] if f.get("sqlfnBackingOnly") else [sqlfn]
    bare = aliases.get(op) if aliases else None
    if bare and bare not in names:
        names.append(bare)
    # The operator symbol is registered only when DuckDB can parse it: a name
    # containing '#' is rejected by the lexer in any operator position, so those
    # operators are reachable only through their bare name (before/after/tEq/...).
    if op and "#" not in op and op not in names:
        names.append(op)
    return names

# Output organization mirrors the canonical generator family (JMEOS FunctionsGenerator /
# Spark codegen_spark_udfs.py): every emitted body + registration is bucketed by its
# doxygen @ingroup GROUP, so a function lands in the same place across tools and the
# surface is browseable by the MEOS reference-manual structure. GReg captures the group
# of every appended line WITHOUT touching the 20 per-shape append call-sites — STATE["grp"]
# is set once per emitted function at the top of each emission loop.
STATE = {"grp": "meos_ungrouped"}
class GReg:
    """A list whose .append() also records the current @ingroup group (STATE['grp'])."""
    def __init__(self): self.items = []          # list of (group, line)
    def append(self, line): self.items.append((STATE["grp"], line))
    def by_group(self):
        d = defaultdict(list)
        for g, line in self.items: d[g].append(line)
        return d
    def __len__(self): return len(self.items)

def gen_cpp(fns, out_path, declared=None, aliases=None):
    bodies, generic_regs, specific_regs = GReg(), GReg(), GReg()
    set_bodies, set_generic_regs, set_specific_regs = GReg(), GReg(), GReg()
    span_bodies, span_generic_regs, span_specific_regs = GReg(), GReg(), GReg()
    spanset_generic_regs = GReg(); box_regs = GReg()
    temporal_box_bodies, temporal_box_regs = GReg(), GReg()
    n_un = n_bin = n_ter = n_set = n_span = 0
    for f in fns:
        if declared is not None and f["name"] not in declared:
            continue            # pin/ABI gate: skip catalog fns absent from the build headers
        u = poc_emittable(f); b = None if u else poc_binary(f); t = None if (u or b) else poc_ternary(f)
        tt = None if (u or b or t) else poc_binary_tt(f)
        tts = None if (u or b or t or tt) else poc_binary_tt_scalar(f)
        sf = None if (u or b or t or tt or tts) else poc_scalar_first(f)
        gt = None if (u or b or t or tt or tts or sf) else poc_geo_temporal(f)
        pp = None if (u or b or t or tt or tts or sf or gt) else poc_path(f)
        if not u and not b and not t and not tt and not tts and not sf and not gt and not pp:
            continue
        STATE["grp"] = f.get("group") or "meos_ungrouped"
        sqlfn, fn = f["sqlfn"], f["name"]
        scope, accs = reg_scope(fn)
        if u:
            kind, dret = u; n_un += 1
            bodies.append(emit_body(f, kind))
            argsig = "{type}" if scope == "all" else None
            spec_sig = "{%s}"
        elif b:
            kind, dret, arg2 = b; n_bin += 1
            bodies.append(emit_body_binary(f, kind, arg2))
            argsig = "{type, %s}" % arg2[0]
            spec_sig = "{%%s, %s}" % arg2[0]
        elif t:
            kind, dret, arg2, arg3 = t; n_ter += 1
            bodies.append(emit_body_ternary(f, kind, arg2, arg3))
            argsig = "{type, %s, %s}" % (arg2[0], arg3[0])
            spec_sig = "{%%s, %s, %s}" % (arg2[0], arg3[0])
        elif tt:
            kind, dret = tt; n_bin += 1
            bodies.append(emit_binary_tt(f, kind))
            argsig = "{type, type}"
            spec_sig = "{%s, %s}"
        elif tts:
            kind, dret, arg3 = tts; n_ter += 1
            bodies.append(emit_binary_tt_scalar(f, kind, arg3))
            argsig = "{type, type, %s}" % arg3[0]
            spec_sig = "{%%s, %%s, %s}" % arg3[0]
        elif sf:
            kind, dret, arg1 = sf; n_bin += 1
            bodies.append(emit_scalar_first(f, kind, arg1))
            argsig = "{%s, type}" % arg1[0]
            spec_sig = "{%s, %%s}" % arg1[0]
        elif gt:
            kind, dret, geo_first, has_dbl = gt
            if has_dbl: n_ter += 1
            else: n_bin += 1
            bodies.append(emit_geo_temporal(f, kind, geo_first, has_dbl))
            slots = ["GeoTypes::GEOMETRY()", "%s"] if geo_first else ["%s", "GeoTypes::GEOMETRY()"]
            if has_dbl: slots.append("LogicalType::DOUBLE")
            spec_sig = "{" + ", ".join(slots) + "}"       # geo rels are always scope=='types'
            argsig = spec_sig.replace("%s", "type")
        else:
            # array-return-struct shape -> the canonical SETOF surface = a DuckDB TABLE function
            # (registered via loader.RegisterFunction, NOT the scalar path). Over AllTypes + geo
            # via the {type, type} placeholder the writer wraps in its AllTypes/geo loops.
            n_bin += 1
            bodies.append(emit_path_table(f))
            for nm in reg_names(f, sqlfn, aliases):
                generic_regs.append(
                    f'        loader.RegisterFunction(TableFunction("{reg_name(nm, f)}", {{type, type}}, '
                    f'PathExec_{fn}, PathBindFn_{fn}, PathInit_{fn}));')
            continue
        # Names to register: the native sqlfn + any portable bare-name alias the pin
        # assigns to this fn's operator (catalog portableAliases is the SoT — invent
        # nothing). The alias reuses the SAME backing body ([[aliases-reuse-backing]]).
        names = reg_names(f, sqlfn, aliases)
        if scope == "all":
            rett = ret_temporal_type(fn, "type", f.get("group"), f.get("sqlReturnType")) if dret == "MD_TEMPORAL" else dret
            for nm in names:
                generic_regs.append(f'        RegisterSerializedScalarFunction(loader, ScalarFunction('
                                    f'"{reg_name(nm, f)}", {argsig}, {rett}, Gen_{fn}));')
        else:
            for a in accs:
                sig = spec_sig % ((a,) * spec_sig.count("%s"))   # 1 or 2 accessor slots
                r2 = ret_temporal_type(fn, a, f.get("group"), f.get("sqlReturnType")) if dret == "MD_TEMPORAL" else dret
                for nm in names:
                    specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                         f'"{reg_name(nm, f)}", {sig}, {r2}, Gen_{fn}));')
    # SET family — separate loop (the temporal path above is untouched).
    for f in fns:
        if declared is not None and f["name"] not in declared:
            continue
        s = poc_set(f)
        if s is None:
            continue
        STATE["grp"] = f.get("group") or "meos_ungrouped"
        kind, dret = s; n_set += 1
        set_bodies.append(emit_set(f, kind))
        fn, sqlfn = f["name"], f["sqlfn"]
        # portable bare-name alias; normalize the doxygen `@`-escape (sqlop "\@>" -> "@>").
        names = reg_names(f, sqlfn, aliases)
        # element-typed predicates: accessor from the scalar element type, BOOLEAN ret.
        if kind.startswith("setsc_set:"):   # (Set, scalar element) -> Set (same set type)
            b = kind.split(':')[1]; acc = ELEM_TO_SET[b]
            scd = "LogicalType::VARCHAR" if b == "text" else SCALAR_ARG[b][0]
            for nm in names:
                set_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                         f'"{reg_name(nm, f)}", {{{acc}, {scd}}}, {acc}, Gen_{fn}));')
            continue
        if kind.startswith("setsc:") or kind.startswith("scset:"):
            b = kind.split(':')[1]; acc = ELEM_TO_SET[b]
            scd = "LogicalType::VARCHAR" if b == "text" else SCALAR_ARG[b][0]
            sig = "{%s, %s}" % (acc, scd) if kind.startswith("setsc:") else "{%s, %s}" % (scd, acc)
            for nm in names:
                set_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                         f'"{reg_name(nm, f)}", {sig}, LogicalType::BOOLEAN, Gen_{fn}));')
            continue
        sc = set_reg_scope(fn)
        scope, accs = sc if sc else ("all", None)   # b_set/b_bool (<op>_set_set) -> over AllTypes
        nargs = len(classify(f)[0])
        argsig = "{type}" if nargs == 1 else "{type, type}"
        spec_sig = "{%s}" if nargs == 1 else "{%s, %s}"
        if scope == "all":
            rett = ret_set_type(fn, "type") if dret == "LogicalType::BLOB" else dret
            for nm in names:
                set_generic_regs.append(f'        RegisterSerializedScalarFunction(loader, ScalarFunction('
                                        f'"{reg_name(nm, f)}", {argsig}, {rett}, Gen_{fn}));')
        else:
            for a in accs:
                sig = spec_sig % ((a,) * spec_sig.count("%s"))
                r2 = ret_set_type(fn, a) if dret == "LogicalType::BLOB" else dret
                for nm in names:
                    set_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                             f'"{reg_name(nm, f)}", {sig}, {r2}, Gen_{fn}));')
    # COLLECTION families (Span + SpanSet) — ONE machinery via descriptor (poc_span/emit_span
    # take C). Per-container generic list (its own AllTypes loop); specific list shared.
    for C in (SPAN_C, SPANSET_C, STBOX_C, TBOX_C):
        gen = {"Span": span_generic_regs, "SpanSet": spanset_generic_regs}.get(C["cbase"])
        for f in fns:
            if declared is not None and f["name"] not in declared:
                continue
            s = poc_span(f, C)
            if s is None:
                continue
            STATE["grp"] = f.get("group") or "meos_ungrouped"
            kind, dret = s; n_span += 1
            span_bodies.append(emit_span(f, kind, C))
            fn, sqlfn = f["name"], f["sqlfn"]
            names = reg_names(f, sqlfn, aliases)
            # element predicates: accessor from the scalar element type, BOOLEAN ret (type-specific).
            if kind.startswith("setsc:") or kind.startswith("scset:"):
                b = kind.split(':')[1]; acc = C["elem"][b]; scd = SCALAR_ARG[b][0]
                sig = "{%s, %s}" % (acc, scd) if kind.startswith("setsc:") else "{%s, %s}" % (scd, acc)
                for nm in names:
                    span_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                              f'"{reg_name(nm, f)}", {sig}, LogicalType::BOOLEAN, Gen_{fn}));')
                continue
            # unary (X)->X|scalar: name-scoped (X_*→AllTypes, <elem>X_*→accessor).
            if kind == "u_span" or kind.startswith("u_scalar:"):
                scope, accs = C["scope"](fn)
                if scope == "all":
                    rett = C["ret"](fn, "type") if kind == "u_span" else dret
                    for nm in names:
                        gen.append(f'        RegisterSerializedScalarFunction(loader, ScalarFunction('
                                   f'"{reg_name(nm, f)}", {{type}}, {rett}, Gen_{fn}));')
                else:
                    for a in accs:
                        r2 = C["ret"](fn, a) if kind == "u_span" else dret
                        for nm in names:
                            span_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                                      f'"{reg_name(nm, f)}", {{{a}}}, {r2}, Gen_{fn}));')
                continue
            # (X, by-value scalar)->Interval: name-scoped, 2-arg sig {acc, argtype}.
            if kind.startswith("u2iv:"):
                argdt = SCALAR_ARG[kind.split(':')[1]][0]
                scope, accs = C["scope"](fn)
                if scope == "all":
                    for nm in names:
                        gen.append(f'        RegisterSerializedScalarFunction(loader, ScalarFunction('
                                   f'"{reg_name(nm, f)}", {{type, {argdt}}}, {dret}, Gen_{fn}));')
                else:
                    for a in accs:
                        for nm in names:
                            span_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                                      f'"{reg_name(nm, f)}", {{{a}, {argdt}}}, {dret}, Gen_{fn}));')
                continue
            # generic (X,X)->bool|X.
            if C.get("single"):   # box: single type, concrete accessor (no AllTypes loop)
                acc = C["single"]; r = acc if kind == "b_span" else dret
                for nm in names:
                    box_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                    f'"{reg_name(nm, f)}", {{{acc}, {acc}}}, {r}, Gen_{fn}));')
            else:
                rett = "type" if kind == "b_span" else dret   # (X,X)->X preserves the type
                for nm in names:
                    gen.append(f'        RegisterSerializedScalarFunction(loader, ScalarFunction('
                               f'"{reg_name(nm, f)}", {{type, type}}, {rett}, Gen_{fn}));')
    # Temporal + box (STBox/TBox) -> bool: spatiotemporal/numeric topological predicates,
    # registered over the temporal accessors from reg_scope × the single box accessor.
    for f in fns:
        if declared is not None and f["name"] not in declared:
            continue
        s = poc_temporal_box(f)
        if s is None:
            continue
        STATE["grp"] = f.get("group") or "meos_ungrouped"
        kind, accs = s; n_bin += 1
        temporal_box_bodies.append(emit_temporal_box(f, kind))
        fn, sqlfn = f["name"], f["sqlfn"]
        names = reg_names(f, sqlfn, aliases)
        box_acc = BOX_MARSH[kind.split(":")[1]][1]
        for a in accs:
            sig = "{%s, %s}" % (a, box_acc) if kind.startswith("tb_r:") else "{%s, %s}" % (box_acc, a)
            for nm in names:
                temporal_box_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                         f'"{reg_name(nm, f)}", {sig}, LogicalType::BOOLEAN, Gen_{fn}));')
    # Temporal x span -> bool: numspan PAIRS to the tnumber value type (tint↔intspan,
    # tfloat↔floatspan); tstzspan is fixed and registered over every temporal type.
    for f in fns:
        if declared is not None and f["name"] not in declared:
            continue
        s = poc_temporal_span(f)
        if s is None:
            continue
        STATE["grp"] = f.get("group") or "meos_ungrouped"
        kind, _ = s; n_bin += 1
        temporal_box_bodies.append(emit_temporal_span(f, kind))
        fn, sqlfn = f["name"], f["sqlfn"]; side, cont, flav, retk = kind.split(":")
        span_first = (side == "ts_l")
        names = reg_names(f, sqlfn, aliases)
        TSTZ_CONT = {"Span": "SpanTypes::tstzspan()", "Set": "SetTypes::tstzset()",
                     "SpanSet": "SpansetTypes::tstzspanset()"}
        if flav == "num":                       # tnumber value span, paired (Span only)
            pairs = [(NUMSPAN_PAIR[a], a) for a in reg_scope(fn)[1] if a in NUMSPAN_PAIR]
        else:                                   # tstz time container, fixed, over all temporal types
            pairs = [(TSTZ_CONT[cont], a) for a in ALL_TEMPORAL_ACCS]
        for spacc, tacc in pairs:
            sig = "{%s, %s}" % (tacc, spacc) if not span_first else "{%s, %s}" % (spacc, tacc)
            rett = tacc if retk == "T" else "LogicalType::BOOLEAN"   # at/minus span preserves type
            for nm in names:
                temporal_box_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                         f'"{reg_name(nm, f)}", {sig}, {rett}, Gen_{fn}));')
    # Include order mirrors the hand .cpp files (meos_wrapper + common FIRST, before
    # anything pulls duckdb::Interval into scope; all outside `namespace duckdb`).
    src = ("// GENERATED by tools/codegen_duck_udfs.py from the MEOS-API catalog — DO NOT EDIT.\n"
           '#include "meos_wrapper_simple.hpp"\n'
           '#include "common.hpp"\n'
           '#include "temporal/temporal.hpp"\n'
           '#include "temporal/set.hpp"\n'
           '#include "temporal/span.hpp"\n'
           '#include "temporal/spanset.hpp"\n'
           '#include "temporal/tbox.hpp"\n'
           '#include "geo/stbox.hpp"\n'
           '#include "geo/tgeompoint.hpp"\n'
           '#include "geo/tgeogpoint.hpp"\n'
           '#include "geo/tgeometry.hpp"\n'
           '#include "geo/tgeography.hpp"\n'
           '#include "spatial/spatial_types.hpp"\n'   # GeoTypes::GEOMETRY() (duckdb-spatial)
           '#include "geo_util.hpp"\n'                # GeometryToGSerialized(blob, srid)
           '#include "meos_internal.h"\n'
           '#include "meos_geo.h"\n'
           '#include "meos_internal_geo.h"\n'
           '#include "time_util.hpp"\n'
           '#include "mobilityduck/meos_exec_serial.hpp"\n'
           '#include "duckdb/function/scalar_function.hpp"\n'
           '#include "duckdb/main/extension/extension_loader.hpp"\n'
           '#include "duckdb/common/vector_operations/unary_executor.hpp"\n'
           '#include "duckdb/common/vector_operations/binary_executor.hpp"\n'
           '#include "duckdb/common/vector_operations/ternary_executor.hpp"\n'
           '#include <cstring>\n#include <cstdlib>\n\n'
           "namespace duckdb {\nnamespace {\n"
           "// Self-contained blob<->Temporal marshalling (generated owns it; no hand-header dep).\n"
           "inline string_t TemporalToBlob(Vector &result, Temporal *t) {\n"
           "    size_t sz = temporal_mem_size(t);\n"
           "    string_t out = StringVector::AddStringOrBlob(result, (const char *)t, sz);\n"
           "    free(t);\n    return out;\n}\n"
           "// Null-aware variants: a MEOS function returning NULL (empty/undefined result,\n"
           "// e.g. an intersection or restriction with no match) maps to SQL NULL via the mask.\n"
           "inline string_t TemporalToBlobN(Vector &result, Temporal *t, ValidityMask &mask, idx_t idx) {\n"
           "    if (!t) { mask.SetInvalid(idx); return string_t(); }\n    return TemporalToBlob(result, t);\n}\n"
           "inline Temporal *BlobToTemporal(string_t blob) {\n"
           "    size_t sz = blob.GetSize();\n"
           "    uint8_t *copy = (uint8_t *)malloc(sz);\n"
           "    memcpy(copy, blob.GetData(), sz);\n"
           "    return reinterpret_cast<Temporal *>(copy);\n}\n"
           "// Self-contained blob<->Set marshalling (hand binding's exact method: set_mem_size out).\n"
           "inline string_t SetToBlob(Vector &result, Set *s) {\n"
           "    string_t out = StringVector::AddStringOrBlob(result, (const char *)s, set_mem_size(s));\n"
           "    free(s);\n    return out;\n}\n"
           "inline string_t SetToBlobN(Vector &result, Set *s, ValidityMask &mask, idx_t idx) {\n"
           "    if (!s) { mask.SetInvalid(idx); return string_t(); }\n    return SetToBlob(result, s);\n}\n"
           "inline Set *BlobToSet(string_t blob) {\n"
           "    size_t sz = blob.GetSize();\n"
           "    uint8_t *copy = (uint8_t *)malloc(sz);\n"
           "    memcpy(copy, blob.GetData(), sz);\n"
           "    return reinterpret_cast<Set *>(copy);\n}\n"
           "// Self-contained blob<->Span marshalling (Span is a FIXED-size struct -> sizeof(Span)).\n"
           "inline string_t SpanToBlob(Vector &result, Span *s) {\n"
           "    string_t out = StringVector::AddStringOrBlob(result, (const char *)s, sizeof(Span));\n"
           "    free(s);\n    return out;\n}\n"
           "inline Span *BlobToSpan(string_t blob) {\n"
           "    size_t sz = blob.GetSize();\n"
           "    uint8_t *copy = (uint8_t *)malloc(sz);\n"
           "    memcpy(copy, blob.GetData(), sz);\n"
           "    return reinterpret_cast<Span *>(copy);\n}\n"
           "// Self-contained blob<->SpanSet marshalling (varlena -> spanset_mem_size out).\n"
           "inline string_t SpanSetToBlob(Vector &result, SpanSet *ss) {\n"
           "    string_t out = StringVector::AddStringOrBlob(result, (const char *)ss, spanset_mem_size(ss));\n"
           "    free(ss);\n    return out;\n}\n"
           "inline SpanSet *BlobToSpanSet(string_t blob) {\n"
           "    size_t sz = blob.GetSize();\n"
           "    uint8_t *copy = (uint8_t *)malloc(sz);\n"
           "    memcpy(copy, blob.GetData(), sz);\n"
           "    return reinterpret_cast<SpanSet *>(copy);\n}\n"
           "// Self-contained blob<->STBox/TBox marshalling (FIXED-size structs -> sizeof).\n"
           "inline string_t StboxToBlob(Vector &result, STBox *b) {\n"
           "    string_t out = StringVector::AddStringOrBlob(result, (const char *)b, sizeof(STBox));\n"
           "    free(b);\n    return out;\n}\n"
           "inline STBox *BlobToStbox(string_t blob) {\n"
           "    uint8_t *copy = (uint8_t *)malloc(blob.GetSize());\n    memcpy(copy, blob.GetData(), blob.GetSize());\n"
           "    return reinterpret_cast<STBox *>(copy);\n}\n"
           "inline string_t TboxToBlob(Vector &result, TBox *b) {\n"
           "    string_t out = StringVector::AddStringOrBlob(result, (const char *)b, sizeof(TBox));\n"
           "    free(b);\n    return out;\n}\n"
           "inline TBox *BlobToTbox(string_t blob) {\n"
           "    uint8_t *copy = (uint8_t *)malloc(blob.GetSize());\n    memcpy(copy, blob.GetData(), blob.GetSize());\n"
           "    return reinterpret_cast<TBox *>(copy);\n}\n"
           "// Build a MEOS text* varlena from a DuckDB VARCHAR (hand binding's exact method); caller frees.\n"
           "inline text *MakeText(string_t s) {\n"
           "    size_t len = s.GetSize();\n"
           "    text *t = (text *)malloc(VARHDRSZ + len);\n"
           "    SET_VARSIZE(t, VARHDRSZ + len);\n"
           "    memcpy(VARDATA(t), s.GetData(), len);\n    return t;\n}\n"
           "// Convert an owned MEOS Interval* to a DuckDB interval_t and free it.\n"
           "inline interval_t TakeInterval(MeosInterval *iv) {\n"
           "    interval_t out = IntervalToIntervalt(iv);\n    free(iv);\n    return out;\n}\n"
           "// MEOS TimestampTz (PG epoch micros) -> DuckDB timestamp_tz_t.\n"
           "inline timestamp_tz_t TakeTimestamp(TimestampTz r) {\n"
           "    timestamp_tz_t ts; ts.value = (int64_t) r; return MeosToDuckDBTimestamp(ts);\n}\n"
           "// Owned MEOS text* varlena -> DuckDB VARCHAR string_t; frees the text*.\n"
           "inline string_t TakeText(Vector &result, text *t) {\n"
           "    string_t out = StringVector::AddString(result, (const char *) VARDATA(t), VARSIZE(t) - VARHDRSZ);\n"
           "    free(t);\n    return out;\n}\n"
           "// Owned MEOS C-string (NUL-terminated, malloc'd) -> DuckDB VARCHAR; frees it.\n"
           "inline string_t TakeCString(Vector &result, char *s) {\n"
           "    string_t out = StringVector::AddString(result, s);\n    free(s);\n    return out;\n}\n\n"
           )

    # ---- bodies, sectioned by @ingroup group (one section per group) ----
    body_by_grp = defaultdict(list)
    for acc in (bodies, set_bodies, span_bodies, temporal_box_bodies):
        for g, b in acc.items:
            body_by_grp[g].append(b)
    body_section = ""
    for g in sorted(body_by_grp):
        body_section += "\n// ===== @ingroup %s =====\n" % g + "\n".join(body_by_grp[g]) + "\n"

    # ---- one RegisterGenerated_<group>() per @ingroup group + a registerAll aggregator ----
    # Mirrors the canonical generators' one-unit-per-group structure (Spark GeneratedUdfs_<group>
    # + registerAll). Each generic slot keeps its own type loop; specific/box/tbox at fn level.
    tgen, tspec = generic_regs.by_group(), specific_regs.by_group()
    sgen, sspec = set_generic_regs.by_group(), set_specific_regs.by_group()
    spgen, spspec = span_generic_regs.by_group(), span_specific_regs.by_group()
    ssgen, boxg = spanset_generic_regs.by_group(), box_regs.by_group()
    tboxg = temporal_box_regs.by_group()
    reg_groups = sorted(set(tgen) | set(tspec) | set(sgen) | set(sspec) | set(spgen)
                        | set(spspec) | set(ssgen) | set(boxg) | set(tboxg))
    def _loop(header, lines):
        return ("    " + header + "\n" + "\n".join("        " + l.strip() for l in lines)
                + "\n    }\n")
    def _flat(lines):
        return "".join("    " + l.strip() + "\n" for l in lines)
    fn_section, aggregator = "", []
    for g in reg_groups:
        fname = "RegisterGenerated_" + re.sub(r"[^A-Za-z0-9_]", "_", g)
        b = "static void %s(ExtensionLoader &loader) {\n" % fname
        if tgen.get(g):
            b += _loop("for (auto &type : TemporalTypes::AllTypes()) {", tgen[g])
            b += _loop("for (auto &type : std::vector<LogicalType>{" + ", ".join(GEO_ALLTYPES) + "}) {", tgen[g])
        b += _flat(tspec.get(g, []))
        if sgen.get(g): b += _loop("for (auto &type : SetTypes::AllTypes()) {", sgen[g])
        b += _flat(sspec.get(g, []))
        if spgen.get(g): b += _loop("for (auto &type : SpanTypes::AllTypes()) {", spgen[g])
        b += _flat(spspec.get(g, []))
        if ssgen.get(g): b += _loop("for (auto &type : SpansetTypes::AllTypes()) {", ssgen[g])
        b += _flat(boxg.get(g, []))
        b += _flat(tboxg.get(g, []))
        b += "}\n"
        fn_section += "\n" + b
        aggregator.append("    %s(loader);" % fname)
    reg_section = (fn_section
                   + "\nvoid RegisterGeneratedTemporalUdfs(ExtensionLoader &loader) {\n"
                   + "\n".join(aggregator) + "\n}\n")

    src += body_section + "} // anonymous\n" + reg_section + "\n} // namespace duckdb\n"
    open(out_path, "w").write(src)
    n_reg = (len(generic_regs) + len(specific_regs) + len(set_generic_regs)
             + len(set_specific_regs) + len(span_generic_regs) + len(span_specific_regs)
             + len(spanset_generic_regs) + len(box_regs) + len(temporal_box_regs))
    return n_un, n_bin, n_ter, n_reg, n_set, n_span

def main():
    global RETIRED_GROUPS
    pos = [a for a in sys.argv[1:] if not a.startswith("--")]
    for fl in [a for a in sys.argv[1:] if a.startswith("--")]:
        if fl.startswith("--retire="):
            RETIRED_GROUPS = {x for x in fl[len("--retire="):].split(",") if x}
        # (the --prefix / g_ coexistence flag is removed for good — names are always canonical)
    # Default to the in-repo vendored catalog at a STABLE path (mirrors JMEOS's
    # codegen/input/meos-idl.json, overwritten in place on each base refresh — no
    # per-SHA filename, no PIN marker). Provenance is the vcpkg portfile REF + the
    # regen commit message, never a versioned catalog copy.
    _repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    cat = pos[0] if len(pos) > 0 else os.path.join(_repo_root, "tools/catalog/meos-idl.json")
    out = pos[1] if len(pos) > 1 else None
    d = json.load(open(cat))
    fns = d["functions"]
    # struct layouts (e.g. Match {i,j}) for the array-return LIST(STRUCT) shape — from the catalog.
    STRUCTS.update({s["name"]: s for s in d.get("structs", [])})
    # portable bare-name renderings: operator (@sqlop) -> bareName, straight from the
    # catalog's portableAliases (itself generated from the MEOS doxygen @sqlop tags +
    # the comparison dialect). The SoT; nothing invented here.
    aliases = {}
    for fam in (d.get("portableAliases", {}).get("families", {}) or {}).values():
        for e in fam:
            aliases[e["operator"]] = e["bareName"]
    emittable, reasons, by_fam = [], Counter(), defaultdict(list)
    for f in fns:
        why = supported(f)
        if why is None:
            emittable.append(f); by_fam[family(f)].append(f)
        else:
            reasons[why.split(":")[0]] += 1
    sqlfns = [f for f in fns if f.get("sqlfn")]
    print(f"catalog: {len(fns)} fns, {len(sqlfns)} with sqlfn")
    print(f"EMITTABLE (POC scalar shapes): {len(emittable)}")
    print("top exclusion reasons:", dict(reasons.most_common(8)))
    print(f"families: {len(by_fam)} ->", dict(sorted(((k, len(v)) for k, v in by_fam.items()), key=lambda x:-x[1])[:10]))
    poc = [f for f in fns if poc_emittable(f)]
    print(f"\nCOMPILABLE-POC subset (unary generic-Temporal*, full bodies): {len(poc)}")
    if out:
        hdr = pos[2] if len(pos) > 2 else None
        declared = header_symbols(hdr) if hdr else None
        if declared is not None:
            print(f"pin/ABI gate: {len(declared)} fns declared in {hdr}")
        nu, nb, nt, nr, ns, nsp = gen_cpp(fns, out, declared, aliases)
        print(f"wrote {nu} unary + {nb} binary + {nt} ternary + {ns} set + {nsp} span UDF bodies, {nr} registrations -> {out}")
        # Regularity invariant (build-failing): MEOS keeps locale/collation, session
        # timezone, PROJ context and RNGs in thread-local storage, so every UDF body
        # must run EnsureMeosThreadInitialized() before any MEOS call. Verify it for
        # every emitted body so a future emit path cannot silently drop the guard.
        body_section = open(out).read().split("} // anonymous")[0]
        unguarded = [b.split("(")[0] for b in body_section.split("\nstatic void Gen_")[1:]
                     if "EnsureMeosThreadInitialized" not in b]
        if unguarded:
            print(f"FATAL: {len(unguarded)} generated UDF body(ies) reach MEOS without the "
                  f"per-thread MEOS-init guard:", file=sys.stderr)
            for u in unguarded[:20]:
                print("   Gen_" + u, file=sys.stderr)
            sys.exit(1)
        print(f"per-thread MEOS-init guard: all {nu + nb + nt + ns + nsp} generated bodies verified")
        # Retire-safety guards (build-failing) — the two retire traps, read off the actual
        # generated output so they cannot be reasoned-away by hand.
        gentext = open(out).read()
        emitted = Counter(re.findall(r'(?:Scalar|Table)Function\("([^"]+)"', gentext))
        # TRAP 1 (split name): a name emitted BOTH bare and g_-prefixed means a RETIRED group and
        # a NON-retired group share it (bare here, g_ there) -> a query finds only one. The shared
        # bare/operator dialect must be retired as a coherent wave (add the sibling groups).
        split = sorted(n for n in emitted if not n.startswith("g_") and ("g_" + n) in emitted)
        if split:
            print(f"FATAL: retire trap 1 (split name) — {len(split)} name(s) emitted BOTH bare and "
                  f"g_-prefixed: a retired group shares them with a non-retired group. Retire the "
                  f"whole shared-name wave together (add the sibling @ingroup groups to "
                  f"RETIRED_GROUPS):", file=sys.stderr)
            for n in split[:20]:
                print(f"   {n}  (also g_{n})", file=sys.stderr)
            sys.exit(1)
        # TRAP 2 (uncovered): every @sqlfn of a RETIRED group must be emitted (bare) — else
        # retiring it DROPS that function. A genuine, documented generator-shape gap kept by the
        # hand goes in RETIRE_UNCOVERED_OK (with a reason); anything else is a real coverage gap.
        retired_sqlfns = defaultdict(set)
        for f in fns:
            # A backing-only @sqlfn (sqlfnBackingOnly, e.g. the bbox-topological same_bbox/
            # contains_bbox tags) is NEVER a deployed SQL name — the generator emits its bare
            # publicSqlName (same/~=/contains/@>/...) instead. So it cannot be dropped by a retire;
            # exclude it from the coverage check (covered-by-construction, generalizes to any group).
            if (f.get("group") in RETIRED_GROUPS) and f.get("sqlfn") and not f.get("sqlfnBackingOnly"):
                retired_sqlfns[f["group"]].add(f["sqlfn"])
        uncovered = [(g, nm) for g, nms in retired_sqlfns.items() for nm in sorted(nms)
                     if emitted.get(nm, 0) == 0 and nm not in RETIRE_UNCOVERED_OK]
        if uncovered:
            print(f"FATAL: retire trap 2 (uncovered) — {len(uncovered)} @sqlfn function(s) of a "
                  f"RETIRED group are NOT generated, so retiring drops them. Generate them (close "
                  f"the shape gap), or add to RETIRE_UNCOVERED_OK with a reason if genuinely kept "
                  f"by hand pending a catalog/shape fix:", file=sys.stderr)
            for g, nm in uncovered[:20]:
                print(f"   {g}: {nm}", file=sys.stderr)
            sys.exit(1)
        print(f"retire-safety: {len(RETIRED_GROUPS)} retired group(s) verified "
              f"(no split names, every @sqlfn generated or documented)")
    # sample emitted registrations (the shape; full emit = next increment)
    print("\n=== sample generated registrations (first 6) ===")
    for f in emittable[:6]:
        ins, out = classify(f)
        argts = ", ".join(arg_type(p["canonical"])[0] for p in ins)
        rt, kind = ret_type(f, out)
        print(f'  // {f["sqlfn"]}  <- {f["name"]}  ({kind})')
        print(f'  RegisterSerializedScalarFunction(loader, ScalarFunction("{f["sqlfn"]}", '
              f'{{{argts}}}, {rt}, Gen_{f["name"]}));')
    return emittable, by_fam

if __name__ == "__main__":
    main()
