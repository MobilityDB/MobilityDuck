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
    "TPCBox":       ("TpcboxType::tpcbox()", "BlobToTpcbox(%s)"),
    "TBox":         ("LogicalType::BLOB", "BlobToTbox(%s)"),
    "Cbuffer":      ("CbufferTypes::cbuffer()", "BlobToCbuffer(%s)"),
    "Jsonb":        ("TJsonbTypes::jsonb()", "BlobToJsonb(%s)"),
    "Pcpoint":      ("TPcpointTypes::pcpoint()", "BlobToPcpoint(%s)"),
    "Pcpatch":      ("TPcpatchTypes::pcpatch()", "BlobToPcpatch(%s)"),
    "Npoint":       ("NpointTypes::npoint()", "BlobToNpoint(%s)"),
    "Pose":         ("PoseTypes::pose()", "BlobToPose(%s)"),
    "PoseChain":    ("PosechainTypes::posechain()", "BlobToPosechain(%s)"),
    "Nsegment":     ("NpointTypes::nsegment()", "BlobToNsegment(%s)"),
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
    "Cbuffer":      ("CbufferTypes::cbuffer()", "CbufferToBlob(result, %s)"),
    "Jsonb":        ("TJsonbTypes::jsonb()", "JsonbToBlob(result, %s)"),
    "Pcpoint":      ("TPcpointTypes::pcpoint()", "PcpointToBlob(result, %s)"),
    "Pcpatch":      ("TPcpatchTypes::pcpatch()", "PcpatchToBlob(result, %s)"),
    "Npoint":       ("NpointTypes::npoint()", "NpointToBlob(result, %s)"),
    "Pose":         ("PoseTypes::pose()", "PoseToBlob(result, %s)"),
    "PoseChain":    ("PosechainTypes::posechain()", "PosechainToBlob(result, %s)"),
    "Nsegment":     ("NpointTypes::nsegment()", "NsegmentToBlob(result, %s)"),
}
# The temporal-family pointer returns that marshal as one DuckDB temporal handle. A MEOS
# accessor/cast can return a concrete subtype pointer (TInstant */TSequence */TSequenceSet *
# — e.g. startInstant, tintSeqSet) but the wire representation and the SQL result type are the
# base temporal type of the input (the subtype is erased in SQL), so they emit exactly like a
# `Temporal *` return; the C++ subtype pointer is upcast to `Temporal *` before marshalling.
TEMPORAL_PTR_RET = {k for k, v in PTR_RET.items() if v[0] == "MD_TEMPORAL"}
# Base-value pointer returns: a family's BASE value (Cbuffer today; Npoint/Pose/... follow),
# neither a temporal handle nor a container. A Temporal<T> Accessor such as startValue/endValue
# returns the base value via the per-type MEOS symbol (tcbuffer_start_value -> Cbuffer *) and is
# marshalled by the base-value blob marshaller ({Base}ToBlobN). Derived from PTR_RET so adding a
# base value there (with its type + {Base}ToBlobN marshaller) auto-enables its accessors.
_CONTAINER_PTR_RET = {"Set", "Span", "SpanSet", "STBox", "TBox"}
BASEVAL_PTR_RET = set(PTR_RET) - TEMPORAL_PTR_RET - _CONTAINER_PTR_RET
# Base-value pointer ARGS: the mirror of BASEVAL_PTR_RET on the input side — a family's BASE
# value passed by pointer (Cbuffer today; Pose/Npoint/... when their PTR_IN entry + DuckDB type
# land), as opposed to the temporal handles and the collection/box pointers (owned by other
# shapes). This is the 2nd operand of the uniform (Temporal<T>, T) -> Temporal<T> range-point
# restriction (atValue/minusValue) for the pointer-valued base families; marshalled via PTR_IN
# and freed after the MEOS call.
_CONTAINER_PTR_IN = {"Temporal", "TInstant", "TSequence", "TSequenceSet",
                     "Set", "Span", "SpanSet", "STBox", "TBox"}
BASEVAL_PTR_IN = set(PTR_IN) - _CONTAINER_PTR_IN
# Scalar (by-value) args/returns -> (DuckDB LogicalType, C++ scalar type)
SCALAR = {
    "int":          ("LogicalType::INTEGER",  "int32_t"),
    "int32":        ("LogicalType::INTEGER",  "int32_t"),
    "int32_t":      ("LogicalType::INTEGER",  "int32_t"),
    # uint32 (the PG hash width) registers as the unsigned UINTEGER — see the
    # SCALAR_RET_CPP note below. The uint64 cell ids keep the CELL_UINT path.
    "uint32":       ("LogicalType::UINTEGER", "uint32_t"),
    "uint32_t":     ("LogicalType::UINTEGER", "uint32_t"),
    # NB: uint64_t is deliberately NOT a generic SCALAR arg/return. A bare uint64 in
    # ARG position is a Tcell<T> cell-id base value (h3index/quadbin) already typed by
    # the CELL_BASEVAL comparison shape; adding it here makes a SECOND shape emit those
    # comparisons -> a duplicate Gen_ body (compile error: redefinition). The only
    # uint64 surface is *_hash_extended's (Temporal, seed) -> uint64, handled by
    # shape_binary via SCALAR_ARG / SCALAR_RET_CPP (UBIGINT); supported() lets the
    # SCALAR_ARG seed through even though arg_type has no generic uint64 mapping.
    "int64":        ("LogicalType::BIGINT",   "int64_t"),
    "int64_t":      ("LogicalType::BIGINT",   "int64_t"),
    "double":       ("LogicalType::DOUBLE",   "double"),
    "float8":       ("LogicalType::DOUBLE",   "double"),
    "bool":         ("LogicalType::BOOLEAN",  "bool"),
    "TimestampTz":  ("LogicalType::TIMESTAMP_TZ", "timestamp_tz_t"),
    "DateADT":      ("LogicalType::DATE",     "date_t"),
}
OUTPRIM = {"int *": "LogicalType::INTEGER", "int64_t *": "LogicalType::BIGINT",
           "uint64_t *": "LogicalType::BIGINT",
           "double *": "LogicalType::DOUBLE", "bool *": "LogicalType::BOOLEAN"}
# Out-param C pointer -> (by-value local the folded MEOS call writes into, DuckDB executor cpp).
# A uint64 cell id rides in an int64_t (bit-preserving) to match the BIGINT-backed cell type.
OUTPARAM_LOCAL = {"int *": ("int", "int32_t"), "int64_t *": ("int64_t", "int64_t"),
                  "uint64_t *": ("uint64_t", "int64_t"), "double *": ("double", "double"),
                  "bool *": ("bool", "bool")}

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
    if b == "nullHandleType" and "*" not in nc:
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
    if b in CELL_UINT and "*" not in nc:   # a bare uint64 RETURN: either a Tcell<T> cell id or a hash
        sc = reg_scope(f["name"])          # (checked here, not via SCALAR, so uint64 is NOT a generic
        if sc and sc[0] == "types" and len(sc[1]) == 1 and sc[1][0] in CELL_BASEVAL:  # scalar ARG — that
            return (CELL_BASEVAL[sc[1][0]], "scalar")   # would shadow the cell comparisons). A single
        return ("LogicalType::UBIGINT", "scalar")       # cell type -> its cell DuckDB type; any other
                                                         # uint64 (*_hash_extended) -> native UBIGINT.
    if b in SCALAR and "*" not in nc:      return (SCALAR[b][0], "scalar")
    if b == "GSERIALIZED" and nc.endswith("*"):  # owned geometry -> DuckDB GEOMETRY (geo marshaller)
        return ("MobilityDuckGeometryType()", "geo")
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
    "tcbuffer", "cbuffer", "th3index", "h3index",
    "tjsonb", "jsonb",
    "tpcpoint", "pcpoint", "tpcpatch", "pcpatch",
    "tnpoint", "npoint", "nsegment",
    "tpose", "pose", "trgeometry", "trgeo", "tposechain", "posechain",
}
# Every temporal family token the catalog function names use; those NOT in REGISTERED_FAMILIES
# are the fast-follow families whose DuckDB type the binding does not register yet.
KNOWN_FAMILIES = REGISTERED_FAMILIES | {
    "th3index", "tnpoint", "tpose", "trgeometry", "trgeo", "tpcpoint", "tpcpatch",
    "tjsonb", "npoint", "nsegment", "pose", "pcpoint", "pcpatch", "jsonb", "h3index",
    # A token missing here makes the gate blind, not strict: it can only reject a
    # name whose family token it knows, so an unlisted family rides in on a name
    # whose OTHER token happens to be unregistered, and emits the moment that one
    # registers. tposechain_to_tpose was rejected only for naming the then-
    # unregistered tpose, and reached the compiler as soon as tpose registered.
}
def unregistered_family_ref(name):
    """The first unregistered family token the name references, else None (in scope)."""
    bad = (set(name.split("_")) & KNOWN_FAMILIES) - REGISTERED_FAMILIES
    return sorted(bad)[0] if bad else None

# Param-name tokens that denote a SPECIFIC temporal family (vs the generic `temp`/`temp1`/
# `inst`/... used for same-type operands). Read straight from the catalog `params[].name`.
_PARAM_FAMILY_TOK = re.compile(
    r'^(tpoint|tfloat|tgeo|tnpoint|tcbuffer|tnumber|tint|tbigint|tbool|ttext'
    r'|tgeompoint|tgeogpoint|tgeometry|tgeography)')
def hetero_temporal_args(f):
    """True when a function's operands are ALL generic `Temporal *` in the C signature but
    the catalog param NAMES denote two or more DISTINCT temporal families (e.g.
    tcbuffer_make(tpoint, tfloat) -> tcbuffer). Such a function cannot be typed per-argument
    from the current catalog — the C args are polymorphic and there are no per-overload
    sqlSignatures — so the single-family specialization would register WRONG argument types.
    Excluded until the catalog carries per-arg SQL types (Track B); reachable meanwhile via
    the text-I/O cast the binding registers by hand."""
    tps = [p for p in (f.get("params") or [])
           if base(p.get("canonical", "")) == "Temporal" and norm(p.get("canonical", "")).endswith("*")]
    if len(tps) < 2:
        return False
    fams = set()
    for p in tps:
        m = _PARAM_FAMILY_TOK.match(p.get("name") or "")
        if m:
            fams.add(m.group(1))
    return len(fams) >= 2

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
    # heterogeneous generic-Temporal operands the catalog can't type per-arg yet (Track B)
    if hetero_temporal_args(f):
        return "hetero-temporal-args"
    in_params, out = classify(f)
    if ret_type(f, out) is None:
        return "ret:" + norm(f["returnType"]["canonical"])
    for p in in_params:
        # A by-value uint64 seed of a *_hash_extended fn has no generic arg_type mapping
        # (uint64 is kept out of SCALAR so it does not shadow the cell-id comparisons),
        # but it IS marshalled by shape_binary via SCALAR_ARG. Accept it ONLY for the
        # *_hash_extended functions so temporal_hash_extended is emittable; a uint64 in
        # any OTHER function is a cell-id base value that must STAY rejected here, else a
        # second (generic ever/always) shape shadows the specialized cell comparison and
        # emits a duplicate Gen_ body (redefinition).
        if arg_type(p["canonical"]) is None and not (
                base(p["canonical"]) in SCALAR_ARG and "*" not in norm(p["canonical"])
                and name.endswith("_hash_extended")):
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
                  # The MEOS *_hash functions return uint32 (PG hash width) and
                  # must register as UINTEGER, not a signed INTEGER that flips
                  # the sign of hashes >= 2**31 (DuckDB RANGE-CHECKS the cast, it
                  # does NOT bit-reinterpret the way PG/C does; PG only *declares*
                  # integer because it lacks unsigned SQL types). The uint64
                  # *_hash_extended value returns UBIGINT analogously (native
                  # unsigned, holds the full uint64 hash; the seed arg is UBIGINT
                  # too — see SCALAR_ARG). uint64 cell ids keep the CELL_UINT path.
                  "uint32_t": ("uint32_t", "LogicalType::UINTEGER"),
                  "uint64_t": ("uint64_t", "LogicalType::UBIGINT"),
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
# The TSpatial<T> subtypes = the geo types PLUS the other spatial temporal families
# (tcbuffer first; tnpoint/tpose/trgeometry/th3index/tpcpoint follow). A spatial family
# inherits the abstract `tspatial_*` surface (SRID/setSRID/transform/asText/asEWKT/atStbox)
# and the generic Temporal<T> surface by BEING IN THIS LIST — it is looped alongside the geo
# types by the `tspatial_*` scope and the generic-temporal "all" writer. Distinct from
# GEO_ALLTYPES, which stays geo-only for the `tgeo` supertype (geometry+geography) and the
# geometry-argument spatial relationships. Add a new spatial family here to inherit the surface.
SPATIAL_ALLTYPES = GEO_ALLTYPES + ["CbufferTypes::tcbuffer()", "H3indexTypes::th3index()",
                                   "QuadbinTypes::tquadbin()", "NpointTypes::tnpoint()",
                                   "PoseTypes::tpose()", "TrgeometryTypes::trgeometry()",
                                   "PosechainTypes::tposechain()"]
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
    # A `<src>_to_<dst>` CONVERSION names its OPERAND with the PREFIX and its RESULT with the
    # suffix, so the `_<type>` token searches below must not read the target as the operand
    # family: `trgeometry_to_tpose` takes a trgeometry, and `_tpose(?=_|$)` matches its tail.
    # Resolve a conversion on its source alone; ret_temporal_type() already maps the target.
    if "_to_" in name:
        src = name.split("_to_")[0]
        if src in TO_TYPE:
            return ("types", [TO_TYPE[src]])
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
    # the abstract spatial supertype tspatial_* covers ALL spatial temporal types (the 4 geo
    # types + tcbuffer + future spatial families) with type-preserving results (setSRID/
    # transform/transformPipeline preserve the operand type; asText/asEWKT return text);
    # geometry-coupled variants auto-exclude. This is the TSpatial<T> inherited surface.
    if name.startswith("tspatial_") or re.search(r'_tspatial(?=_|$)', name):
        return ("types", SPATIAL_ALLTYPES)
    # the geo supertype tgeo covers ONLY geometry+geography (NOT cbuffer/other spatial types).
    # Both spellings, as for every other family here: a `tgeo_*` PREFIX (tgeo_stboxes,
    # tgeo_split_n_stboxes) and a `_tgeo` token anywhere (ever_eq_tgeo_tgeo).
    if name.startswith("tgeo_") or re.search(r'_tgeo(?=_|$)', name):
        return ("types", GEO_ALLTYPES)
    # the circular-buffer temporal family (its own gated spatial type, NOT one of the geo
    # types): a tcbuffer_* prefix, or a _tcbuffer token anywhere (ever_eq_tcbuffer_tcbuffer,
    # tdwithin_tcbuffer_tcbuffer, contains_tcbuffer_tcbuffer). GSERIALIZED/Datum-coupled
    # variants auto-exclude on their unmarshallable arg.
    if name.startswith("tcbuffer_") or re.search(r'_tcbuffer(?=_|$)', name):
        return ("types", ["CbufferTypes::tcbuffer()"])
    # the H3 temporal cell index (its own gated spatial type): a th3index_* prefix, or a
    # _th3index token anywhere (ever_eq_h3index_th3index, teq_th3index_th3index). The static
    # h3index/h3indexset-coupled variants auto-exclude on their unmarshallable arg/return.
    if name.startswith("th3index_") or re.search(r'_th3index(?=_|$)', name):
        return ("types", ["H3indexTypes::th3index()"])
    # the CARTO QUADBIN temporal cell index (its own gated spatial type, sibling of th3index):
    # a tquadbin_* prefix, or a _tquadbin token anywhere. The static quadbin/quadbinset-coupled
    # variants auto-exclude on their unmarshallable arg/return.
    if name.startswith("tquadbin_") or re.search(r'_tquadbin(?=_|$)', name):
        return ("types", ["QuadbinTypes::tquadbin()"])
    # the temporal JSONB family (its own gated non-spatial type): a tjsonb_* prefix, or a
    # _tjsonb token anywhere. The base-jsonb-value-coupled variants (startValue/atValue with a
    # Jsonb arg/return) auto-exclude on their unmarshallable Jsonb arg/return until the base
    # value type + marshaller land; the temporal-only surface (comparisons, restrictions by
    # temporal, transforms) emits here.
    if name.startswith("tjsonb_") or re.search(r'_tjsonb(?=_|$)', name):
        return ("types", ["TJsonbTypes::tjsonb()"])
    # the temporal pointcloud families (their own gated types): a tpcpoint_*/tpcpatch_*
    # prefix, or a _tpcpoint/_tpcpatch token anywhere. The base pcpoint/pcpatch-value-coupled
    # accessors marshal via the Pcpoint/Pcpatch varlena marshaller; geometry-coupled variants
    # auto-exclude on their unmarshallable arg/return.
    if name.startswith("tpcpoint_") or re.search(r'_tpcpoint(?=_|$)', name):
        return ("types", ["TPcpointTypes::tpcpoint()"])
    if name.startswith("tpcpatch_") or re.search(r'_tpcpatch(?=_|$)', name):
        return ("types", ["TPcpatchTypes::tpcpatch()"])
    # the temporal rigid-body pose family (its own gated spatial type): a tpose_* prefix, or a
    # _tpose token anywhere (ever_eq_tpose_tpose, teq_tpose_tpose). The base pose value marshals
    # via the Pose varlena marshaller; geometry- and geopose-coupled variants auto-exclude on
    # their unmarshallable arg/return.
    if name.startswith("tpose_") or re.search(r'_tpose(?=_|$)', name):
        return ("types", ["PoseTypes::tpose()"])
    # the temporal rigid geometry (its own gated spatial type, base value `pose`): a
    # trgeometry_*/trgeo_* prefix, or a _trgeometry/_trgeo token anywhere. rgeo has ONE leaf, so
    # `trgeo` is the file/helper abbreviation of the same family, not a superclass over others.
    # GSERIALIZED-coupled variants auto-exclude on their unmarshallable arg/return.
    if (name.startswith("trgeometry_") or name.startswith("trgeo_")
            or re.search(r'_trgeometry(?=_|$)', name) or re.search(r'_trgeo(?=_|$)', name)):
        return ("types", ["TrgeometryTypes::trgeometry()"])
    # the temporal pose chain (its own gated spatial type, base value `posechain`): a
    # tposechain_* prefix, or a _tposechain token anywhere. Its value is a fixed-size varlena,
    # so unlike trgeometry it rides the generic Temporal<T> surface rather than owning one.
    if name.startswith("tposechain_") or re.search(r'_tposechain(?=_|$)', name):
        return ("types", ["PosechainTypes::tposechain()"])
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
           "tgeompoint": "TgeompointType::tgeompoint()", "tgeogpoint": "TgeogpointType::tgeogpoint()",
           "tcbuffer": "CbufferTypes::tcbuffer()", "th3index": "H3indexTypes::th3index()",
           "tquadbin": "QuadbinTypes::tquadbin()", "tjsonb": "TJsonbTypes::tjsonb()",
           "tpcpoint": "TPcpointTypes::tpcpoint()", "tpcpatch": "TPcpatchTypes::tpcpatch()",
           "tnpoint": "NpointTypes::tnpoint()", "tpose": "PoseTypes::tpose()",
           "trgeometry": "TrgeometryTypes::trgeometry()",
           "tposechain": "PosechainTypes::tposechain()"}
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
    m = re.search(r'_to_(tint|tbigint|tfloat|tbool|ttext|tgeometry|tgeography|tgeompoint|tgeogpoint|tcbuffer|tpose|trgeometry|tposechain)$', name)
    return TO_TYPE[m.group(1)] if m else arg_acc

# The concrete DuckDB accessors the generic temporal track can emit, keyed by the catalog
# SQL type name: the 5 core types (TemporalTypes::AllTypes()) plus the 4 geo types (the
# second emit loop). Ordered so the generated registrations come out canonically (core
# order, then geo). Accessor strings reuse GEO_TYPES / TO_TYPE verbatim.
SIG_TEMPORAL_ACC = {
    "tint":       "TemporalTypes::tint()",     "tbigint":    "TemporalTypes::tbigint()",
    "tbool":      "TemporalTypes::tbool()",     "tfloat":     "TemporalTypes::tfloat()",
    "ttext":      "TemporalTypes::ttext()",
    "tgeompoint": "TgeompointType::tgeompoint()", "tgeogpoint": "TgeogpointType::tgeogpoint()",
    "tgeometry":  "TGeometryTypes::tgeometry()",  "tgeography": "TGeographyTypes::tgeography()",
    "tcbuffer":   "CbufferTypes::tcbuffer()",      "th3index":   "H3indexTypes::th3index()",
    "tquadbin":   "QuadbinTypes::tquadbin()",      "tjsonb":     "TJsonbTypes::tjsonb()",
    "tpcpoint":   "TPcpointTypes::tpcpoint()",     "tpcpatch":   "TPcpatchTypes::tpcpatch()",
    "tnpoint":    "NpointTypes::tnpoint()",      "tpose":      "PoseTypes::tpose()",
    "trgeometry": "TrgeometryTypes::trgeometry()",
    "tposechain": "PosechainTypes::tposechain()",
}
def sig_declared_accs(f):
    """The exact temporal-operand types this GENERIC (`Temporal *`) function is CREATE
    FUNCTION'd for, read from the catalog's per-overload `sqlSignatures` (the SoT) — so the
    generic AllTypes()+geo blanket registration is replaced by the catalog-declared type set
    (minInstant/maxInstant land on the four ordered types, not all nine; tintInst on tint
    alone). The mechanical replacement for the name/return heuristics on generic functions.
    Returns the accessor list in canonical order, or None when the catalog carries no
    signature for the function (keep the generic loop as the fallback)."""
    sigs = f.get("sqlSignatures")
    if not sigs:
        return None
    have = {a for s in sigs for a in s["args"] if a in SIG_TEMPORAL_ACC}
    return [SIG_TEMPORAL_ACC[t] for t in SIG_TEMPORAL_ACC if t in have] or None

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

# SQL set-type name -> the CORE Duck accessor. Extended sets (geomset/geogset/
# cbufferset/npointset/poseset/...) are absent -> a scalar-param overload on them
# registers nothing here (they are gated to their own family files).
SET_SQL_TO_ACC = dict(SET_TYPES)

def set_scalar_param_sigs(f):
    """For a 2-arg (Set, scalar)->Set function, decide from the catalog sqlSignatures
    whether arg2 is a fixed PARAM (precision/SRID) rather than a set ELEMENT, and if so
    return the [(set accessor, ret accessor)] over the catalog-declared set types.

    The mechanical tell: a scalar param keeps the SAME arg2 SQL type across overloads
    whose set arg1 differs (round: (floatset,integer)+(geomset,integer); setSRID/
    transform: (geomset,integer)+(geogset,integer)), whereas a genuine element-add has
    arg2 co-vary with the set base (setUnion: (geomset,geometry)+(cbufferset,cbuffer)).
    So the set type must come from the signature (floatset), NOT be inferred from the
    element scalar (which wrongly yields intset). Returns the (possibly empty) core-type
    list when arg2 is a param; None when it is an element (keep the element path)."""
    sigs = f.get("sqlSignatures")
    two = [s for s in (sigs or []) if len(s["args"]) == 2]
    if len(two) < 2:
        return None
    by_arg2 = defaultdict(set)
    for s in two:
        by_arg2[s["args"][1]].add(s["args"][0])
    if not any(len(sets) >= 2 for sets in by_arg2.values()):
        return None                                   # arg2 co-varies -> element-add
    out = []
    for s in two:
        acc = SET_SQL_TO_ACC.get(s["args"][0])
        if acc:
            defs = s.get("argDefaults") or [None] * len(s["args"])
            out.append((acc, SET_SQL_TO_ACC.get(s["ret"], acc), defs[1]))
    return out                                        # may be [] (only extended sets)

def sql_default_to_cpp(val):
    """A SQL DEFAULT literal -> the C++ literal to substitute for the omitted argument.
    NULL -> nullptr, TRUE/FALSE -> true/false, a numeric literal (0/15/0.0) verbatim."""
    u = val.strip().upper()
    return {"NULL": "nullptr", "TRUE": "true", "FALSE": "false"}.get(u, val.strip())

def trailing_arg_default(f):
    """The SQL default of the LAST argument if the catalog declares one uniformly across
    overloads (round -> '0', degrees -> 'FALSE'), else None. This is the value to substitute
    when emitting the shorter overload of a SQL-optional trailing argument."""
    defs = {s["argDefaults"][-1] for s in (f.get("sqlSignatures") or []) if s.get("argDefaults")}
    return next(iter(defs)) if len(defs) == 1 and None not in defs else None

def emit_defaulted_unary(name, blobto, toblob, ctype, dval):
    """The shorter (X)->X overload of an (X, scalar-param DEFAULT)->X blob-container function
    (set/span/spanset): a UnaryExecutor body calling the MEOS fn with the default substituted."""
    return (f"static void Gen_{name}_d(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    UnaryExecutor::Execute<string_t, string_t>(args.data[0], result, args.size(),\n"
            f"        [&](string_t a) {{\n"
            f"            {ctype} *s = {blobto}(a);\n            {ctype} *r = {name}(s, {dval});\n            free(s);\n"
            f"            return {toblob}(result, r);\n        }});\n}}\n")

def emit_defaulted_unary_temporal(name, subcast, dval):
    """The shorter (temporal)->temporal overload — NULL-safe like emit_body_binary's temporal
    kind (MEOS NULL -> SQL NULL via TemporalToBlobN), with the SQL default substituted."""
    return (f"static void Gen_{name}_d(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(args.data[0], result, args.size(),\n"
            f"        [&](string_t in, ValidityMask &mask, idx_t idx) -> string_t {{\n"
            f"            Temporal *t = BlobToTemporal(in);\n"
            f"            Temporal *r = {subcast}{name}(t, {dval});\n            free(t);\n"
            f"            return TemporalToBlobN(result, r, mask, idx);\n"
            f"        }});\n}}\n")

def emit_defaulted_unary_temporal_scalar(f, dval):
    """The shorter (Temporal)->scalar overload of a (Temporal, scalar-param DEFAULT)->scalar
    function (duration(temporal[,boolean]), asText/asEWKT(tspatial[,int])): a UnaryExecutor
    calling the MEOS fn with the trailing default substituted, marshalling the by-value/owned
    scalar return exactly like emit_body's scalar branch (scalar_emit3)."""
    name = f["name"]
    cct, rett, rexpr = scalar_emit3(f)
    return (f"static void Gen_{name}_d(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    UnaryExecutor::Execute<string_t, {rett}>(args.data[0], result, args.size(),\n"
            f"        [&](string_t in) {{\n"
            f"            Temporal *t = BlobToTemporal(in);\n"
            f"            {cct} r = {name}(t, {dval});\n            free(t);\n"
            f"            return {rexpr};\n"
            f"        }});\n}}\n")

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
def shape_set(f):
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
    # name-based set_reg_scope misses. Mirror shape_span's contp detection.
    if len(ins) == 2 and setp(ins[0]) and setp(ins[1]):
        if rb == "Set" and rn.endswith("*"):  return ("b_set", "LogicalType::BLOB")
        if rb == "bool" and "*" not in rn:    return ("b_bool", "LogicalType::BOOLEAN")
    # (Set, scalar element) -> Set : setUnion/setMinus/setIntersection with an element value
    if len(ins) == 2 and rb == "Set" and rn.endswith("*"):
        e1 = selem(ins[1])
        if setp(ins[0]) and e1: return ("setsc_set:" + e1, "LogicalType::BLOB")
    # (Set, scalar PARAM) -> Set where arg2 is NOT a set element (degrees(floatset, bool)):
    # a same-set-type return whose trailing scalar is a fixed param, name-scoped to its set
    # (<elem>set_*). Distinct from the element-add setsc_set above (arg2 co-varies there).
    if (len(ins) == 2 and setp(ins[0]) and rb == "Set" and rn.endswith("*")
            and not selem(ins[1]) and base(ins[1]["canonical"]) in SCALAR_ARG
            and "*" not in norm(ins[1]["canonical"]) and set_reg_scope(f["name"])):
        return ("setcsc:" + base(ins[1]["canonical"]), "LogicalType::BLOB")
    # (Set, by-value uint64 seed) -> uint64 hash (set_hash_extended); name-scoped like setcsc.
    if (len(ins) == 2 and setp(ins[0]) and base(ins[1]["canonical"]) in SCALAR_ARG
            and "*" not in norm(ins[1]["canonical"]) and rb == "uint64_t" and "*" not in rn
            and set_reg_scope(f["name"])):
        return ("bsc:" + base(ins[1]["canonical"]), "LogicalType::UBIGINT")
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
    if kind.startswith("setcsc:"):  # (Set, by-value scalar param) -> Set (degrees(floatset, bool))
        _dt, cpp2, marsh = SCALAR_ARG[kind.split(':')[1]]
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::Execute<string_t, {cpp2}, string_t>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t a, {cpp2} a2) {{\n"
                f"            Set *s = BlobToSet(a);\n            Set *r = {name}(s, {marsh});\n            free(s);\n"
                f"            return SetToBlob(result, r);\n        }});\n}}\n")
    if kind.startswith("bsc:"):     # (Set, by-value uint64 seed) -> uint64 hash (set_hash_extended)
        _dt, cpp2, marsh = SCALAR_ARG[kind.split(':')[1]]
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::Execute<string_t, {cpp2}, uint64_t>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t a, {cpp2} a2) {{\n"
                f"            Set *s = BlobToSet(a);\n            uint64_t r = {name}(s, {marsh});\n            free(s);\n"
                f"            return r;\n        }});\n}}\n")
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

# Tcell<T> cell-id base value. A per-type cell accessor (th3index_start_value /
# tquadbin_start_value; reg_scope keys the single cell temporal type) returns the cell id as
# the collapsed uint64 -- spelled "unsigned long" (libh3's H3Index) OR "uint64_t" (MobilityDB's
# Quadbin, or after the MEOS-API canonical normalization). Both map to the family's
# BIGINT-backed cell DuckDB type. A cell temporal type appears here only once its hand accessor
# layer is gone (mirrors the line-772 geometry-accessor guard) -- tquadbin joins when its hand
# src/quadbin/tquadbin.cpp accessors retire, so both Tcell subtypes are then generated identically.
CELL_UINT = {"unsigned long", "uint64_t"}
CELL_BASEVAL = {
    "H3indexTypes::th3index()": "H3indexTypes::h3index()",
    "QuadbinTypes::tquadbin()": "QuadbinTypes::quadbin()",
}

def shape_emittable(f):
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
    # A `const` temporal-pointer return is a BORROWED pointer into the input (MEOS `_p`/peek
    # accessors, e.g. temporal_max_inst_p) — freeing the input or the result is a use-after-free,
    # and every such peek has an owned non-const sibling with the same @sqlfn (temporal_max_instant).
    # Take only the owned (non-const) return so the body's free(t)+TemporalToBlob stays correct.
    if rb in TEMPORAL_PTR_RET and rn.endswith("*") and "const" not in f["returnType"]["canonical"]:
        return ("temporal", "MD_TEMPORAL")   # ret type resolved per-accessor via ret_temporal_type
    # Tcell<T> cell-id base value: a per-type cell accessor (reg_scope keys the single cell
    # temporal type) returns the collapsed uint64 cell id -> the family's cell DuckDB type
    # (startValue/endValue on th3index -> h3index()). Checked BEFORE the generic by-value
    # scalar branch so a uint64 cell id keeps its cell type (BYVAL_RET now includes uint64
    # for *_hash_extended). See CELL_BASEVAL.
    if rb in CELL_UINT and "*" not in rn and sc[0] == "types" and len(sc[1]) == 1 \
            and sc[1][0] in CELL_BASEVAL:
        return ("cellscalar", CELL_BASEVAL[sc[1][0]])
    if rb in BYVAL_RET and "*" not in rn:
        return ("scalar:" + rb, scalar_ret_duck(f))
    if rb in ("Interval", "text", "char") and rn.endswith("*"):   # owned-pointer scalar return (duration / text / cstring)
        return ("scalar:" + rb, scalar_ret_duck(f))
    # base-value pointer return (Temporal<T> accessor: startValue/endValue -> the family base value,
    # e.g. tcbuffer_start_value -> Cbuffer *). reg_scope keys the per-type MEOS symbol to its own
    # family (tcbuffer_ prefix -> tcbuffer only), so no cross-type over-registration; the owned
    # pointer is marshalled + freed by {Base}ToBlobN. `const` returns are borrowed peeks — excluded.
    if rb in BASEVAL_PTR_RET and rn.endswith("*") and "const" not in f["returnType"]["canonical"]:
        return ("baseval:" + rb, PTR_RET[rb][0])
    # spatial accessor returning a geometry (TSpatial<T>: convexHull -> the family's hull
    # GSERIALIZED). The owned GSERIALIZED marshals to the DuckDB GEOMETRY via the geo helper
    # (inverse of GeometryToGSerialized); reg_scope keys the per-type symbol to its own family.
    if rb == "GSERIALIZED" and rn.endswith("*") and "const" not in f["returnType"]["canonical"]:
        # The geo temporal types carry a HAND geometry-accessor layer (twCentroid/convexHull on
        # tgeompoint/tgeogpoint/tgeometry/tgeography); a bare generated name double-registers
        # against it. Emit only for the non-geo spatial families (tcbuffer, …) whose geometry
        # accessors have no hand reg, until the geo hand layer is retired.
        if sc[0] == "types" and set(sc[1]).issubset(set(GEO_ALLTYPES)):
            return None
        return ("geo", "MobilityDuckGeometryType()")
    return None

def emit_body(f, kind):
    name, sqlfn = f["name"], f["sqlfn"]
    # A concrete subtype return (TInstant */TSequence */TSequenceSet *) upcasts to Temporal *
    # before the uniform temporal marshalling; a plain Temporal * return needs no cast.
    subcast = "" if base(f["returnType"]["canonical"]) == "Temporal" else "(Temporal *) "
    if kind == "temporal":          # (Temporal) -> Temporal (pointer return; MEOS NULL -> SQL NULL)
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(args.data[0], result, args.size(),\n"
                f"        [&](string_t in, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            Temporal *t = BlobToTemporal(in);\n"
                f"            Temporal *r = {subcast}{name}(t);\n"
                f"            free(t);\n"
                f"            return TemporalToBlobN(result, r, mask, idx);\n"
                f"        }});\n}}\n")
    if kind.startswith("baseval:"):   # (Temporal) -> base value ptr (startValue/endValue -> Cbuffer *)
        rb = kind.split(":", 1)[1]    # {Base}ToBlobN owns the free + NULL-safe marshalling
        cct = norm(f["returnType"]["canonical"])   # e.g. "Cbuffer *"
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(args.data[0], result, args.size(),\n"
                f"        [&](string_t in, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            Temporal *t = BlobToTemporal(in);\n"
                f"            {cct} r = {name}(t);\n"
                f"            free(t);\n"
                f"            return {rb}ToBlobN(result, r, mask, idx);\n"
                f"        }});\n}}\n")
    if kind == "cellscalar":   # (Temporal) -> uint64 cell id -> BIGINT-backed cell DuckDB type
        # The cell DuckDB type (h3index()/quadbin()) is BIGINT-backed, so the uint64 cell id
        # writes as int64_t (bit-preserving; H3/quadbin cell ids clear the sign bit).
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    UnaryExecutor::Execute<string_t, int64_t>(args.data[0], result, args.size(),\n"
                f"        [&](string_t in) -> int64_t {{\n"
                f"            Temporal *t = BlobToTemporal(in);\n"
                f"            int64_t r = (int64_t) {name}(t);\n"
                f"            free(t);\n"
                f"            return r;\n"
                f"        }});\n}}\n")
    if kind == "geo":   # (Temporal) -> owned GSERIALIZED -> DuckDB GEOMETRY (freed after marshalling)
        cct = norm(f["returnType"]["canonical"])   # "GSERIALIZED *"
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &state, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(args.data[0], result, args.size(),\n"
                f"        [&](string_t in, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            Temporal *t = BlobToTemporal(in);\n"
                f"            {cct} r = {name}(t);\n"
                f"            free(t);\n"
                f"            if (!r) {{ mask.SetInvalid(idx); return string_t(); }}\n"
                f"            string_t out = GSerializedToGeometry(r, state, result);\n"
                f"            free(r);\n"
                f"            return out;\n"
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
    "uint64_t":    ("LogicalType::UBIGINT", "uint64_t", "a2"),  # *_hash_extended seed (native unsigned)
    "double":      ("LogicalType::DOUBLE",  "double",  "a2"),
    "bool":        ("LogicalType::BOOLEAN", "bool",    "a2"),
    "TimestampTz": ("LogicalType::TIMESTAMP_TZ", "timestamp_tz_t", "DuckDBToMeosTimestamp(a2).value"),
    "DateADT":     ("LogicalType::DATE", "date_t", "ToMeosDate(a2)"),  # single-expr, no lifecycle
}
# The VARCHAR half of a shape argument, beside SCALAR_ARG's by-value half: a C string, and the
# null-handling enum MEOS itself parses from one. MobilityDB spells the latter `null_handle text`
# (mobilitydb/sql/json/454_tjsonb_jsonfuncs.in.sql), and null_handle_type_from_string is the public
# MEOS parser, so the binding delegates the mapping instead of carrying its own name table.
VARCHAR_ARG = {
    "char":           ("LogicalType::VARCHAR", "string_t", "a2.GetString().c_str()"),
    "nullHandleType": ("LogicalType::VARCHAR", "string_t",
                       "null_handle_type_from_string(a2.GetString().c_str())"),
}
def shape_arg(p):
    """The (duck_type, cpp_local, marshal_expr) triple for a by-value or VARCHAR argument."""
    bb = base(p["canonical"]); nn = norm(p["canonical"])
    if bb in SCALAR_ARG and "*" not in nn:
        return SCALAR_ARG[bb]
    if bb == "nullHandleType" and "*" not in nn:
        return VARCHAR_ARG[bb]
    if bb == "char" and nn.count("*") == 1:
        return VARCHAR_ARG[bb]
    return None

def shape_binary(f):
    """Binary Temporal + by-value-scalar shape (BinaryExecutor). Same correctness
    rules as shape_emittable: scalar return OR generic same-type temporal return."""
    if supported(f) is not None: return None
    ins, out = classify(f)
    if len(ins) != 2: return None
    if base(ins[0]["canonical"]) != "Temporal" or not norm(ins[0]["canonical"]).endswith("*"): return None
    b2 = base(ins[1]["canonical"]); n2 = norm(ins[1]["canonical"])
    is_text2 = (b2 == "text" and n2.endswith("*"))   # owned text* arg via MakeText
    is_baseptr2 = (b2 in BASEVAL_PTR_IN and n2.endswith("*"))  # base value by pointer (Cbuffer, ...)
    if not is_text2 and not is_baseptr2 and (b2 not in SCALAR_ARG or "*" in n2): return None
    sc = reg_scope(f["name"])
    if sc is None: return None
    if is_text2:
        arg2 = ("LogicalType::VARCHAR", "string_t", "__TEXT__")
    elif is_baseptr2:                                 # marshal via PTR_IN (BlobTo<Base>) + free after
        arg2 = (PTR_IN[b2][0], "string_t", "__PTRFREE__:" + b2)
    else:
        arg2 = SCALAR_ARG[b2]
    if out is not None:
        # Out-param fold: bool <name>(Temporal, <scalar>, <base *out>) -> the folded base value.
        # The inherited Temporal<T> value accessor valueN (meos_temporal_accessor). Only a
        # concrete scalar out-param is folded here; the generic `Datum *` overload (temporal_value_n)
        # drops out (varying per-type return) so exactly the per-type <t>_value_n emit, once each.
        no = norm(out)
        if no not in OUTPARAM_LOCAL: return None
        if sc[0] == "types" and sc[1] and len(sc[1]) == 1 and sc[1][0] in CELL_BASEVAL:
            dret = CELL_BASEVAL[sc[1][0]]            # Tcell<T> cell-id base value (h3index/quadbin)
        else:
            dret = OUTPRIM[no]
        return ("outval:" + no, dret, arg2)
    rb = base(f["returnType"]["canonical"]); rn = norm(f["returnType"]["canonical"])
    if rb in TEMPORAL_PTR_RET and rn.endswith("*"):
        return ("temporal", "MD_TEMPORAL", arg2)
    if rb in BYVAL_RET and "*" not in rn:
        return ("scalar:" + rb, scalar_ret_duck(f), arg2)
    if rb in ("Interval", "text", "char") and rn.endswith("*"):   # owned-pointer scalar return
        return ("scalar:" + rb, scalar_ret_duck(f), arg2)
    return None

def emit_body_binary(f, kind, arg2):
    name = f["name"]; dt2, cpp2, marsh = arg2
    is_text = (marsh == "__TEXT__")
    is_ptr = marsh.startswith("__PTRFREE__:")   # base value by pointer: marshal via BlobTo<Base>, then free
    if is_text:
        pre, call2, post = "text *a2t = MakeText(a2);\n            ", "a2t", "free(a2t); "
    elif is_ptr:
        pb = marsh.split(":", 1)[1]
        pre, call2, post = f"{pb} *a2p = {PTR_IN[pb][1] % 'a2'};\n            ", "a2p", "free(a2p); "
    else:
        pre, call2, post = "", marsh, ""
    if kind.startswith("outval:"):  # bool <name>(t, a2, &v) -> the folded base value (valueN); false -> SQL NULL
        loc, exec_cpp = OUTPARAM_LOCAL[kind.split(":", 1)[1]]
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::ExecuteWithNulls<string_t, {cpp2}, {exec_cpp}>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t in, {cpp2} a2, ValidityMask &mask, idx_t idx) -> {exec_cpp} {{\n"
                f"            Temporal *t = BlobToTemporal(in);\n"
                f"            {pre}{loc} v;\n"
                f"            bool ok = {name}(t, {call2}, &v);\n            {post}free(t);\n"
                f"            if (!ok) {{ mask.SetInvalid(idx); return {exec_cpp}(); }}\n"
                f"            return ({exec_cpp}) v;\n"
                f"        }});\n}}\n")
    subcast = "" if base(f["returnType"]["canonical"]) == "Temporal" else "(Temporal *) "
    if kind == "temporal":          # pointer return -> NULL-safe (MEOS NULL -> SQL NULL)
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::ExecuteWithNulls<string_t, {cpp2}, string_t>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t in, {cpp2} a2, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            Temporal *t = BlobToTemporal(in);\n"
                f"            {pre}Temporal *r = {subcast}{name}(t, {call2});\n            {post}free(t);\n"
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
                # A prototype counts if it is `extern`-qualified OR starts with a MEOS
                # return type. The width-suffixed stdint types (int32_t/uint64/…) are
                # included so by-value integer accessors like `int32_t tspatial_srid(...)`
                # are seen by the gate — otherwise the plain `int `/`bool ` prefixes miss
                # them and the function is silently dropped from the generated surface.
                if m and ("extern" in line or line.lstrip().startswith((
                        "int ", "bool ", "double ", "Temporal ", "void ",
                        "int8 ", "int16 ", "int32 ", "int64 ",
                        "uint8 ", "uint16 ", "uint32 ", "uint64 ",
                        "int8_t ", "int16_t ", "int32_t ", "int64_t ",
                        "uint8_t ", "uint16_t ", "uint32_t ", "uint64_t ", "size_t "))):
                    syms.add(m.group(1))
        except OSError:
            pass
    return syms

def shape_ternary(f):
    """Ternary Temporal + 2 by-value scalars (TernaryExecutor). Same correctness rules."""
    if supported(f) is not None: return None
    ins, out = classify(f)
    if out is not None or len(ins) != 3: return None
    if base(ins[0]["canonical"]) != "Temporal" or not norm(ins[0]["canonical"]).endswith("*"): return None
    a2 = shape_arg(ins[1]); a3 = shape_arg(ins[2])
    if a2 is None or a3 is None: return None
    if reg_scope(f["name"]) is None: return None
    rb = base(f["returnType"]["canonical"]); rn = norm(f["returnType"]["canonical"])
    if rb == "Temporal" and rn.endswith("*"):
        return ("temporal", "MD_TEMPORAL", a2, a3)
    if rb in BYVAL_RET and "*" not in rn:
        return ("scalar:" + rb, scalar_ret_duck(f), a2, a3)
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

# A MEOS function whose C signature is WIDER than the SQL surface: the PG wrapper binds some
# parameters to literals (`shape.boundArgs`, e.g. `tjsonbObjectField` binds `astext` to false and
# `tjsonbObjectFieldText` binds it to true over the same kernel), and any parameter the SQL surface
# DOES expose past the first two is a per-call setting. DuckDB has no quaternary executor, so a
# setting is read once outside the executor — the `tsequence_make` idiom — and a BinaryExecutor runs
# over the two arguments that vary per row.
VARY_ARG = {   # second column: (duck_type, cpp_local, "expr from the local", "cleanup or ''")
    "text":   ("LogicalType::VARCHAR", "string_t", "MakeText(%s)", "free"),
    "int":    ("LogicalType::INTEGER", "int32_t",  "%s", ""),
    "double": ("LogicalType::DOUBLE",  "double",   "%s", ""),
    "bool":   ("LogicalType::BOOLEAN", "bool",     "%s", ""),
}
SETTING_ARG = {  # trailing column: (duck_type, cpp_type, "expr from a duckdb::Value named v")
    "bool":           ("LogicalType::BOOLEAN", "bool",     "v.GetValue<bool>()"),
    "int":            ("LogicalType::INTEGER", "int32_t",  "v.GetValue<int32_t>()"),
    "double":         ("LogicalType::DOUBLE",  "double",   "v.GetValue<double>()"),
    "nullHandleType": ("LogicalType::VARCHAR", "nullHandleType",
                       "null_handle_type_from_string(v.ToString().c_str())"),
}
def _vary(p):
    b = base(p["canonical"]); n = norm(p["canonical"])
    if b == "text" and n.endswith("*"): return VARY_ARG["text"]
    if b in VARY_ARG and b != "text" and "*" not in n: return VARY_ARG[b]
    return None

def shape_bound_tail(f):
    """(Temporal *, varying, <bound literals and per-call settings...>) -> Temporal *.

    The SQL surface is `sqlSignatures.args`; every C parameter the wrapper binds is emitted as
    its literal rather than exposed. Exposing a bound parameter would publish a signature
    MobilityDB does not declare."""
    if supported(f) is not None: return None
    ins, out = classify(f)
    if out is not None or len(ins) < 4: return None
    if base(ins[0]["canonical"]) != "Temporal" or not norm(ins[0]["canonical"]).endswith("*"):
        return None
    bound = ((f.get("shape") or {}).get("boundArgs") or {})
    if not bound: return None            # without the literal the tail cannot be emitted
    v = _vary(ins[1])
    if v is None: return None
    tail = []                            # (kind, payload) per C parameter after the second
    for p in ins[2:]:
        nm = p.get("name")
        if nm in bound:
            tail.append(("lit", bound[nm]))
            continue
        st = SETTING_ARG.get(base(p["canonical"])) if "*" not in norm(p["canonical"]) else None
        if st is None: return None
        tail.append(("col", st))
    if not any(k == "col" for k, _ in tail): return None
    if reg_scope(f["name"]) is None: return None
    rb = base(f["returnType"]["canonical"]); rn = norm(f["returnType"]["canonical"])
    if not (rb == "Temporal" and rn.endswith("*")): return None
    # the registered signature is the type, the varying arg, then each exposed setting
    sig = [v[0]] + [st[0] for k, st in tail if k == "col"]
    return ("temporal", "MD_TEMPORAL", v, tail, sig)

def emit_body_bound_tail(f, kind, vary, tail, sig):
    name = f["name"]
    _dt, cpp2, expr2, cleanup = vary
    L = [f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{",
         "    EnsureMeosThreadInitialized();",
         "    auto rc = args.size();"]
    call, col = [], 2
    for kindi, payload in tail:
        if kindi == "lit":
            call.append(payload)
            continue
        _d, ctype, cexpr = payload
        L.append(f"    {ctype} c{col} = ({ctype}) 0;")
        L.append(f"    {{ auto &cv = args.data[{col}]; cv.Flatten(rc); Value v = cv.GetValue(0);"
                 f" if (!v.IsNull()) c{col} = {cexpr}; }}")
        call.append(f"c{col}")
        col += 1
    tailargs = "".join(", " + a for a in call)
    lead = expr2 % "a2"
    L += [f"    BinaryExecutor::ExecuteWithNulls<string_t, {cpp2}, string_t>("
          "args.data[0], args.data[1], result, rc,",
          f"        [&](string_t in, {cpp2} a2, ValidityMask &mask, idx_t idx) -> string_t {{",
          "            Temporal *t = BlobToTemporal(in);"]
    if cleanup:
        L += [f"            auto p2 = {lead};",
              f"            Temporal *r = {name}(t, p2{tailargs});",
              f"            free(t); {cleanup}(p2);"]
    else:
        L += [f"            Temporal *r = {name}(t, {lead}{tailargs});",
              "            free(t);"]
    L += ["            return TemporalToBlobN(result, r, mask, idx);",
          "        });", "}", ""]
    return "\n".join(L)

def shape_binary_tt(f):
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

def shape_binary_tt_scalar(f):
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

def shape_geo_temporal(f):
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
    # A geo spatial-RELATIONSHIP backing (group *_rel_ever / *_rel_temp) is admitted even when
    # the name heuristic leaves it unscoped: the point-specific _tpoint_geo touches carry no
    # reg_scope token (etouches_tpoint_geo -> None), yet they are the correct trajectory-based
    # backing for temporal points. Its exact per-type scope comes from the catalog sqlSignatures
    # in gen_cpp (sig_declared_accs), not this heuristic.
    if reg_scope(name) is None and not re.search(r'_rel_(ever|temp)$', f.get("group") or ""):
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
    # When the temporal operand is geodetic, coerce the geometry to geography so the
    # planar/geodetic flags + bbox match before the MEOS call (MEOS correctly errors on a
    # mixed planar/geodetic pair). DuckDB has a single GEOMETRY type, so this cross-argument
    # coercion is the binding's job; the SRID likewise comes from the sibling temporal.
    marshal = ("            Temporal *t = BlobToTemporal(in_t);\n"
               "            GSERIALIZED *gs = GeometryToGSerialized(in_g, tspatial_srid(t));\n"
               "            if (MEOS_FLAGS_GET_GEODETIC(t->flags)) {\n"
               "                GSERIALIZED *gs_geog = geom_to_geog(gs);\n"
               "                free(gs); gs = gs_geog;\n"
               "            }\n")
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

def shape_path(f):
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

# ---- general array-return shape: <elem>* + int* count -> DuckDB LIST(<elem-duck>) ----
# The flat-LIST general case of shape_path (which handles the LIST(STRUCT) *Path structs).
# Entirely catalog-driven: shape.arrayReturn carries the element type (from MEOS-API shapeinfer)
# and lengthFrom.name the trailing int* count out-param. Element marshalling is ONE boundary
# table keyed on the canonical element type (zero heuristics): a scalar element goes to a typed
# child vector; a text* element to a VARCHAR child (each varlena freed); a MEOS value-type struct
# (Span/STBox/TBox) to a BLOB child by sizeof (the array is freed once, elements are inline).
# elem canonical(normalized) -> (LIST child LogicalType, child C++ type, per-element marshal stmt)
ARRAY_ELEM = {
    "int":         ("LogicalType::INTEGER",      "int32_t",        "cd[off + j] = arr[j];"),
    "int32_t":     ("LogicalType::INTEGER",      "int32_t",        "cd[off + j] = arr[j];"),
    "int64_t":     ("LogicalType::BIGINT",       "int64_t",        "cd[off + j] = arr[j];"),
    # Tcell<T> cell id (uint64_t) rides in a signed int64_t child (bit-preserving), matching the
    # BIGINT-backed cell alias; the LIST's LOGICAL element type is the cell alias (array_ret_duck).
    "uint64_t":    ("LogicalType::BIGINT",       "int64_t",        "cd[off + j] = (int64_t) arr[j];"),
    "double":      ("LogicalType::DOUBLE",       "double",         "cd[off + j] = arr[j];"),
    "bool":        ("LogicalType::BOOLEAN",      "bool",           "cd[off + j] = arr[j];"),
    # date/timestamp elements carry the MEOS (PG) epoch: convert to the DuckDB epoch with the
    # same helpers the scalar-return bodies use (TakeTimestamp / FromMeosDate), never a raw cast.
    "TimestampTz": ("LogicalType::TIMESTAMP_TZ", "timestamp_tz_t", "cd[off + j] = TakeTimestamp(arr[j]);"),
    "DateADT":     ("LogicalType::DATE",         "date_t",         "cd[off + j] = FromMeosDate((int32_t) arr[j]);"),
    "text *":      ("LogicalType::VARCHAR", "string_t", "cd[off + j] = TakeText(child_vector, arr[j]);"),
    "Span":  ("LogicalType::BLOB", "string_t",
              "cd[off + j] = StringVector::AddStringOrBlob(child_vector, (const char *) &arr[j], sizeof(Span));"),
    "STBox": ("LogicalType::BLOB", "string_t",
              "cd[off + j] = StringVector::AddStringOrBlob(child_vector, (const char *) &arr[j], sizeof(STBox));"),
    "TBox":  ("LogicalType::BLOB", "string_t",
              "cd[off + j] = StringVector::AddStringOrBlob(child_vector, (const char *) &arr[j], sizeof(TBox));"),
}
# container-input family (base of the single non-count param) -> (C container type, Blob->container fn)
ARRAY_IN = {
    "Set":          ("Set",      "BlobToSet"),
    "SpanSet":      ("SpanSet",  "BlobToSpanSet"),
    "Temporal":     ("Temporal", "BlobToTemporal"),
    "TInstant":     ("Temporal", "BlobToTemporal"),
    "TSequence":    ("Temporal", "BlobToTemporal"),
    "TSequenceSet": ("Temporal", "BlobToTemporal"),
}

def shape_tgeoarr(f):
    """The NxN set-set functions take two arrays-of-temporal (const Temporal ** + int count) pairs.
    Two catalog shapes are emitted as DuckDB scalars over LIST(<geo>) x LIST(<geo>):
      minDistance(tgeo[],tgeo[]) -> double                 = ('scalar_double',)
      the *Pairs SRFs: int* flattened [i,j] pairs + int* count (+ double dist, + SpanSet*** periods)
        -> LIST(STRUCT(i,j[,periods]))                      = ('pairs', has_dist, has_periods)
    The Temporal** args make base()=='__INTERNAL__' so every other shape rejects them, so this shape
    applies its own eligibility checks. The *Pairs result is a scalar LIST(STRUCT) (UNNEST to rows),
    NOT a table fn: the BerlinMOD q06/q10 args are per-row collect_list LISTs, which a DuckDB table
    fn (bind-time-const args) cannot take without LATERAL."""
    name = f["name"]
    if name.startswith("meos_internal") or (f.get("group") or "").startswith("meos_internal"):
        return None
    if not f.get("sqlfn") or re.search(r'_(out|in|send|recv)$', f.get("sqlfn") or ""):
        return None
    ps = f["params"]
    def is_temp_arr(p): return norm(p["canonical"]) == "Temporal **"
    def is_int(p): return base(p["canonical"]) in ("int", "int32_t") and "*" not in norm(p["canonical"])
    # both forms begin (const Temporal ** arr1, int count1, const Temporal ** arr2, int count2)
    if len(ps) < 4 or not (is_temp_arr(ps[0]) and is_int(ps[1]) and is_temp_arr(ps[2]) and is_int(ps[3])):
        return None
    rb = base(f["returnType"]["canonical"]); rn = norm(f["returnType"]["canonical"])
    if len(ps) == 4 and rb == "double" and "*" not in rn:
        return ("scalar_double",)
    # *Pairs: int* return with a catalog arrayReturn (flattened index pairs) + int* count out-param
    ar = (f.get("shape") or {}).get("arrayReturn")
    if ar and rb == "int" and rn.endswith("*"):
        pnames = [p.get("name") for p in ps]
        return ("pairs", "dist" in pnames, "periods" in pnames)
    return None

def emit_tgeoarr_scalar(f):
    """DuckDB scalar over two LIST(temporal-blob) args -> double (minDistance). Each LIST row is
    marshalled to a fresh Temporal** via ListToTemporalArr, the MEOS kernel called, the arrays freed."""
    name = f["name"]
    return (
f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
f"    EnsureMeosThreadInitialized();\n"
f"    auto &lv1 = args.data[0]; auto &lv2 = args.data[1];\n"
f"    auto &child1 = ListVector::GetEntry(lv1); child1.Flatten(ListVector::GetListSize(lv1));\n"
f"    auto &child2 = ListVector::GetEntry(lv2); child2.Flatten(ListVector::GetListSize(lv2));\n"
f"    BinaryExecutor::Execute<list_entry_t, list_entry_t, double>(\n"
f"        lv1, lv2, result, args.size(),\n"
f"        [&](list_entry_t le1, list_entry_t le2) -> double {{\n"
f"            int n1, n2;\n"
f"            const Temporal **a1 = ListToTemporalArr(child1, le1, &n1);\n"
f"            const Temporal **a2 = ListToTemporalArr(child2, le2, &n2);\n"
f"            double r = {name}(a1, n1, a2, n2);\n"
f"            FreeTemporalArr(a1, n1); FreeTemporalArr(a2, n2);\n"
f"            return r;\n"
f"        }});\n}}\n")

def emit_pairs_scalar(f, has_dist, has_periods):
    """DuckDB scalar over two LIST(temporal-blob) args (+ optional double dist) -> a
    LIST(STRUCT(i INTEGER, j INTEGER[, periods TSTZSPANSET])) (the NxN *Pairs SRFs). The MEOS kernel
    returns a flattened [i0,j0,i1,j1,...] int array of `count` index pairs (+ for the t-variants a
    parallel SpanSet** of `count` tstzspansets); each row is reshaped into a DuckDB LIST(STRUCT) that
    UNNEST expands to rows. Mirrors emit_array's Reserve/SetListSize LIST-build over a STRUCT child."""
    name = f["name"]
    dist_flat = ("    args.data[2].Flatten(row_count);\n"
                 "    auto dd = FlatVector::GetData<double>(args.data[2]);\n") if has_dist else ""
    dist_arg  = "dd[r], " if has_dist else ""
    per_decl  = "        SpanSet **periods = nullptr;\n" if has_periods else ""
    per_arg   = ", &periods" if has_periods else ""
    per_field = "            auto &pv = *sf[2];\n" if has_periods else ""
    per_fill  = ("                FlatVector::GetData<string_t>(pv)[off + k] = SpanSetToBlob(pv, periods[k]);\n"
                 if has_periods else "")
    # Each periods[k] SpanSet* is consumed+freed by SpanSetToBlob above; free only the outer array.
    per_free  = "        if (periods) free(periods);\n" if has_periods else ""
    return (
f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
f"    EnsureMeosThreadInitialized();\n"
f"    idx_t row_count = args.size();\n"
f"    auto &lv1 = args.data[0]; auto &lv2 = args.data[1];\n"
f"    lv1.Flatten(row_count); lv2.Flatten(row_count);\n"
f"    auto le1 = FlatVector::GetData<list_entry_t>(lv1);\n"
f"    auto le2 = FlatVector::GetData<list_entry_t>(lv2);\n"
f"    auto &v1 = FlatVector::Validity(lv1);\n"
f"    auto &v2 = FlatVector::Validity(lv2);\n"
f"    auto &child1 = ListVector::GetEntry(lv1); child1.Flatten(ListVector::GetListSize(lv1));\n"
f"    auto &child2 = ListVector::GetEntry(lv2); child2.Flatten(ListVector::GetListSize(lv2));\n"
f"{dist_flat}"
f"    auto list_entries = FlatVector::GetData<list_entry_t>(result);\n"
f"    auto &result_validity = FlatVector::Validity(result);\n"
f"    idx_t off = 0;\n"
f"    for (idx_t r = 0; r < row_count; r++) {{\n"
f"        if (!v1.RowIsValid(r) || !v2.RowIsValid(r)) {{\n"
f"            result_validity.SetInvalid(r); list_entries[r] = list_entry_t{{off, 0}}; continue;\n"
f"        }}\n"
f"        int n1, n2;\n"
f"        const Temporal **a1 = ListToTemporalArr(child1, le1[r], &n1);\n"
f"        const Temporal **a2 = ListToTemporalArr(child2, le2[r], &n2);\n"
f"        int cnt = 0;\n"
f"{per_decl}"
f"        int *res = {name}(a1, n1, a2, n2, {dist_arg}&cnt{per_arg});\n"
f"        FreeTemporalArr(a1, n1); FreeTemporalArr(a2, n2);\n"
f"        int n = (res && cnt > 0) ? cnt : 0;\n"
f"        ListVector::Reserve(result, off + n);\n"
f"        ListVector::SetListSize(result, off + n);\n"
f"        list_entries[r] = list_entry_t{{off, (uint64_t) n}};\n"
f"        if (n > 0) {{\n"
f"            auto &sv = ListVector::GetEntry(result);\n"
f"            auto &sf = StructVector::GetEntries(sv);\n"
f"            auto id = FlatVector::GetData<int32_t>(*sf[0]);\n"
f"            auto jd = FlatVector::GetData<int32_t>(*sf[1]);\n"
f"{per_field}"
f"            for (int k = 0; k < n; k++) {{\n"
f"                id[off + k] = res[2 * k];\n"
f"                jd[off + k] = res[2 * k + 1];\n"
f"{per_fill}"
f"            }}\n"
f"            off += n;\n"
f"        }}\n"
f"        if (res) free(res);\n"
f"{per_free}"
f"        result_validity.SetValid(r);\n"
f"    }}\n"
f"    if (row_count == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);\n"
f"}}\n")

def _acc_sqlname(acc):
    """The canonical SQL type name inside an accessor string, e.g. SetTypes::intset() -> intset,
    TemporalTypes::tbool() -> tbool (the meos_catalog.c lowercase name the sqlSignatures key on)."""
    m = re.search(r'::(\w+)\(\)', acc)
    return m.group(1) if m else acc

def _sig_ret_for(f, sqlname, tail=()):
    """The catalog sqlSignature return for the overload whose args are `sqlname` followed by `tail`,
    else None. `tail` names the fixed scalar SQL types trailing the container argument: empty for a
    plain array accessor (`spans(tgeompoint)`), ("integer",) for the split-into-N accessors
    (`splitNStboxes(tgeompoint, integer)`)."""
    want = [sqlname, *tail]
    for s in (f.get("sqlSignatures") or []):
        if s.get("args") == want:
            return s.get("ret")
    return None

def array_declared_accs(f, tail=()):
    """The registered DuckDB accessors this array-return is CREATE FUNCTION'd for, from the catalog
    sqlSignatures whose SQL return is an array (`<base>[]`). Extended/unregistered arg types map to
    no accessor and drop out. This is the SoT for a generic (scope='all') array-return's type set,
    excluding types whose canonical return is NOT an array (e.g. set_spans over textset: no sig).
    `tail` matches the fixed scalar arguments after the container, as in _sig_ret_for."""
    m = {**SET_TYPES, **SPANSET_TYPES, **SIG_TEMPORAL_ACC}
    out = []
    want_len = 1 + len(tail)
    for s in (f.get("sqlSignatures") or []):
        args = s.get("args", [])
        if len(args) != want_len or list(args[1:]) != list(tail):
            continue
        if not (s.get("ret") or "").endswith("[]"):
            continue
        a = m.get(args[0])
        if a and a not in out:
            out.append(a)
    return out

# The DuckDB LIST child LOGICAL type, keyed on the canonical SQL array element (the sig ret base).
# Struct-blob elements use the NAMED span/box type (BLOB-backed, but typed so results display /
# typeof / cast like the hand surface), not a raw BLOB. Physical marshalling stays keyed on the C
# element (ARRAY_ELEM); this only fixes the LOGICAL element type per the catalog sqlSignatures.
SQL_BASE_TO_DUCK = {
    "integer": "LogicalType::INTEGER", "bigint": "LogicalType::BIGINT", "float": "LogicalType::DOUBLE",
    "boolean": "LogicalType::BOOLEAN", "text": "LogicalType::VARCHAR", "date": "LogicalType::DATE",
    "timestamptz": "LogicalType::TIMESTAMP_TZ",
    "intspan": "SpanTypes::intspan()", "bigintspan": "SpanTypes::bigintspan()",
    "floatspan": "SpanTypes::floatspan()", "datespan": "SpanTypes::datespan()",
    "tstzspan": "SpanTypes::tstzspan()", "tbox": "TboxType::tbox()", "stbox": "StboxType::stbox()",
    "tpcbox": "TpcboxType::tpcbox()",
}

def array_ret_duck(f, acc, tail=()):
    """The DuckDB LIST return type for this array-return on input accessor `acc`, from the catalog
    sqlSignature (e.g. spans(intset)->intspan[] -> LIST(SpanTypes::intspan())). None if not an
    array sig or the element type is not mapped (deferred). `tail` selects the overload with the
    fixed scalar arguments trailing the container, as in _sig_ret_for."""
    # Tcell<T> cell-id array: LIST of the cell alias (getValues(tquadbin)->LIST(quadbin)); the
    # canonical <cell>set SQL return maps to a DuckDB LIST here (see the cell branch in shape_array).
    # GATE on the ELEMENT being a cell uint64 — a cell temporal's timestamps()/other array accessors
    # keep their own element type (e.g. LIST(TIMESTAMP_TZ)), so acc-in-CELL_BASEVAL alone is not enough.
    ar = (f.get("shape") or {}).get("arrayReturn") or {}
    ec = norm((ar.get("element") or {}).get("canonical") or (ar.get("element") or {}).get("c") or "")
    if ec in CELL_UINT and acc in CELL_BASEVAL:
        return "LogicalType::LIST(%s)" % CELL_BASEVAL[acc]
    r = _sig_ret_for(f, _acc_sqlname(acc), tail) or ""
    if not r.endswith("[]"):
        return None
    d = SQL_BASE_TO_DUCK.get(r[:-2])
    return "LogicalType::LIST(%s)" % d if d else None

def shape_array(f):
    """Flat array-return -> DuckDB LIST(<elem>). Like shape_path, the trailing int* count arg
    makes supported() reject it (arg:int *), so gate on user-facing eligibility + a marshallable
    container input, not the standard arg gate. Returns ('array', elem_canon, in_base, accs, tail)
    or None, where `tail` is the fixed scalar SQL args after the container — () for a plain
    accessor, ("integer",) for the split-into-N family. Geo/extended element types
    (GSERIALIZED*/Cbuffer*/...) are not in ARRAY_ELEM -> deferred, as are the array-returns whose
    extra arguments are not a single by-value int (*Pairs, table-fn shapes).

    CANONICAL GATE (the catalog sqlSignatures are the SoT): a LIST overload is emitted for an input
    type ONLY where MobilityDB's SQL surface returns a SQL array (`<base>[]`) for it. Several MEOS
    accessors carry @sqlfn=getValues but their canonical SQL return is a spanset/set, not an array
    (getValues(tint)->intspanset via Tnumber_valuespans; getValues(ttext)->textset), so no LIST
    overload is emitted for them."""
    name = f["name"]
    if name.startswith("meos_internal") or (f.get("group") or "").startswith("meos_internal"):
        return None
    sqlfn = f.get("sqlfn")
    if not sqlfn or re.search(r'_(out|in|send|recv)$', sqlfn):
        return None
    if name.endswith("_p"):
        return None                       # internal pointer-preserving twin (temporal_sequences_p)
    ar = (f.get("shape") or {}).get("arrayReturn")
    if not ar:
        return None
    # NB: no STRUCTS-fields exclusion here — the MEOS value-type structs Span/STBox/TBox ARE handled
    # (as opaque BLOB elements, in ARRAY_ELEM); the LIST(STRUCT) path structs (Match/warp) are routed
    # out below because their element is not in ARRAY_ELEM (shape_path handles those separately).
    lf = ar.get("lengthFrom") or {}
    if lf.get("kind") != "param" or not lf.get("name"):
        return None
    ec = norm((ar.get("element") or {}).get("canonical") or (ar.get("element") or {}).get("c") or "")
    if ec not in ARRAY_ELEM:
        return None
    ins = [p for p in f["params"] if p.get("name") != lf["name"]]
    # The container, plus the fixed by-value scalars that follow it. One trailing `int` is the
    # split-into-N family (splitNStboxes/splitNSpans/splitNTboxes and their splitEachN twins):
    # the same array-return over the same container, parameterised by how many boxes to produce
    # or how many elements to merge per box. Anything else after the container is a different
    # shape and stays out (the *Pairs / table-fn forms).
    if not ins:
        return None
    tailp = ins[1:]
    if len(tailp) > 1 or any(norm(p["canonical"]) != "int" for p in tailp):
        return None
    tail = ("integer",) * len(tailp)
    ib = base(ins[0]["canonical"])
    if ib not in ARRAY_IN or not norm(ins[0]["canonical"]).endswith("*"):
        return None
    sc = set_reg_scope(name) if ib == "Set" else spanset_reg_scope(name) if ib == "SpanSet" else reg_scope(name)
    if sc is None:
        return None
    scope, accs = sc
    # Tcell<T> cell-id array (tquadbin_values/th3index_values): the canonical MobilityDB SQL return
    # is a <cell>set, which MobilityDuck surfaces as LIST(<cell>) — matching the hand contract and the
    # getValues(set)->LIST(base) / getValues(tbool)->LIST(boolean) precedent. Element is uint64_t
    # (CELL_UINT); the <cell>set sig does not end in "[]", so it must bypass the array-sig filter.
    if ec in CELL_UINT and scope == "types" and len(accs) == 1 and accs[0] in CELL_BASEVAL:
        return ("array", ec, ib, accs, tail)
    # The catalog sqlSignatures are the SoT for the type set, for a family-scoped accessor as much
    # as for a generic one: each signature is the CREATE FUNCTION MobilityDB actually declares, so a
    # family whose MEOS accessor is named for a supertype (tgeo_stboxes, declared for tcbuffer and
    # tpose besides the 4 geo types) registers exactly the types it serves. The name-derived scope
    # above stays the family gate (None = not a core family) and still selects the cell branch; it
    # does not narrow the type set, which would drop the declared types its prefix does not name.
    accs = array_declared_accs(f, tail)
    if not accs:
        return None
    return ("array", ec, ib, accs, tail)

def emit_array(f, ec, ib, tail=()):
    """The Gen_<name> body: marshal the container in, call the MEOS accessor with a local count,
    and build a DuckDB LIST from the returned <elem>* array (per-element marshal from ARRAY_ELEM).
    Reserve grows the child capacity to the running total; GetEntry is re-fetched after Reserve.
    A non-empty `tail` adds the split-into-N argument: a second INTEGER vector read per row and
    passed through to the accessor, NULL in either argument yielding a NULL list."""
    name = f["name"]
    cbase, blobto = ARRAY_IN[ib]
    _lt, ccpp, marsh = ARRAY_ELEM[ec]
    retc = norm(f["returnType"]["canonical"])          # e.g. "TimestampTz *", "text **", "Span *"
    n_vec = ("    auto &n_vec = args.data[1];\n"
             "    n_vec.Flatten(row_count);\n") if tail else ""
    n_null = " || n_vec.GetValue(i).IsNull()" if tail else ""
    n_read = "        int32_t n_arg = FlatVector::GetData<int32_t>(n_vec)[i];\n" if tail else ""
    call_args = "in, n_arg, &count" if tail else "in, &count"
    return (
f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
f"    EnsureMeosThreadInitialized();\n"
f"    auto &in_vec = args.data[0];\n"
f"    idx_t row_count = args.size();\n"
f"    in_vec.Flatten(row_count);\n"
f"{n_vec}"
f"    auto &result_validity = FlatVector::Validity(result);\n"
f"    auto list_entries = FlatVector::GetData<list_entry_t>(result);\n"
f"    idx_t off = 0;\n"
f"    for (idx_t i = 0; i < row_count; ++i) {{\n"
f"        if (in_vec.GetValue(i).IsNull(){n_null}) {{ result_validity.SetInvalid(i); continue; }}\n"
f"        string_t blob = FlatVector::GetData<string_t>(in_vec)[i];\n"
f"{n_read}"
f"        {cbase} *in = {blobto}(blob);\n"
f"        int count = 0;\n"
f"        {retc} arr = {name}({call_args});\n"
f"        free(in);\n"
f"        int n = (arr && count > 0) ? count : 0;\n"
f"        ListVector::Reserve(result, off + n);\n"
f"        ListVector::SetListSize(result, off + n);\n"
f"        list_entries[i] = list_entry_t{{off, (uint64_t) n}};\n"
f"        if (n > 0) {{\n"
f"            auto &child_vector = ListVector::GetEntry(result);\n"
f"            child_vector.SetVectorType(VectorType::FLAT_VECTOR);\n"
f"            auto *cd = FlatVector::GetData<{ccpp}>(child_vector);\n"
f"            for (int j = 0; j < n; ++j) {{ {marsh} }}\n"
f"            off += n;\n"
f"        }}\n"
f"        if (arr) free(arr);\n"
f"        result_validity.SetValid(i);\n"
f"    }}\n"
f"}}\n")

# Temporal + box (STBox/TBox) -> bool: the spatiotemporal/numeric topological predicates
# (contains/overlaps/contained/adjacent/same/left/right/... between a temporal and a box).
# Mixed-arg shape (one Temporal blob + one box blob); temporal scope from reg_scope
# (tspatial->geo for STBox, tnumber->{tint,tfloat} for TBox).
BOX_MARSH = {"STBox":  ("BlobToStbox",  "StboxType::stbox()"),
             "TBox":   ("BlobToTbox",   "TboxType::tbox()"),
             "TPCBox": ("BlobToTpcbox", "TpcboxType::tpcbox()")}
def shape_temporal_box(f):
    """Temporal + box -> a scalar. The topological predicates answer bool and the
    nearest approach answers a distance; both marshal the box the same way, so the
    return the MEOS function declares is carried through the shape rather than
    fixed to one of them. A tri-state ever/always int is NOT one of these — it is
    a bool the comparison shapes own, so is_pred_int keeps it out."""
    if supported(f) is not None: return None
    ins, out = classify(f)
    if out is not None or len(ins) != 2: return None
    rb = base(f["returnType"]["canonical"]); rn = norm(f["returnType"]["canonical"])
    if "*" in rn: return None
    if rb == "bool":
        ret = "bool"
    elif rb in SCALAR_RET_CPP and not is_pred_int(f):
        ret = rb
    else:
        return None
    # A distance answers the maximum of its own return type when there is no
    # nearest approach, and the SQL surface reports that as NULL, as the
    # PostgreSQL wrappers do. The guard is a pure function of the return type,
    # so it needs no per-function knowledge. The index path is a different
    # surface and keeps the maximum, which is what sorts an unreachable entry
    # last under an ORDER BY. Distance groups are named `_dist`; `_distance` is
    # the same family under the older spelling.
    if re.search(r"_dist(ance)?$", f.get("group") or ""):
        ret = "sentinel:" + ret
    b0 = base(ins[0]["canonical"]); n0 = norm(ins[0]["canonical"])
    b1 = base(ins[1]["canonical"]); n1 = norm(ins[1]["canonical"])
    if not (n0.endswith("*") and n1.endswith("*")): return None
    sc = reg_scope(f["name"])
    if sc is None or sc[0] != "types": return None
    if b0 == "Temporal" and b1 in BOX_MARSH: return (f"tb_r:{b1}:{ret}", sc[1])  # (Temporal, Box)
    if b1 == "Temporal" and b0 in BOX_MARSH: return (f"tb_l:{b0}:{ret}", sc[1])  # (Box, Temporal)
    return None

def emit_temporal_box(f, kind):
    name = f["name"]; parts = kind.split(":")
    side, box = parts[0], parts[1]
    sentinel = parts[2] == "sentinel"
    ret = parts[-1]
    blobto = BOX_MARSH[box][0]
    cpp = "bool" if ret == "bool" else SCALAR_RET_CPP[ret][0]
    call = f"{name}(t, bx)" if side == "tb_r" else f"{name}(bx, t)"
    marshal = (f"            Temporal *t = BlobToTemporal(a);\n            {box} *bx = {blobto}(b);\n"
               if side == "tb_r" else
               f"            {box} *bx = {blobto}(a);\n            Temporal *t = BlobToTemporal(b);\n")
    frees = "free(t); free(bx);" if side == "tb_r" else "free(bx); free(t);"
    if not sentinel:
        body = f"{marshal}            {cpp} r = {call};\n            {frees}\n            return r;"
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::Execute<string_t, string_t, {cpp}>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t a, string_t b) {{\n{body}\n        }});\n}}\n")
    # The maximum of the return type says there is no distance, which is NULL here.
    body = (f"{marshal}            {cpp} r = {call};\n            {frees}\n"
            f"            if (r == std::numeric_limits<{cpp}>::max()) {{ mask.SetInvalid(idx); return {cpp}(); }}\n"
            f"            return r;")
    return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    BinaryExecutor::ExecuteWithNulls<string_t, string_t, {cpp}>(args.data[0], args.data[1], result, args.size(),\n"
            f"        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> {cpp} {{\n{body}\n        }});\n}}\n")

# Temporal + span -> bool: topological predicates across the value (numspan) or time
# (tstzspan) dimension. numspan PAIRS to the tnumber's value type; tstzspan is fixed and
# applies to every temporal type. ALL_TEMPORAL_ACCS = the full temporal type set (core + all
# spatial subtypes via SPATIAL_ALLTYPES, so a new spatial family inherits the time-restriction
# surface atTime/minusTime/deleteTime over tstzspan/tstzset/tstzspanset).
NUMSPAN_PAIR = {"TemporalTypes::tint()": "SpanTypes::intspan()",
                "TemporalTypes::tfloat()": "SpanTypes::floatspan()"}
ALL_TEMPORAL_ACCS = (["TemporalTypes::tint()", "TemporalTypes::tbigint()", "TemporalTypes::tbool()",
                      "TemporalTypes::tfloat()", "TemporalTypes::ttext()"] + SPATIAL_ALLTYPES)
# A family whose VALUE LAYOUT makes the generic walker wrong owns its own temporal surface and
# must NOT take the blanket: trgeometry appends its reference geometry to the varlena, so a
# generic `temporal_*` op would drop it — which is why MEOS publishes a `trgeometry_*` counterpart
# for each. Such a family stays in SPATIAL_ALLTYPES (it IS spatial, so the `tspatial_*` surface —
# SRID/transform/setSRID and the box operators — reaches it) but is excluded from the generic
# Temporal<T> blanket loop, where it would ALSO collide with its own counterpart: DuckDB refuses
# the extension at load with "Failed to add new function overloads to function <name>".
OWNS_TEMPORAL_SURFACE = {"TrgeometryTypes::trgeometry()"}
GENERIC_BLANKET_SPATIAL = [t for t in SPATIAL_ALLTYPES if t not in OWNS_TEMPORAL_SURFACE]

def shape_temporal_span(f):
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
    # retk == "T": a restriction (atTime/minusTime etc.) that removes everything is a
    # NULL-safe MEOS outcome -> must map to SQL NULL via ExecuteWithNulls/TemporalToBlobN
    if retk == "T":
        ret_c, ret_s, rett = "Temporal *r = ", "return TemporalToBlobN(result, r, mask, idx);", "string_t"
        lam_args = "string_t a, string_t b, ValidityMask &mask, idx_t idx) -> string_t"
        executor = "ExecuteWithNulls"
    else:
        ret_c, ret_s, rett = "bool r = ", "return r;", "bool"
        lam_args = "string_t a, string_t b)"
        executor = "Execute"
    if side == "ts_r":   # (Temporal, Container)
        body = (f"            Temporal *t = BlobToTemporal(a);\n            {cont} *cc = {blob}(b);\n"
                f"            {ret_c}{name}(t, cc);\n            free(t); free(cc);\n            {ret_s}")
    else:                # (Container, Temporal)
        body = (f"            {cont} *cc = {blob}(a);\n            Temporal *t = BlobToTemporal(b);\n"
                f"            {ret_c}{name}(cc, t);\n            free(cc); free(t);\n            {ret_s}")
    return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    BinaryExecutor::{executor}<string_t, string_t, {rett}>(args.data[0], args.data[1], result, args.size(),\n"
            f"        [&]({lam_args} {{\n{body}\n        }});\n}}\n")

# ---- Temporal -> container conversion (timeSpan/valueSpan/tbox), sqlSignatures-DRIVEN ----
# A unary `Temporal -> Span/SpanSet/TBox/STBox` cast (timeSpan=temporal_to_tstzspan,
# valueSpan=tnumber_to_span, tbox=tnumber_to_tbox). Registration is a PURE PROJECTION of the
# catalog sqlSignatures — each overload's (temporal arg type -> container ret type) is read
# straight from the catalog (mechanical, zero heuristic, no `flav`). The container return
# marshals via PTR_RET (SpanToBlob/TboxToBlob/...), already present.
SQL_CONTAINER_ACC = {
    "tstzspan": "SpanTypes::tstzspan()", "intspan": "SpanTypes::intspan()",
    "bigintspan": "SpanTypes::bigintspan()", "floatspan": "SpanTypes::floatspan()",
    "datespan": "SpanTypes::datespan()",
    "tstzspanset": "SpansetTypes::tstzspanset()", "intspanset": "SpansetTypes::intspanset()",
    "bigintspanset": "SpansetTypes::bigintspanset()", "floatspanset": "SpansetTypes::floatspanset()",
    "datespanset": "SpansetTypes::datespanset()",
    "tbox": "TboxType::tbox()", "stbox": "StboxType::stbox()",
    "tpcbox": "TpcboxType::tpcbox()",
}
def shape_temporal_to_container(f):
    """(Temporal) -> Span/SpanSet/TBox/STBox conversion, registered per catalog sqlSignature.
    Returns (Cbase, [(arg_acc, ret_acc)...]) over the overloads whose BOTH types are registered,
    or None (no sqlSignatures / unmappable / not this shape) -> the fn is left to the hand layer."""
    if supported(f) is not None:
        return None
    ins, out = classify(f)
    if out is not None or len(ins) != 1:
        return None
    if base(ins[0]["canonical"]) != "Temporal" or not norm(ins[0]["canonical"]).endswith("*"):
        return None
    rb = base(f["returnType"]["canonical"]); rn = norm(f["returnType"]["canonical"])
    if rb not in ("Span", "SpanSet", "TBox", "STBox") or not rn.endswith("*"):
        return None
    pairs = []
    for s in (f.get("sqlSignatures") or []):
        if len(s["args"]) != 1:
            continue
        aacc = SIG_TEMPORAL_ACC.get(s["args"][0]); racc = SQL_CONTAINER_ACC.get(s.get("ret"))
        if aacc and racc and (aacc, racc) not in pairs:
            pairs.append((aacc, racc))
    return (rb, pairs) if pairs else None

def emit_temporal_to_container(f, rb):
    name = f["name"]; toblob = PTR_RET[rb][1] % "r"      # 'SpanToBlob(result, r)'
    return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(args.data[0], result, args.size(),\n"
            f"        [&](string_t in, ValidityMask &mask, idx_t idx) -> string_t {{\n"
            f"            Temporal *t = BlobToTemporal(in);\n            {rb} *r = {name}(t);\n            free(t);\n"
            f"            if (!r) {{ mask.SetInvalid(idx); return string_t(); }}\n"
            f"            return {toblob};\n        }});\n}}\n")

# Temporal x finite-subset-of-domain -> Temporal restriction, driven by the catalog sqlSignatures
# pairings (heuristic-free, no flav). Covers the two-operand value restrictions whose second operand
# is a finite-subset representation of the value RANGE: atValues/minusValues (Set-of-T) and
# atTbox/minusTbox (TBox = the joint value x time box for numbers). Each generic MEOS fn
# (temporal_at_values, tnumber_at_tbox, ...) carries explicit [temporal-type, container-type]
# overloads; register one per pairing whose BOTH accessors are registered (cbufferset/geomset are
# skipped until their Duck set type lands). Complements shape_temporal_span, which owns the tstz time
# restrictions (atTime) and the numspan value restrictions via the name heuristic; this handles what
# it does not. The 3-operand box restrictions (atStbox with a border bool) are a separate shape.
# Marshalling map extends CONT_BLOB with the box types (BlobToTbox/BlobToStbox already emitted).
RESTRICT_CONT_BLOB = {**CONT_BLOB, "TBox": "BlobToTbox", "STBox": "BlobToStbox",
                      "TPCBox": "BlobToTpcbox"}
FINITE_SUBSET_ACC = {**SET_TYPES, **SQL_CONTAINER_ACC}
def shape_temporal_restrict_sig(f):
    if supported(f) is not None: return None
    ins, out = classify(f)
    if out is not None or len(ins) != 2: return None
    if base(ins[0]["canonical"]) != "Temporal" or not norm(ins[0]["canonical"]).endswith("*"): return None
    cont = base(ins[1]["canonical"])
    if cont not in RESTRICT_CONT_BLOB or not norm(ins[1]["canonical"]).endswith("*"): return None
    rb = base(f["returnType"]["canonical"]); rn = norm(f["returnType"]["canonical"])
    if rb != "Temporal" or not rn.endswith("*"): return None
    pairs = []
    for s in (f.get("sqlSignatures") or []):
        if len(s.get("args", [])) != 2: continue
        aacc = SIG_TEMPORAL_ACC.get(s["args"][0]); cacc = FINITE_SUBSET_ACC.get(s["args"][1])
        if aacc and cacc and (aacc, cacc) not in pairs: pairs.append((aacc, cacc))
    return (cont, pairs) if pairs else None

def emit_temporal_restrict_sig(f, cont):
    name = f["name"]; blob = RESTRICT_CONT_BLOB[cont]   # NULL-safe: a restriction that removes all -> SQL NULL
    return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(args.data[0], args.data[1], result, args.size(),\n"
            f"        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> string_t {{\n"
            f"            Temporal *t = BlobToTemporal(a);\n            {cont} *cc = {blob}(b);\n"
            f"            Temporal *r = {name}(t, cc);\n            free(t); free(cc);\n"
            f"            return TemporalToBlobN(result, r, mask, idx);\n"
            f"        }});\n}}\n")

def shape_scalar_first(f):
    """(by-value scalar, Temporal) — the mirror of shape_binary. Covers the scalar-first
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
# machinery (shape_coll/emit_coll) via a descriptor, so SpanSet is added without duplication.
SPANSET_TYPES = {
    "intspanset": "SpansetTypes::intspanset()", "bigintspanset": "SpansetTypes::bigintspanset()",
    "floatspanset": "SpansetTypes::floatspanset()",
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
    m = re.search(r'_to_(intspanset|bigintspanset|floatspanset|datespanset|tstzspanset)$', name)
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
TPCBOX_C = dict(cbase="TPCBox", blobto="BlobToTpcbox", toblob="TpcboxToBlob",
                elem={}, scope=None, ret=lambda n, a: a, single="TpcboxType::tpcbox()")
def shape_span(f, C=SPAN_C):
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
        if C["scope"] is None or C["scope"](f["name"]) is None:
            # Boxes (STBOX_C/TBOX_C, scope=None) have no name-scoped accessor map, so a bare
            # relax here would also pull in the whole box scalar-accessor group (hasX/hasT/
            # volume/...), which is a SEPARATE hand-registered surface (own generate-then-retire
            # wave). Carve out ONLY the bare unary `hash` -> uint32 accessor by sqlfn, gated on
            # C.get("single") so this never fires for Span/SpanSet (which reach hash via their
            # own scope above).
            if (C.get("single") and f.get("sqlfn") == "hash"
                    and rb in BYVAL_RET and "*" not in rn):
                return ("u_scalar:" + rb, byval_ret_duck(rb))
            return None
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
    # (X, by-value uint64 seed) -> uint64 hash (span_hash_extended/spanset_hash_extended);
    # scope-gated like u2iv. Boxes (STBOX_C/TBOX_C, scope=None) carve out ONLY hashExtended
    # by sqlfn below (mirrors the unary hash carve-out above) so the rest of the box
    # scalar-accessor group stays out of scope.
    if (contp(ins[0]) and base(ins[1]["canonical"]) in SCALAR_ARG
            and "*" not in norm(ins[1]["canonical"]) and rb == "uint64_t" and "*" not in rn
            and C["scope"] is not None and C["scope"](f["name"]) is not None):
        return ("bsc:" + base(ins[1]["canonical"]), "LogicalType::UBIGINT")
    if (contp(ins[0]) and base(ins[1]["canonical"]) in SCALAR_ARG
            and "*" not in norm(ins[1]["canonical"]) and rb == "uint64_t" and "*" not in rn
            and C.get("single") and f.get("sqlfn") == "hashExtended"):
        return ("bsc:" + base(ins[1]["canonical"]), "LogicalType::UBIGINT")
    # (X, scalar PARAM) -> X : a same-container return whose scalar is NOT an element
    # (floatspan_round/floatspanset_round's precision integer). Name-scoped (<elem>span_round
    # -> that container type), so the round=float-only base-value scoping falls out.
    if (contp(ins[0]) and base(ins[1]["canonical"]) in SCALAR_ARG
            and "*" not in norm(ins[1]["canonical"]) and rb == cb and rn.endswith("*")
            and C["scope"] is not None and C["scope"](f["name"]) is not None):
        return ("csc:" + base(ins[1]["canonical"]), "type")
    # mixed container (Span, SpanSet) / (SpanSet, Span) -> bool : the span<->spanset
    # positional operators (left/right/overleft/overright span_spanset|spanset_span).
    # A 1-D span and its spanset order on the same axis, but the two operands are
    # DIFFERENT containers, so the same-base (X,X) case below skips them. Marshal each
    # operand as its own container; drive the concrete type pairs from sqlSignatures.
    # Detect once under Span (cbase=="Span") so it is emitted exactly once, not per-C.
    if (cb == "Span" and rb == "bool" and "*" not in rn
            and norm(ins[0]["canonical"]).endswith("*") and norm(ins[1]["canonical"]).endswith("*")):
        b0, b1 = base(ins[0]["canonical"]), base(ins[1]["canonical"])
        if {b0, b1} == {"Span", "SpanSet"}:
            return ("b_mix:%s_%s" % (b0.lower(), b1.lower()), "LogicalType::BOOLEAN")
    # generic (X,X) -> bool|X
    if not (contp(ins[0]) and contp(ins[1])): return None
    if rb == cb and rn.endswith("*"):  return ("b_span", "type")
    if rb == "bool" and "*" not in rn:     return ("b_bool", "LogicalType::BOOLEAN")
    return None

def emit_span(f, kind, C=SPAN_C):
    name = f["name"]; cb, bt, tb = C["cbase"], C["blobto"], C["toblob"]
    if kind.startswith("b_mix:"):   # (Span,SpanSet)/(SpanSet,Span) -> bool
        t0, t1 = kind.split(':', 1)[1].split('_')     # "span"/"spanset" in operand order
        BT = {"span": "BlobToSpan", "spanset": "BlobToSpanSet"}
        CT = {"span": "Span", "spanset": "SpanSet"}
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::Execute<string_t, string_t, bool>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t a, string_t b) {{\n"
                f"            {CT[t0]} *x = {BT[t0]}(a);\n            {CT[t1]} *y = {BT[t1]}(b);\n"
                f"            bool r = {name}(x, y);\n            free(x); free(y);\n"
                f"            return r;\n        }});\n}}\n")
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
    if kind.startswith("bsc:"):     # (X, by-value uint64 seed) -> uint64 hash (X_hash_extended)
        _dt, cpp2, marsh = SCALAR_ARG[kind.split(':')[1]]
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::Execute<string_t, {cpp2}, uint64_t>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t in, {cpp2} a2) {{\n"
                f"            {cb} *s = {bt}(in);\n            uint64_t r = {name}(s, {marsh});\n            free(s);\n"
                f"            return r;\n        }});\n}}\n")
    if kind.startswith("csc:"):     # (X, by-value scalar param) -> X (round(floatspan, integer))
        _dt, cpp2, marsh = SCALAR_ARG[kind.split(':')[1]]
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::ExecuteWithNulls<string_t, {cpp2}, string_t>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t in, {cpp2} a2, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            {cb} *s = {bt}(in);\n            {cb} *r = {name}(s, {marsh});\n            free(s);\n"
                f"            if (!r) {{ mask.SetInvalid(idx); return string_t(); }}\n"
                f"            return {tb}(result, r);\n        }});\n}}\n")
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
                  "meos_geo_bbox_topo",
                  "meos_temporal_math", "meos_temporal_comp_ever",
                  # Temporal conversions: the tnumber temporal->temporal casts (tint/tbigint/
                  # tfloat) come from shape_emittable; timeSpan/valueSpan/tbox come from
                  # shape_temporal_to_container (sqlSignatures-driven). Retire the group as one
                  # wave once every @sqlfn is generated.
                  "meos_temporal_conversion",
                  # The temporal value-comparison surface (eEq/aEq/eNe/aNe + tEq/tNe)
                  # for every spatial family is generated from the same comp shape as the
                  # base temporal_comp_* groups (geo×T, T×geo literal regs + the T×T
                  # AllTypes loop that includes tgeometry/tgeography). Retire the whole
                  # surface as one wave so the safety ledger fails the build if a future
                  # catalog change ever drops a family's comparison coverage.
                  "meos_geo_comp_ever", "meos_geo_comp_temp",
                  "meos_cbuffer_comp_ever", "meos_cbuffer_comp_temp",
                  "meos_h3_comp_ever", "meos_h3_comp_temp",
                  "meos_npoint_comp_ever", "meos_npoint_comp_temp",
                  "meos_pose_comp_ever", "meos_pose_comp_temp",
                  "meos_rgeo_comp_ever", "meos_rgeo_comp_temp",
                  "meos_json_comp_ever", "meos_json_comp_temp",
                  # Set/span/spanset relative-position operators (left/right/before/after +
                  # over*, value and time axes) — generated from the span/set shapes incl the
                  # mixed span<->spanset case; the hand span_left/set_left + operator regs are
                  # deleted in the same wave.
                  "meos_setspan_pos",
                  # Set/span/spanset topological operators (contains/contained/overlaps/
                  # adjacent + @>/<@/&&/-|-) — bare names + operators from the span/set shapes,
                  # both argument orders incl the symmetric adjacent(value, span); the hand
                  # set_contains/span_contains snake + operator regs are deleted.
                  "meos_setspan_topo",
                  # Temporal bounding-box topological operators (contains/contained/
                  # overlaps/same/adjacent + @>/<@/&&/~=/-|-) for temporal × temporal,
                  # temporal × tstzspan, and tnumber × {numspan, tbox} — generated from
                  # the box/span shapes; the hand temporal_* snake aliases and the mixed
                  # cross-product operator regs in temporal.cpp are deleted.
                  "meos_temporal_bbox_topo"}
# @sqlfn names in a RETIRED group that the generator legitimately does NOT emit and that the
# hand keeps on purpose (a documented generator-shape gap, NOT a silent drop). Anything else
# uncovered in a retired group is a build-FATAL retire-safety error (see the validation below).
# The NxN *Pairs relations (ever/always int*->LIST(STRUCT(i,j)); temporal + SpanSet*** periods)
# are now GENERATED as scalar LIST(STRUCT) UDFs via shape_tgeoarr/emit_pairs_scalar (the 'pairs'
# form), over exactly the catalog sqlSignature geo types, so they are covered by construction —
# no longer listed here. Empty: every @sqlfn of a retired group is generated.
RETIRE_UNCOVERED_OK = set()
def retired(f):
    return (f.get("group") or "") in RETIRED_GROUPS
def reg_name(nm, f):
    return nm            # canonical always; the g_ coexistence prefix is removed for good
def reg_names(f, sqlfn, aliases, argsig=None):
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
    # The operator symbol is registered only when DuckDB can parse it in an operator
    # position. Two things put it out of reach:
    #   - a name containing '#', which the lexer rejects in any operator position;
    #   - an arity past 2, since operator syntax is unary or binary. MobilityDB binds
    #     such an operator to its OWN 2-argument wrapper (`->` is `tjsonbObjectFieldOpr`,
    #     not the 3-argument `tjsonbObjectField`), so registering the symbol on the wider
    #     function would publish a name no operator syntax reaches.
    # Either way the function stays reachable through its bare name.
    wide = argsig is not None and len([x for x in argsig.strip("{}").split(",") if x.strip()]) > 2
    if op and "#" not in op and not wide and op not in names:
        names.append(op)
    return names

# Output organization mirrors the canonical generator family (JMEOS FunctionsGenerator /
# Spark codegen_spark_udfs.py): every emitted body + registration is bucketed by its
# doxygen @ingroup GROUP, so a function lands in the same place across tools and the
# surface is browseable by the MEOS reference-manual structure. GReg captures the group
# of every appended line WITHOUT touching the 20 per-shape append call-sites — STATE["grp"]
# is set once per emitted function at the top of each emission loop.
STATE = {"grp": "meos_ungrouped"}

# How many translation units the UDF surface is emitted as. Fixed rather than derived so the
# file list in CMakeLists.txt is stable across regenerations; raising it costs nothing but a
# CMake line, and past the core count of the build machine it buys nothing.
GENERATED_CHUNKS = 8
# The whole emitted surface, for the post-generation invariants -- which read what was WRITTEN
# rather than what was intended, and so have to see every unit.
GENERATED_TEXT = []
class GReg:
    """A list whose .append() also records the current @ingroup group (STATE['grp'])."""
    def __init__(self): self.items = []          # list of (group, line)
    def append(self, line): self.items.append((STATE["grp"], line))
    def by_group(self):
        d = defaultdict(list)
        for g, line in self.items: d[g].append(line)
        return d
    def __len__(self): return len(self.items)

# ---- Temporal constructor / transform NAME FAMILY (per-sqlName over the core types) ----
# One base-type-generic MEOS wrapper backs a per-type SQL name FAMILY: temporal_as_tinstant is
# exposed as tintInst/tbigintInst/.../ttextInst, tsequence_make as tintSeq/..., etc. The catalog
# sqlSignatures carry each overload's own `sqlName`, so the generator registers every core-type name
# from the catalog rather than the single representative. Scoped to the 5 CORE
# types (TemporalTypes::AllTypes) — the geo/cbuffer constructors live in their own family files, so
# emitting them here would double-register. This self-contained path (like the NxN `ga` path) REPLACES
# the hand loop in src/temporal/temporal.cpp; the hand loop is deleted in the same wave to avoid a
# bare-name collision. tsequenceset_make_gaps is NOT included yet (needs DuckDB->MEOS Interval
# marshalling; absent from the hand loop today, so deferring it is a pure follow-on, no regression).
CORE_CTOR_ACC = {
    "tint": "TemporalTypes::tint()", "tbigint": "TemporalTypes::tbigint()",
    "tbool": "TemporalTypes::tbool()", "tfloat": "TemporalTypes::tfloat()",
    "ttext": "TemporalTypes::ttext()",
}
TEMPORAL_CTOR = {
    "temporal_as_tinstant":     "to_inst",     # <t>Inst(<t>)
    "temporal_as_tsequence":    "to_seq",      # <t>Seq(<t>[, text interp])
    "temporal_as_tsequenceset": "to_seqset",   # <t>SeqSet(<t>[, text interp])
    "tsequence_make":           "make_seq",    # <t>Seq(<t>[][, text, bool, bool])
    "tsequenceset_make":        "make_seqset", # <t>SeqSet(<t>[])
    "tsequenceset_make_gaps":   "make_gaps",   # <t>SeqSetGaps(<t>[][, interval maxt, float maxdist, text])
    "temporal_from_hexwkb":     "from_hexwkb", # <t>FromHexWKB(text)
    "temporal_from_mfjson":     "from_mfjson", # <t>FromMFJSON(text)
}
def shape_temporal_ctor(f):
    return TEMPORAL_CTOR.get(f["name"])

def ctor_arg_slot(a, acc):
    """A catalog SQL arg type -> the DuckDB LogicalType slot for a constructor overload."""
    if a.endswith("[]"): return f"LogicalType::LIST({acc})"
    if a == "text":      return "LogicalType::VARCHAR"
    if a == "boolean":   return "LogicalType::BOOLEAN"
    if a == "interval":  return "LogicalType::INTERVAL"
    if a == "float":     return "LogicalType::DOUBLE"
    return acc                                  # the temporal operand itself

# The Gen_<fn> body per kind, transcribed from the proven hand impls
# (src/temporal/temporal_functions.cpp: Temporal_to_t*, Tsequence(set)_constructor). The transforms
# read an optional VARCHAR interp from data[1]; the array makes marshal the LIST child into a fresh
# Temporal** (ListToTemporalArr), reinterpret to TInstant**/TSequence**, call the MEOS kernel, and
# free. temptype (for the array interp default) comes from the registered result type's alias.
_CTOR_BODY = {
  "to_inst":
    "static void Gen_{fn}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
    "    EnsureMeosThreadInitialized();\n"
    "    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(args.data[0], result, args.size(),\n"
    "        [&](string_t in, ValidityMask &mask, idx_t idx) -> string_t {{\n"
    "            Temporal *t = BlobToTemporal(in);\n"
    "            Temporal *r = (Temporal *) temporal_as_tinstant(t);\n"
    "            free(t);\n"
    "            return TemporalToBlobN(result, r, mask, idx);\n"
    "        }});\n"
    "}}\n",
  "to_seq":     None,   # filled below (shares the transform template)
  "to_seqset":  None,
  "make_seq":
    "static void Gen_{fn}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
    "    EnsureMeosThreadInitialized();\n"
    "    auto rc = args.size();\n"
    "    auto &arr = args.data[0]; arr.Flatten(rc);\n"
    "    auto &child = ListVector::GetEntry(arr);\n"
    "    child.Flatten(ListVector::GetListSize(arr));\n"
    "    MeosType temptype = TemporalHelpers::GetTemptypeFromAlias(result.GetType().GetAlias().c_str());\n"
    "    interpType interp = temptype_supports_linear(temptype) ? LINEAR : STEP;\n"
    "    bool lower_inc = true, upper_inc = true;\n"
    "    if (args.ColumnCount() > 1) {{ auto &c = args.data[1]; c.Flatten(rc); Value v = c.GetValue(0); if (!v.IsNull()) interp = interptype_from_string(v.ToString().c_str()); }}\n"
    "    if (args.ColumnCount() > 2) {{ auto &c = args.data[2]; c.Flatten(rc); Value v = c.GetValue(0); if (!v.IsNull()) lower_inc = v.GetValue<bool>(); }}\n"
    "    if (args.ColumnCount() > 3) {{ auto &c = args.data[3]; c.Flatten(rc); Value v = c.GetValue(0); if (!v.IsNull()) upper_inc = v.GetValue<bool>(); }}\n"
    "    UnaryExecutor::ExecuteWithNulls<list_entry_t, string_t>(arr, result, rc,\n"
    "        [&](list_entry_t le, ValidityMask &mask, idx_t idx) -> string_t {{\n"
    "            int n = 0;\n"
    "            const Temporal **a = ListToTemporalArr(child, le, &n);\n"
    "            TSequence *seq = tsequence_make((TInstant **) a, n, lower_inc, upper_inc, interp, true);\n"
    "            string_t out = TemporalToBlobN(result, (Temporal *) seq, mask, idx);\n"
    "            FreeTemporalArr(a, n);\n"
    "            return out;\n"
    "        }});\n"
    "}}\n",
  "make_seqset":
    "static void Gen_{fn}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
    "    EnsureMeosThreadInitialized();\n"
    "    auto rc = args.size();\n"
    "    auto &arr = args.data[0]; arr.Flatten(rc);\n"
    "    auto &child = ListVector::GetEntry(arr);\n"
    "    child.Flatten(ListVector::GetListSize(arr));\n"
    "    UnaryExecutor::ExecuteWithNulls<list_entry_t, string_t>(arr, result, rc,\n"
    "        [&](list_entry_t le, ValidityMask &mask, idx_t idx) -> string_t {{\n"
    "            int n = 0;\n"
    "            const Temporal **a = ListToTemporalArr(child, le, &n);\n"
    "            TSequenceSet *ss = tsequenceset_make((TSequence **) a, n, true);\n"
    "            string_t out = TemporalToBlobN(result, (Temporal *) ss, mask, idx);\n"
    "            FreeTemporalArr(a, n);\n"
    "            return out;\n"
    "        }});\n"
    "}}\n",
  # <t>SeqSetGaps(<t>[][, interval maxt, float maxdist, text interp]): split the array of instants
  # into sequences at temporal gaps > maxt and/or value gaps > maxdist. Optional args mirror the PG
  # wrapper defaults (maxdist -1.0 = ignore value gaps, maxt NULL = ignore time gaps, interp default
  # from temptype); the per-type arg subset (bool/text: only maxt; float: adds a text interp) comes
  # from the catalog signatures, so the same body serves every callable arity.
  "make_gaps":
    "static void Gen_{fn}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
    "    EnsureMeosThreadInitialized();\n"
    "    auto rc = args.size();\n"
    "    auto &arr = args.data[0]; arr.Flatten(rc);\n"
    "    auto &child = ListVector::GetEntry(arr);\n"
    "    child.Flatten(ListVector::GetListSize(arr));\n"
    "    MeosType temptype = TemporalHelpers::GetTemptypeFromAlias(result.GetType().GetAlias().c_str());\n"
    "    interpType interp = temptype_supports_linear(temptype) ? LINEAR : STEP;\n"
    "    MeosInterval maxt_val; bool has_maxt = false; double maxdist = -1.0;\n"
    "    if (args.ColumnCount() > 1) {{ auto &c = args.data[1]; c.Flatten(rc); Value v = c.GetValue(0); if (!v.IsNull()) {{ maxt_val = IntervaltToInterval(v.GetValue<interval_t>()); has_maxt = true; }} }}\n"
    "    if (args.ColumnCount() > 2) {{ auto &c = args.data[2]; c.Flatten(rc); Value v = c.GetValue(0); if (!v.IsNull()) maxdist = v.GetValue<double>(); }}\n"
    "    if (args.ColumnCount() > 3) {{ auto &c = args.data[3]; c.Flatten(rc); Value v = c.GetValue(0); if (!v.IsNull()) interp = interptype_from_string(v.ToString().c_str()); }}\n"
    "    const MeosInterval *maxt = has_maxt ? &maxt_val : NULL;\n"
    "    UnaryExecutor::ExecuteWithNulls<list_entry_t, string_t>(arr, result, rc,\n"
    "        [&](list_entry_t le, ValidityMask &mask, idx_t idx) -> string_t {{\n"
    "            int n = 0;\n"
    "            const Temporal **a = ListToTemporalArr(child, le, &n);\n"
    "            TSequenceSet *ss = tsequenceset_make_gaps((TInstant **) a, n, interp, maxt, maxdist);\n"
    "            string_t out = TemporalToBlobN(result, (Temporal *) ss, mask, idx);\n"
    "            FreeTemporalArr(a, n);\n"
    "            return out;\n"
    "        }});\n"
    "}}\n",
  # <t>FromHexWKB(text): parse a hex-encoded WKB string into a temporal. The WKB is
  # self-describing, so the kernel needs no temptype.
  "from_hexwkb":
    "static void Gen_{fn}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
    "    EnsureMeosThreadInitialized();\n"
    "    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(args.data[0], result, args.size(),\n"
    "        [&](string_t in, ValidityMask &mask, idx_t idx) -> string_t {{\n"
    "            Temporal *r = temporal_from_hexwkb(in.GetString().c_str());\n"
    "            return TemporalToBlobN(result, r, mask, idx);\n"
    "        }});\n"
    "}}\n",
  # <t>FromMFJSON(text): parse a MF-JSON string into a temporal of the registered result type.
  "from_mfjson":
    "static void Gen_{fn}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
    "    EnsureMeosThreadInitialized();\n"
    "    MeosType temptype = TemporalHelpers::GetTemptypeFromAlias(result.GetType().GetAlias().c_str());\n"
    "    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(args.data[0], result, args.size(),\n"
    "        [&](string_t in, ValidityMask &mask, idx_t idx) -> string_t {{\n"
    "            Temporal *r = temporal_from_mfjson(in.GetString().c_str(), temptype);\n"
    "            return TemporalToBlobN(result, r, mask, idx);\n"
    "        }});\n"
    "}}\n",
}
_CTOR_TRANSFORM = (
    "static void Gen_{fn}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
    "    EnsureMeosThreadInitialized();\n"
    "    interpType interp = INTERP_NONE;\n"
    "    if (args.ColumnCount() > 1) {{ auto &c = args.data[1]; c.Flatten(args.size()); Value v = c.GetValue(0); if (!v.IsNull()) interp = interptype_from_string(v.ToString().c_str()); }}\n"
    "    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(args.data[0], result, args.size(),\n"
    "        [&](string_t in, ValidityMask &mask, idx_t idx) -> string_t {{\n"
    "            Temporal *t = BlobToTemporal(in);\n"
    "            Temporal *r = (Temporal *) {meos}(t, interp);\n"
    "            free(t);\n"
    "            return TemporalToBlobN(result, r, mask, idx);\n"
    "        }});\n"
    "}}\n")
def emit_temporal_ctor(f, kind):
    fn = f["name"]
    if kind == "to_seq":
        return _CTOR_TRANSFORM.format(fn=fn, meos="temporal_as_tsequence")
    if kind == "to_seqset":
        return _CTOR_TRANSFORM.format(fn=fn, meos="temporal_as_tsequenceset")
    return _CTOR_BODY[kind].format(fn=fn)

# ---- Set/Span/SpanSet FromHexWKB parsers (per-element-type NAME FAMILY) ----
# The base-type-generic MEOS wrappers set_from_hexwkb/span_from_hexwkb/spanset_from_hexwkb back a
# per-element-type SQL name family (intsetFromHexWKB, intspanFromHexWKB, ...). Same shape as the
# temporal from_hexwkb: a NUL-terminated hex string parses into the container, marshalled to a BLOB.
# The WKB is self-describing, so the kernel needs no element type. Core element types only; the
# spatial set elements (geo/cbuffer/npoint/...) are registered in their own family files once their
# Duck set type lands.
CONTAINER_FROM_HEXWKB = {
    "set_from_hexwkb":     "Set",
    "span_from_hexwkb":    "Span",
    "spanset_from_hexwkb": "SpanSet",
}
CONTAINER_FROMHEX_ACC = {**SET_TYPES,
                         **{k: v for k, v in SQL_CONTAINER_ACC.items() if k not in ("tbox", "stbox")}}
_CFH_SERIALIZE = {
    "Set":     "            return SetToBlobN(result, r, mask, idx);\n",
    "Span":    "            if (!r) { mask.SetInvalid(idx); return string_t(); }\n"
               "            return SpanToBlob(result, r);\n",
    "SpanSet": "            if (!r) { mask.SetInvalid(idx); return string_t(); }\n"
               "            return SpanSetToBlob(result, r);\n",
}
def emit_container_from_hexwkb(f, cont):
    fn = f["name"]
    return ("static void Gen_{fn}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            "    EnsureMeosThreadInitialized();\n"
            "    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(args.data[0], result, args.size(),\n"
            "        [&](string_t in, ValidityMask &mask, idx_t idx) -> string_t {{\n"
            "            {cont} *r = {fn}(in.GetString().c_str());\n"
            "{ser}"
            "        }});\n"
            "}}\n").format(fn=fn, cont=cont, ser=_CFH_SERIALIZE[cont])

# ---- asHexWKB serializer (Temporal<T> INHERITED output surface) ----
# The mirror of the FromHexWKB input family: one base-type-generic MEOS wrapper
# (temporal_as_hexwkb) backs the SQL asHexWKB over EVERY temporal subtype via late binding — the
# Temporal<T> "Input and Output" <sect1> (023_temporal_inout.in.sql / geo/053_tpoint_inout.in.sql /
# tools/codegen/inherited/INHERITANCE_MAP.md §4). Canonical surface: a single name with an optional
# `endianenconding text DEFAULT ''` arg; asHexWKB is the BASE hex-WKB — Temporal_as_hexwkb calls
# Datum_as_hexwkb(extended=false) (mobilitydb type_out.c:296), variant = endian only, no SRID flag.
# (The extended asHexEWKB is a SEPARATE spatial wrapper Tspatial_as_hexewkb — its own follow-on.)
# This REPLACES the per-family hand asHexWKB (the geo files register it by hand) with the ONE
# inherited generation, and ADDS the base temporals (tint/tbigint/tfloat/tbool/ttext) that had no
# asHexWKB at all. The registered type set is the catalog sqlSignatures (sqlName == asHexWKB)
# intersected with the types the binding has (SIG_TEMPORAL_ACC) — subtypes whose Duck type is not
# yet wired (tjsonb/th3index/tnpoint/tpc*) auto-drop and land in their own family file later. The
# bare canonical name collides with the retired hand asHexWKB (deleted in the same wave).
OUTPUT_HEXWKB = {
    "temporal_as_hexwkb": ("Temporal", "BlobToTemporal"),
}
OUTPUT_HEXWKB_ACC = {**SIG_TEMPORAL_ACC}
def emit_output_hexwkb(f, cls, blobto):
    fn = f["name"]
    # asHexWKB is the base variant; the optional endian text selects NDR/XDR/machine
    # (wkb_variant_from_endian("") == 0). One Gen body serves both the 1-arg and 2-arg overloads.
    return ("static void Gen_{fn}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            "    EnsureMeosThreadInitialized();\n"
            "    if (args.ColumnCount() > 1) {{\n"
            "        BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(args.data[0], args.data[1], result, args.size(),\n"
            "            [&](string_t in, string_t endian, ValidityMask &mask, idx_t idx) -> string_t {{\n"
            "                {cls} *x = {blobto}(in);\n"
            "                uint8_t variant = wkb_variant_from_endian(endian.GetString().c_str());\n"
            "                size_t sz = 0;\n"
            "                char *hex = {fn}(x, variant, &sz);\n"
            "                free(x);\n"
            "                if (!hex) {{ mask.SetInvalid(idx); return string_t(); }}\n"
            "                string_t out = StringVector::AddString(result, hex);\n"
            "                free(hex);\n"
            "                return out;\n"
            "            }});\n"
            "    }} else {{\n"
            "        UnaryExecutor::ExecuteWithNulls<string_t, string_t>(args.data[0], result, args.size(),\n"
            "            [&](string_t in, ValidityMask &mask, idx_t idx) -> string_t {{\n"
            "                {cls} *x = {blobto}(in);\n"
            "                size_t sz = 0;\n"
            "                char *hex = {fn}(x, (uint8_t) 0, &sz);\n"
            "                free(x);\n"
            "                if (!hex) {{ mask.SetInvalid(idx); return string_t(); }}\n"
            "                string_t out = StringVector::AddString(result, hex);\n"
            "                free(hex);\n"
            "                return out;\n"
            "            }});\n"
            "    }}\n"
            "}}\n").format(fn=fn, cls=cls, blobto=blobto)

def shape_h3_prefilter(f):
    """The static H3 cell shapes the generic paths miss (all family=H3; the Set
    operand/return is an h3indexset, registered as its own BLOB alias):
      - geoToH3IndexSet  (GSERIALIZED*, int) -> Set*   : geometry->cells, marshalled @ 4326
        (h3 is geographic). The generic arg_type has no GSERIALIZED marshaller, and the
        Temporal-sibling geo path (shape_geo_temporal) sources the SRID from a temporal arg
        this function lacks, so a dedicated fixed-SRID emit is needed.
      - geoToH3Cell  (GSERIALIZED*, int) -> uint64_t   : geometry->single cell, same GSERIALIZED
        marshalling as geoToH3IndexSet but a scalar h3index (BIGINT-backed) return instead of
        a Set.
      - eEq(h3indexset,th3index)  (Set*, Temporal*) -> int  : the trip-in-cells prefilter,
        an `int` tri-state (ret<0 -> SQL NULL) that shape_set does not match (it keys on
        rb==bool and (Set,scalar)/(Set,Set), not (Set,Temporal)).
      - eEq/eNe/aEq/aNe(h3index,th3index) and the (th3index,h3index) mirror  (uint64_t,
        Temporal*) -> int tri-state : the bare-cell-vs-temporal-cell comparison family.
        arg_type has no uint64_t scalar mapping (it is not a plain integer — it is the
        family's BIGINT-backed cell id, same as the CELL_UINT/CELL_BASEVAL return-side
        path for startValue/endValue), so this marshals it directly via the cell type
        reg_scope(name) resolves to.
    Keyed on SHAPE + H3 family, not the literal name, so any future such H3 fn generates."""
    if (f.get("family") or "") != "H3":
        return None
    if not f.get("sqlfn"):
        return None
    # geoToH3IndexSet is rejected by supported() ONLY on its GSERIALIZED arg (marshalled here);
    # geoToH3Cell ONLY on its scalar uint64_t return (marshalled here, bit-preserving into
    # h3index/BIGINT); the cell-vs-temporal comparisons ONLY on their uint64_t scalar arg
    # (marshalled here, same bit-preserving convention); eEq(h3indexset,th3index) passes
    # supported() outright. Any OTHER unsupported reason -> skip.
    sup = supported(f)
    if sup is not None and "GSERIALIZED" not in sup and sup not in ("ret:uint64_t", "arg:uint64_t"):
        return None
    ins, out = classify(f)
    if out is not None or len(ins) != 2:
        return None
    b0, b1 = base(ins[0]["canonical"]), base(ins[1]["canonical"])
    rb, rn = base(f["returnType"]["canonical"]), norm(f["returnType"]["canonical"])
    ptr = lambda p: norm(p["canonical"]).endswith("*")
    if b0 == "GSERIALIZED" and ptr(ins[0]) and b1 == "int" and "*" not in norm(ins[1]["canonical"]):
        if rb == "Set" and rn.endswith("*"):
            return "geo2set"
        if rb == "uint64_t" and "*" not in rn:
            return "geo2scalar"
    if (b0 == "Set" and ptr(ins[0]) and b1 == "Temporal" and ptr(ins[1])
            and rb in ("int", "int32_t") and "*" not in rn):
        return "settemp_ebool"
    if rb in ("int", "int32_t") and "*" not in rn:
        sc = reg_scope(f["name"])
        cell_ok = sc and sc[0] == "types" and len(sc[1]) == 1 and sc[1][0] in CELL_BASEVAL
        if cell_ok and b0 == "uint64_t" and not ptr(ins[0]) and b1 == "Temporal" and ptr(ins[1]):
            return "cell_l:" + sc[1][0]
        if cell_ok and b0 == "Temporal" and ptr(ins[0]) and b1 == "uint64_t" and not ptr(ins[1]):
            return "cell_r:" + sc[1][0]
    return None

def emit_h3_prefilter(f, kind):
    name = f["name"]
    if kind == "geo2set":   # (geometry, int) -> h3indexset ; geometry marshalled @ 4326
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::Execute<string_t, int32_t, string_t>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t in_g, int32_t res) {{\n"
                f"            GSERIALIZED *gs = GeometryToGSerialized(in_g, 4326);\n"
                f"            Set *r = {name}(gs, res);\n"
                f"            free(gs);\n"
                f"            return SetToBlob(result, r);\n"
                f"        }});\n}}\n")
    if kind == "geo2scalar":   # (geometry, int) -> h3index (BIGINT, bit-preserving) @ 4326
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::Execute<string_t, int32_t, int64_t>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t in_g, int32_t res) {{\n"
                f"            GSERIALIZED *gs = GeometryToGSerialized(in_g, 4326);\n"
                f"            uint64_t r = {name}(gs, res);\n"
                f"            free(gs);\n"
                f"            return (int64_t) r;\n"
                f"        }});\n}}\n")
    if kind.startswith("cell_l:"):   # (h3index, th3index) -> int tri-state -> nullable BOOLEAN
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::ExecuteWithNulls<int64_t, string_t, bool>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](int64_t a1, string_t in_t, ValidityMask &mask, idx_t idx) {{\n"
                f"            Temporal *t = BlobToTemporal(in_t);\n"
                f"            int r = {name}((uint64_t) a1, t);\n"
                f"            free(t);\n"
                f"            if (r < 0) {{ mask.SetInvalid(idx); return false; }}\n"
                f"            return r != 0;\n"
                f"        }});\n}}\n")
    if kind.startswith("cell_r:"):   # (th3index, h3index) -> int tri-state -> nullable BOOLEAN
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::ExecuteWithNulls<string_t, int64_t, bool>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t in_t, int64_t a2, ValidityMask &mask, idx_t idx) {{\n"
                f"            Temporal *t = BlobToTemporal(in_t);\n"
                f"            int r = {name}(t, (uint64_t) a2);\n"
                f"            free(t);\n"
                f"            if (r < 0) {{ mask.SetInvalid(idx); return false; }}\n"
                f"            return r != 0;\n"
                f"        }});\n}}\n")
    # settemp_ebool: (h3indexset, th3index) -> int tri-state -> nullable BOOLEAN
    return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(args.data[0], args.data[1], result, args.size(),\n"
            f"        [&](string_t in_s, string_t in_t, ValidityMask &mask, idx_t idx) {{\n"
            f"            Set *s = BlobToSet(in_s);\n"
            f"            Temporal *t = BlobToTemporal(in_t);\n"
            f"            int r = {name}(s, t);\n"
            f"            free(s); free(t);\n"
            f"            if (r < 0) {{ mask.SetInvalid(idx); return false; }}\n"
            f"            return r != 0;\n"
            f"        }});\n}}\n")

def shape_baseval_scalar(f):
    """(BaseValue*)->by-value scalar: the base-value UNARY accessors — a cbuffer/npoint/
    nsegment value in, a scalar out (radius/route/position/SRID/hash/pitch/...). The base
    value is marshalled via BlobTo<Base> (PTR_IN) and freed; registered over the base
    accessor type. Base values are not Temporal/geo, so no other shape catches them.
    Returns (baseCName, retBase) or None."""
    if supported(f) is not None:
        return None
    ins, out = classify(f)
    if out is not None or len(ins) != 1:
        return None
    bb = base(ins[0]["canonical"])
    if bb not in BASEVAL_PTR_IN or not norm(ins[0]["canonical"]).endswith("*"):
        return None
    rb = base(f["returnType"]["canonical"]); rn = norm(f["returnType"]["canonical"])
    if rb in BYVAL_RET and "*" not in rn:
        return (bb, rb)
    return None

def emit_baseval_scalar(f, bb, rb):
    name = f["name"]; marshal = PTR_IN[bb][1] % "in"
    cct, rett, rexpr = byval_ret3(rb)
    return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    UnaryExecutor::Execute<string_t, {rett}>(args.data[0], result, args.size(),\n"
            f"        [&](string_t in) {{\n"
            f"            {bb} *v = {marshal};\n            {cct} r = {name}(v);\n            free(v);\n"
            f"            return {rexpr};\n        }});\n}}\n")

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
        tc = shape_temporal_ctor(f)
        if tc:
            # Temporal constructor / transform NAME FAMILY: one Gen_<fn> body, registered per
            # core-type sqlName over each callable arity (the catalog argDefaults give the shorter
            # overloads). Self-contained (own body + regs + continue), so the generic shapes below
            # never also emit these — the bare names are owned here and by the retired hand loop.
            STATE["grp"] = f.get("group") or "meos_ungrouped"
            fn = f["name"]
            bodies.append(emit_temporal_ctor(f, tc))
            for s in f.get("sqlSignatures", []):
                # The registered SQL/return type is the RESULT temporal type (the parsers
                # take a text/blob arg, the constructors an operand of the same type).
                acc = CORE_CTOR_ACC.get(s["ret"])
                if not acc:
                    continue    # non-core (geo/cbuffer) -> registered in its own family file
                nm = s.get("sqlName", f["sqlfn"])
                argd = s.get("argDefaults") or [None] * len(s["args"])
                required = sum(1 for d in argd if d is None)
                for k in range(required, len(s["args"]) + 1):
                    slots = ", ".join(ctor_arg_slot(s["args"][i], acc) for i in range(k))
                    specific_regs.append(
                        f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                        f'"{reg_name(nm, f)}", {{{slots}}}, {acc}, Gen_{fn}));')
            continue
        cont = CONTAINER_FROM_HEXWKB.get(f["name"])
        if cont:
            # Set/Span/SpanSet FromHexWKB NAME FAMILY: one Gen_<fn> body, registered per core
            # element-type sqlName over the single VARCHAR (hex) argument. Self-contained
            # (own body + regs + continue) like the temporal constructor branch above.
            STATE["grp"] = f.get("group") or "meos_ungrouped"
            fn = f["name"]
            bodies.append(emit_container_from_hexwkb(f, cont))
            for s in f.get("sqlSignatures", []):
                acc = CONTAINER_FROMHEX_ACC.get(s["ret"])
                if not acc:
                    continue    # spatial element (geo/cbuffer/npoint/...) -> its own family file
                nm = s.get("sqlName", f["sqlfn"])
                specific_regs.append(
                    f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                    f'"{reg_name(nm, f)}", {{LogicalType::VARCHAR}}, {acc}, Gen_{fn}));')
            continue
        oh = OUTPUT_HEXWKB.get(f["name"])
        if oh:
            # asHexWKB (Temporal<T> inherited output surface): one Gen_<fn> body, registered per
            # catalog sqlSignature over the input type for the 1-arg and 2-arg (endian text)
            # arities. Only the base-WKB sqlName asHexWKB is emitted here (the extended asHexEWKB
            # is a distinct spatial wrapper). Self-contained (own body + regs + continue); the bare
            # name replaces the retired hand asHexWKB registrations deleted in the same wave.
            cls, blobto = oh
            STATE["grp"] = f.get("group") or "meos_ungrouped"
            fn = f["name"]
            bodies.append(emit_output_hexwkb(f, cls, blobto))
            for s in f.get("sqlSignatures", []):
                nm = s.get("sqlName", f["sqlfn"])
                if nm != "asHexWKB":
                    continue    # asHexEWKB = extended/spatial wrapper (Tspatial_as_hexewkb), own PR
                acc = OUTPUT_HEXWKB_ACC.get(s["args"][0])
                if not acc:
                    continue    # subtype whose Duck type isn't wired yet -> its own family file
                specific_regs.append(
                    f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                    f'"{reg_name(nm, f)}", {{{acc}}}, LogicalType::VARCHAR, Gen_{fn}));')
                specific_regs.append(
                    f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                    f'"{reg_name(nm, f)}", {{{acc}, LogicalType::VARCHAR}}, LogicalType::VARCHAR, Gen_{fn}));')
            continue
        # Base-value UNARY scalar accessors (Cbuffer/Npoint/Nsegment radius/route/
        # position/SRID/hash/...): a base value in, a scalar out. Self-contained (own
        # body + regs + continue); base values aren't Temporal/geo so the shapes below
        # never also emit them.
        bvsc = shape_baseval_scalar(f)
        if bvsc:
            bb, rbb = bvsc
            STATE["grp"] = f.get("group") or "meos_ungrouped"
            fn = f["name"]
            bodies.append(emit_baseval_scalar(f, bb, rbb))
            argt = PTR_IN[bb][0]; dret_bv = byval_ret_duck(rbb)
            for nm in reg_names(f, f["sqlfn"], aliases):
                specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                     f'"{reg_name(nm, f)}", {{{argt}}}, {dret_bv}, Gen_{fn}));')
            continue
        u = shape_emittable(f); b = None if u else shape_binary(f); t = None if (u or b) else shape_ternary(f)
        tt = None if (u or b or t) else shape_binary_tt(f)
        tts = None if (u or b or t or tt) else shape_binary_tt_scalar(f)
        sf = None if (u or b or t or tt or tts) else shape_scalar_first(f)
        gt = None if (u or b or t or tt or tts or sf) else shape_geo_temporal(f)
        ga = None if (u or b or t or tt or tts or sf or gt) else shape_tgeoarr(f)
        pp = None if (u or b or t or tt or tts or sf or gt or ga) else shape_path(f)
        bt = None if (u or b or t or tt or tts or sf or gt or ga or pp) else shape_bound_tail(f)
        if not u and not b and not t and not tt and not tts and not sf and not gt and not ga and not pp and not bt:
            continue
        STATE["grp"] = f.get("group") or "meos_ungrouped"
        sqlfn, fn = f["sqlfn"], f["name"]
        scope, accs = reg_scope(fn) or ("types", None)
        # A generic (`Temporal *`) function whose name carries no type is scoped "all" by
        # reg_scope and blanket-registered over AllTypes()+geo. When the catalog declares its
        # concrete overloads, register over exactly that set instead (the SoT) — dropping the
        # over-registrations (minInstant on tbool/geo, tintInst on all nine). Per-type C fns
        # keep their name scope (the type is in the canonical MEOS symbol, not a heuristic).
        if scope == "all":
            _declared = sig_declared_accs(f)
            if _declared is not None:
                scope, accs = "types", _declared
        # Geo spatial-RELATIONSHIP predicates (group *_rel_ever / *_rel_temp) have NON-UNIFORM
        # per-type support that the `_tgeo`/`_tpoint` name heuristic cannot express and over-expands:
        #   eTouches/eContains/eCovers -> tgeometry only (points use the trajectory-based _tpoint_geo
        #     for touches; are undefined for the others); eIntersects/eDisjoint -> all four geo types;
        #     eDwithin -> mixed; the _tgeo_tgeo T x T forms mirror this.
        # The catalog sqlSignatures are the SoT for exactly which temporal types each backing is
        # CREATE FUNCTION'd over -> scope every relationship backing (both the Temporal x geometry and
        # the Temporal x Temporal shapes) by them. This stops the generic _tgeo_geo from shadowing the
        # point types, binds the point-specific _tpoint_geo to tgeompoint, and drops the fabricated
        # geodetic/cross-type overloads MEOS never declares. Gated to the relationship groups so the
        # gt-shaped restriction ops (atValue/atGeometry, group meos_geo_restrict) keep their name scope.
        if re.search(r'_rel_(ever|temp)$', f.get("group") or ""):
            _gaccs = sig_declared_accs(f)
            if _gaccs is not None:
                scope, accs = "types", _gaccs
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
        elif bt:
            kind, dret, vary, tail, sig = bt; n_ter += 1
            bodies.append(emit_body_bound_tail(f, kind, vary, tail, sig))
            _rest = "".join(", %s" % x for x in sig[1:])
            argsig = "{type, %s%s}" % (sig[0], _rest)
            spec_sig = "{%%s, %s%s}" % (sig[0], _rest)
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
            slots = ["MobilityDuckGeometryType()", "%s"] if geo_first else ["%s", "MobilityDuckGeometryType()"]
            if has_dbl: slots.append("LogicalType::DOUBLE")
            spec_sig = "{" + ", ".join(slots) + "}"       # geo rels are always scope=='types'
            argsig = spec_sig.replace("%s", "type")
        elif ga:
            # NxN array-of-temporal scalars over LIST(<geo>) x LIST(<geo>), registered over exactly
            # the element geo types the catalog CREATE FUNCTION's them for (sqlSignatures = SoT).
            # Self-contained (own registration + continue), like the table-fn path. Two forms:
            #   minDistance -> DOUBLE ; the *Pairs SRFs -> LIST(STRUCT(i,j[,periods])) (UNNEST to rows).
            n_bin += 1
            have = set()
            for s in (f.get("sqlSignatures") or []):
                for a in s.get("args", []):
                    bt = a[:-2] if a.endswith("[]") else a
                    if bt in SIG_TEMPORAL_ACC:
                        have.add(bt)
            elems = [SIG_TEMPORAL_ACC[t] for t in SIG_TEMPORAL_ACC if t in have]
            if ga[0] == "scalar_double":
                bodies.append(emit_tgeoarr_scalar(f))
                for a in elems:
                    for nm in reg_names(f, sqlfn, aliases):
                        specific_regs.append(
                            f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                            f'"{reg_name(nm, f)}", {{LogicalType::LIST({a}), LogicalType::LIST({a})}}, '
                            f'LogicalType::DOUBLE, Gen_{fn}));')
            else:
                _pk, has_dist, has_periods = ga
                bodies.append(emit_pairs_scalar(f, has_dist, has_periods))
                sfields = '{"i", LogicalType::INTEGER}, {"j", LogicalType::INTEGER}'
                if has_periods:
                    sfields += ', {"periods", SpansetTypes::tstzspanset()}'
                rett = f"LogicalType::LIST(LogicalType::STRUCT({{{sfields}}}))"
                for a in elems:
                    slots = f"LogicalType::LIST({a}), LogicalType::LIST({a})"
                    if has_dist:
                        slots += ", LogicalType::DOUBLE"
                    for nm in reg_names(f, sqlfn, aliases):
                        specific_regs.append(
                            f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                            f'"{reg_name(nm, f)}", {{{slots}}}, {rett}, Gen_{fn}));')
            continue
        else:
            # array-return-struct shape -> the canonical SETOF surface = a DuckDB TABLE function
            # (registered via loader.RegisterFunction, NOT the scalar path). Over AllTypes + geo
            # via the {type, type} placeholder the writer wraps in its AllTypes/geo loops.
            n_bin += 1
            bodies.append(emit_path_table(f))
            # The NAME scope, not the sqlSignatures-narrowed one: the generic `temporal_*`
            # path function keeps its blanket registration exactly as before.
            _psc = reg_scope(fn) or ("all", None)
            for nm in reg_names(f, sqlfn, aliases):
                if _psc[0] == "all":
                    generic_regs.append(
                        f'        loader.RegisterFunction(TableFunction("{reg_name(nm, f)}", {{type, type}}, '
                        f'PathExec_{fn}, PathBindFn_{fn}, PathInit_{fn}));')
                else:
                    # A family that owns its own path table function registers over ITS OWN
                    # type, like the scalar branch below. The {type, type} placeholder rides
                    # the AllTypes + spatial loops, so emitting a family function there
                    # registers it for every temporal type and COLLIDES with the generic one
                    # on each: DuckDB refuses the extension at load with "Failed to add new
                    # function overloads to function <name>: function already exists".
                    for acc in _psc[1] or []:
                        specific_regs.append(
                            f'    loader.RegisterFunction(TableFunction("{reg_name(nm, f)}", {{{acc}, {acc}}}, '
                            f'PathExec_{fn}, PathBindFn_{fn}, PathInit_{fn}));')
            continue
        # Names to register: the native sqlfn + any portable bare-name alias the pin
        # assigns to this fn's operator (catalog portableAliases is the SoT — invent
        # nothing). The alias reuses the SAME backing body ([[aliases-reuse-backing]]).
        names = reg_names(f, sqlfn, aliases, argsig)
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
        # A (temporal, scalar-param DEFAULT)->temporal fn (round's precision integer DEFAULT 0)
        # is callable at the shorter arity; emit the (temporal)->temporal overload with the
        # catalog default substituted, over the same types.
        if b and kind == "temporal" and dret == "MD_TEMPORAL" and trailing_arg_default(f):
            dflt = sql_default_to_cpp(trailing_arg_default(f))
            subcast = "" if base(f["returnType"]["canonical"]) == "Temporal" else "(Temporal *) "
            bodies.append(emit_defaulted_unary_temporal(fn, subcast, dflt))
            if scope == "all":
                rett = ret_temporal_type(fn, "type", f.get("group"), f.get("sqlReturnType"))
                for nm in names:
                    generic_regs.append(f'        RegisterSerializedScalarFunction(loader, ScalarFunction('
                                        f'"{reg_name(nm, f)}", {{type}}, {rett}, Gen_{fn}_d));')
            else:
                for a in accs:
                    r2 = ret_temporal_type(fn, a, f.get("group"), f.get("sqlReturnType"))
                    for nm in names:
                        specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                             f'"{reg_name(nm, f)}", {{{a}}}, {r2}, Gen_{fn}_d));')
        # Same shorter-arity overload for a (Temporal, scalar-param DEFAULT)->SCALAR fn
        # (duration(temporal[,boolean]) DEFAULT FALSE; asText/asEWKT(tspatial[,int]) DEFAULT 15):
        # the 1-arg form is canonical SQL but geo-only-hand today — generate it for every type
        # so a new family inherits it too. The return type is the fixed scalar dret.
        if b and kind.startswith("scalar:") and trailing_arg_default(f):
            dflt = sql_default_to_cpp(trailing_arg_default(f))
            bodies.append(emit_defaulted_unary_temporal_scalar(f, dflt))
            if scope == "all":
                for nm in names:
                    generic_regs.append(f'        RegisterSerializedScalarFunction(loader, ScalarFunction('
                                        f'"{reg_name(nm, f)}", {{type}}, {dret}, Gen_{fn}_d));')
            else:
                for a in accs:
                    for nm in names:
                        specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                             f'"{reg_name(nm, f)}", {{{a}}}, {dret}, Gen_{fn}_d));')
    # SET family — separate loop (the temporal path above is untouched).
    for f in fns:
        if declared is not None and f["name"] not in declared:
            continue
        s = shape_set(f)
        if s is None:
            continue
        STATE["grp"] = f.get("group") or "meos_ungrouped"
        kind, dret = s
        # (Set, scalar PARAM)->Set (round/setSRID/transform): the 2nd arg is a precision/
        # SRID, not a set element, so the set type comes from the catalog signature, not the
        # element scalar. Skip entirely when the catalog declares only extended (non-core)
        # sets -> no body emitted (else an unused static), no registration.
        sp = set_scalar_param_sigs(f) if kind.startswith("setsc_set:") else None
        if sp is not None and not sp:
            continue
        n_set += 1
        set_bodies.append(emit_set(f, kind))
        fn, sqlfn = f["name"], f["sqlfn"]
        # portable bare-name alias; normalize the doxygen `@`-escape (sqlop "\@>" -> "@>").
        names = reg_names(f, sqlfn, aliases)
        # element-typed predicates: accessor from the scalar element type, BOOLEAN ret.
        if kind.startswith("setsc_set:"):   # (Set, scalar element) -> Set (same set type)
            b = kind.split(':')[1]
            scd = "LogicalType::VARCHAR" if b == "text" else SCALAR_ARG[b][0]
            # scalar-param: register over the catalog-declared core set types (round->floatset);
            # element-add: the accessor is the element's set type (setUnion(intset)->intset).
            pairs = sp if sp is not None else [(ELEM_TO_SET[b], ELEM_TO_SET[b], None)]
            dflt = next((d for *_, d in pairs if d is not None), None)
            for acc, rett, _d in pairs:
                for nm in names:
                    set_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                             f'"{reg_name(nm, f)}", {{{acc}, {scd}}}, {rett}, Gen_{fn}));')
            # A SQL-optional trailing param (round's precision DEFAULT 0) is callable at the
            # shorter arity; emit the (Set)->Set overload with the catalog default substituted.
            if dflt is not None:
                set_bodies.append(emit_defaulted_unary(fn, "BlobToSet", "SetToBlob", "Set", sql_default_to_cpp(dflt)))
                for acc, rett, _d in pairs:
                    for nm in names:
                        set_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                                 f'"{reg_name(nm, f)}", {{{acc}}}, {rett}, Gen_{fn}_d));')
            continue
        if kind.startswith("setcsc:"):   # (Set, scalar PARAM)->Set: degrees(floatset, bool)
            b = kind.split(':')[1]; scd = SCALAR_ARG[b][0]
            scope, accs = set_reg_scope(fn)
            dflt = trailing_arg_default(f)
            if dflt is not None:
                set_bodies.append(emit_defaulted_unary(fn, "BlobToSet", "SetToBlob", "Set",
                                                       sql_default_to_cpp(dflt)))
            acc_list = ["type"] if scope == "all" else accs
            sink = set_generic_regs if scope == "all" else set_specific_regs
            for a in acc_list:
                for nm in names:
                    sink.append(f'        RegisterSerializedScalarFunction(loader, ScalarFunction('
                                f'"{reg_name(nm, f)}", {{{a}, {scd}}}, {a}, Gen_{fn}));')
                    if dflt is not None:
                        sink.append(f'        RegisterSerializedScalarFunction(loader, ScalarFunction('
                                    f'"{reg_name(nm, f)}", {{{a}}}, {a}, Gen_{fn}_d));')
            continue
        if kind.startswith("setsc:") or kind.startswith("scset:"):
            b = kind.split(':')[1]; acc = ELEM_TO_SET[b]
            scd = "LogicalType::VARCHAR" if b == "text" else SCALAR_ARG[b][0]
            sig = "{%s, %s}" % (acc, scd) if kind.startswith("setsc:") else "{%s, %s}" % (scd, acc)
            for nm in names:
                set_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                         f'"{reg_name(nm, f)}", {sig}, LogicalType::BOOLEAN, Gen_{fn}));')
            continue
        if kind.startswith("bsc:"):   # (Set, by-value uint64 seed) -> uint64 hash: name-scoped 2-arg {type, UBIGINT}
            argdt = SCALAR_ARG[kind.split(':')[1]][0]
            scope, accs = set_reg_scope(fn)
            if scope == "all":
                for nm in names:
                    set_generic_regs.append(f'        RegisterSerializedScalarFunction(loader, ScalarFunction('
                                            f'"{reg_name(nm, f)}", {{type, {argdt}}}, {dret}, Gen_{fn}));')
            else:
                for a in accs:
                    for nm in names:
                        set_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                                 f'"{reg_name(nm, f)}", {{{a}, {argdt}}}, {dret}, Gen_{fn}));')
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
    # COLLECTION families (Span + SpanSet) — ONE machinery via descriptor (shape_span/emit_span
    # take C). Per-container generic list (its own AllTypes loop); specific list shared.
    for C in (SPAN_C, SPANSET_C, STBOX_C, TBOX_C, TPCBOX_C):
        gen = {"Span": span_generic_regs, "SpanSet": spanset_generic_regs}.get(C["cbase"])
        for f in fns:
            if declared is not None and f["name"] not in declared:
                continue
            s = shape_span(f, C)
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
            # mixed span<->spanset positional: typed per the catalog sqlSignatures
            # (intspan x intspanset, ...); accessors from the span + spanset type maps.
            if kind.startswith("b_mix:"):
                _accmap = {**SPAN_TYPES, **SPANSET_TYPES}
                _seen = set()
                for s in (f.get("sqlSignatures") or []):
                    a0, a1 = _accmap.get(s["args"][0]), _accmap.get(s["args"][1])
                    if not (a0 and a1):
                        continue
                    sig = "{%s, %s}" % (a0, a1)
                    if sig in _seen:
                        continue
                    _seen.add(sig)
                    for nm in names:
                        span_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                                  f'"{reg_name(nm, f)}", {sig}, LogicalType::BOOLEAN, Gen_{fn}));')
                continue
            # unary (X)->X|scalar: name-scoped (X_*→AllTypes, <elem>X_*→accessor).
            if kind == "u_span" or kind.startswith("u_scalar:"):
                if C.get("single"):   # box hash carve-out: single type, concrete accessor
                    acc = C["single"]; r = acc if kind == "u_span" else dret
                    for nm in names:
                        box_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                        f'"{reg_name(nm, f)}", {{{acc}}}, {r}, Gen_{fn}));')
                    continue
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
            # (X, by-value uint64 seed)->uint64 hash: name-scoped, 2-arg sig {acc, UBIGINT}.
            if kind.startswith("bsc:"):
                argdt = SCALAR_ARG[kind.split(':')[1]][0]
                if C.get("single"):   # box hashExtended carve-out: single type, concrete accessor
                    acc = C["single"]
                    for nm in names:
                        box_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                        f'"{reg_name(nm, f)}", {{{acc}, {argdt}}}, {dret}, Gen_{fn}));')
                    continue
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
            # (X, scalar PARAM)->X: floatspan(set)_round. Name-scoped 2-arg {X, argtype}->X;
            # plus the shorter {X}->X overload when the trailing scalar has a SQL default.
            if kind.startswith("csc:"):
                argdt = SCALAR_ARG[kind.split(':')[1]][0]
                scope, accs = C["scope"](fn)
                dflt = trailing_arg_default(f)
                if dflt is not None:
                    span_bodies.append(emit_defaulted_unary(fn, C["blobto"], C["toblob"], C["cbase"],
                                                            sql_default_to_cpp(dflt)))
                acc_list = ["type"] if scope == "all" else accs
                sink = gen if scope == "all" else span_specific_regs
                for a in acc_list:
                    for nm in names:
                        sink.append(f'        RegisterSerializedScalarFunction(loader, ScalarFunction('
                                    f'"{reg_name(nm, f)}", {{{a}, {argdt}}}, {a}, Gen_{fn}));')
                        if dflt is not None:
                            sink.append(f'        RegisterSerializedScalarFunction(loader, ScalarFunction('
                                        f'"{reg_name(nm, f)}", {{{a}}}, {a}, Gen_{fn}_d));')
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
        s = shape_temporal_box(f)
        if s is None:
            continue
        STATE["grp"] = f.get("group") or "meos_ungrouped"
        kind, accs = s; n_bin += 1
        temporal_box_bodies.append(emit_temporal_box(f, kind))
        fn, sqlfn = f["name"], f["sqlfn"]
        names = reg_names(f, sqlfn, aliases)
        kparts = kind.split(":")
        box_acc = BOX_MARSH[kparts[1]][1]
        ret_key = kparts[-1]
        dret = "LogicalType::BOOLEAN" if ret_key == "bool" else SCALAR_RET_CPP[ret_key][1]
        for a in accs:
            sig = "{%s, %s}" % (a, box_acc) if kind.startswith("tb_r:") else "{%s, %s}" % (box_acc, a)
            for nm in names:
                temporal_box_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                         f'"{reg_name(nm, f)}", {sig}, {dret}, Gen_{fn}));')
    # Temporal x span -> bool: numspan PAIRS to the tnumber value type (tint↔intspan,
    # tfloat↔floatspan); tstzspan is fixed and registered over every temporal type.
    for f in fns:
        if declared is not None and f["name"] not in declared:
            continue
        s = shape_temporal_span(f)
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
        else:
            # tstz time container, fixed. The BLANKET set is right only for the GENERIC
            # `temporal_*` function; a family that owns its own time restriction — trgeometry
            # does, because its varlena appends the reference geometry and the generic walker
            # would drop it — must register over ITS OWN type alone. Registering the family
            # function over every temporal type points e.g. `minusTime(tint, tstzset)` at
            # `trgeometry_minus_tstzset`, which reads a tint blob as a pose skeleton plus a
            # trailing geometry. The `num` branch above already scopes by name this way.
            sc = reg_scope(fn)
            accs = sc[1] if (sc and sc[0] == "types" and sc[1]) else ALL_TEMPORAL_ACCS
            pairs = [(TSTZ_CONT[cont], a) for a in accs]
        for spacc, tacc in pairs:
            sig = "{%s, %s}" % (tacc, spacc) if not span_first else "{%s, %s}" % (spacc, tacc)
            rett = tacc if retk == "T" else "LogicalType::BOOLEAN"   # at/minus span preserves type
            for nm in names:
                temporal_box_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                         f'"{reg_name(nm, f)}", {sig}, {rett}, Gen_{fn}));')
    # Temporal x finite-subset-of-range -> Temporal restriction (atValues/minusValues),
    # sqlSignatures-driven. Runs only for restrictions the flav path (shape_temporal_span) did NOT
    # claim (atTime), so a function is registered by exactly one path — no double-registration.
    for f in fns:
        if declared is not None and f["name"] not in declared:
            continue
        if shape_temporal_span(f) is not None:
            continue
        s = shape_temporal_restrict_sig(f)
        if s is None:
            continue
        STATE["grp"] = f.get("group") or "meos_ungrouped"
        cont, pairs = s; n_bin += 1
        fn, sqlfn = f["name"], f["sqlfn"]
        temporal_box_bodies.append(emit_temporal_restrict_sig(f, cont))
        names = reg_names(f, sqlfn, aliases)
        for tacc, cacc in pairs:
            for nm in names:
                temporal_box_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                         f'"{reg_name(nm, f)}", {{{tacc}, {cacc}}}, {tacc}, Gen_{fn}));')
    # Temporal -> container conversion (timeSpan/valueSpan/tbox), sqlSignatures-driven — the
    # per-overload (temporal arg type -> container ret type) comes straight from the catalog
    # (mechanical, no flav). Gated on retired(f): emit+register only for a group being retired
    # as a coherent wave, so a not-yet-migrated hand @sqlfn (getTime/getValues/stbox/whenTrue,
    # other groups) is never double-registered against its hand reg.
    for f in fns:
        if declared is not None and f["name"] not in declared:
            continue
        if not retired(f):
            continue
        s = shape_temporal_to_container(f)
        if s is None:
            continue
        STATE["grp"] = f.get("group") or "meos_ungrouped"
        rb, pairs = s; n_un += 1
        fn, sqlfn = f["name"], f["sqlfn"]
        temporal_box_bodies.append(emit_temporal_to_container(f, rb))
        names = reg_names(f, sqlfn, aliases)
        for aacc, racc in pairs:
            for nm in names:
                temporal_box_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                         f'"{reg_name(nm, f)}", {{{aacc}}}, {racc}, Gen_{fn}));')
    # Include order mirrors the hand .cpp files (meos_wrapper + common FIRST, before
    # anything pulls duckdb::Interval into scope; all outside `namespace duckdb`).
    src = ("// GENERATED by tools/codegen_duck_udfs.py from the MEOS-API catalog — DO NOT EDIT.\n"
           '#include "meos_wrapper_simple.hpp"\n'
           '#include "duckdb_version_compat.hpp"\n'  # MobilityDuckGeometryType()
           '#include "common.hpp"\n'
           '#include "temporal/temporal.hpp"\n'
           '#include "temporal/temporal_functions.hpp"\n'   # TemporalHelpers::GetTemptypeFromAlias
           '#include "temporal/set.hpp"\n'
           '#include "temporal/span.hpp"\n'
           '#include "temporal/spanset.hpp"\n'
           '#include "temporal/tbox.hpp"\n'
           '#include "geo/stbox.hpp"\n'
           '#include "pointcloud/tpcbox.hpp"\n'
           '#include "geo/tgeompoint.hpp"\n'
           '#include "geo/tgeogpoint.hpp"\n'
           '#include "geo/tgeometry.hpp"\n'
           '#include "geo/tgeography.hpp"\n'
           '#include "cbuffer/tcbuffer.hpp"\n'         # CbufferTypes::cbuffer()/tcbuffer()
           '#include "h3/th3index.hpp"\n'              # H3indexTypes::h3index()/th3index()
           '#include "quadbin/tquadbin.hpp"\n'         # QuadbinTypes::quadbin()/tquadbin()
           '#include "json/tjsonb.hpp"\n'              # TJsonbTypes::jsonb()/tjsonb()
           '#include "pointcloud/tpcpoint.hpp"\n'      # TPcpointTypes::pcpoint()/tpcpoint()
           '#include "pointcloud/tpcpatch.hpp"\n'      # TPcpatchTypes::pcpatch()/tpcpatch()
           '#include "npoint/tnpoint.hpp"\n'           # NpointTypes::npoint()/nsegment()/tnpoint()
           '#include "pose/tpose.hpp"\n'               # PoseTypes::pose()/tpose()
           '#include "rgeo/trgeometry.hpp"\n'          # TrgeometryTypes::trgeometry()
           '#include "posechain/tposechain.hpp"\n'     # PosechainTypes::posechain()/tposechain()
           '#include "spatial/spatial_types.hpp"\n'   # MobilityDuckGeometryType() (duckdb-spatial)
           '#include "geo_util.hpp"\n'                # GeometryToGSerialized(blob, srid)
           '#include "meos_internal.h"\n'
           '#include "meos_geo.h"\n'
           '#include "meos_internal_geo.h"\n'
           '#include "meos_json.h"\n'                  # Jsonb type (via pgtypes.h) + tjsonb_*/jsonb_* fns
           '#include "meos_pointcloud.h"\n'            # Pcpoint/Pcpatch types + tpcpoint/tpcpatch fns
           '#include "time_util.hpp"\n'
           '#include "mobilityduck/meos_exec_serial.hpp"\n'
           '#include "duckdb/function/scalar_function.hpp"\n'
           '#include "duckdb/main/extension/extension_loader.hpp"\n'
           '#include "duckdb/common/vector_operations/unary_executor.hpp"\n'
           '#include "duckdb/common/vector_operations/binary_executor.hpp"\n'
           '#include "duckdb/common/vector_operations/ternary_executor.hpp"\n'
           '#include <cstring>\n#include <cstdlib>\n#include <limits>\n\n'
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
           "// Array-of-temporal (LIST(blob)) INPUT marshalling for the NxN set-set functions\n"
           "// (minDistance(tgeo[],tgeo[]) etc.). Reads a DuckDB LIST row's child BLOBs into a fresh\n"
           "// Temporal** (each element malloc'd by BlobToTemporal); the caller frees via FreeTemporalArr.\n"
           "inline const Temporal **ListToTemporalArr(Vector &child, list_entry_t le, int *count) {\n"
           "    auto data = FlatVector::GetData<string_t>(child);\n"
           "    int n = (int) le.length;\n"
           "    const Temporal **arr = (const Temporal **) malloc(sizeof(Temporal *) * (n > 0 ? n : 1));\n"
           "    for (idx_t i = 0; i < le.length; i++) arr[i] = BlobToTemporal(data[le.offset + i]);\n"
           "    *count = n;\n    return arr;\n}\n"
           "inline void FreeTemporalArr(const Temporal **arr, int n) {\n"
           "    for (int i = 0; i < n; i++) free((void *) arr[i]);\n    free((void *) arr);\n}\n"
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
           "// Self-contained blob<->Cbuffer marshalling. Cbuffer is a 4-byte-header varlena\n"
           "// (int32 vl_len_ + point + radius) -> VARSIZE out; the stored BLOB is the raw bytes.\n"
           "inline string_t CbufferToBlob(Vector &result, Cbuffer *cb) {\n"
           "    string_t out = StringVector::AddStringOrBlob(result, (const char *)cb, VARSIZE(cb));\n"
           "    free(cb);\n    return out;\n}\n"
           "inline string_t CbufferToBlobN(Vector &result, Cbuffer *cb, ValidityMask &mask, idx_t idx) {\n"
           "    if (!cb) { mask.SetInvalid(idx); return string_t(); }\n    return CbufferToBlob(result, cb);\n}\n"
           "inline Cbuffer *BlobToCbuffer(string_t blob) {\n"
           "    size_t sz = blob.GetSize();\n"
           "    uint8_t *copy = (uint8_t *)malloc(sz);\n"
           "    memcpy(copy, blob.GetData(), sz);\n"
           "    return reinterpret_cast<Cbuffer *>(copy);\n}\n"
           "// Self-contained blob<->Jsonb marshalling. Jsonb is a varlena (vl_len_ header)\n"
           "// -> VARSIZE out; the stored BLOB is the raw jsonb bytes (mirror Cbuffer).\n"
           "inline string_t JsonbToBlob(Vector &result, Jsonb *jb) {\n"
           "    string_t out = StringVector::AddStringOrBlob(result, (const char *)jb, VARSIZE(jb));\n"
           "    free(jb);\n    return out;\n}\n"
           "inline string_t JsonbToBlobN(Vector &result, Jsonb *jb, ValidityMask &mask, idx_t idx) {\n"
           "    if (!jb) { mask.SetInvalid(idx); return string_t(); }\n    return JsonbToBlob(result, jb);\n}\n"
           "inline Jsonb *BlobToJsonb(string_t blob) {\n"
           "    size_t sz = blob.GetSize();\n"
           "    uint8_t *copy = (uint8_t *)malloc(sz);\n"
           "    memcpy(copy, blob.GetData(), sz);\n"
           "    return reinterpret_cast<Jsonb *>(copy);\n}\n"
           "// Self-contained blob<->Pose marshalling. Pose is a 4-byte-header varlena\n"
           "// (int32 vl_len_ + flags + srid + position/orientation doubles) -> VARSIZE out;\n"
           "// the stored BLOB is the raw bytes (mirror Cbuffer).\n"
           "inline string_t PoseToBlob(Vector &result, Pose *p) {\n"
           "    string_t out = StringVector::AddStringOrBlob(result, (const char *)p, VARSIZE(p));\n"
           "    free(p);\n    return out;\n}\n"
           "inline string_t PoseToBlobN(Vector &result, Pose *p, ValidityMask &mask, idx_t idx) {\n"
           "    if (!p) { mask.SetInvalid(idx); return string_t(); }\n    return PoseToBlob(result, p);\n}\n"
           "inline Pose *BlobToPose(string_t blob) {\n"
           "    size_t sz = blob.GetSize();\n"
           "    uint8_t *copy = (uint8_t *)malloc(sz);\n"
           "    memcpy(copy, blob.GetData(), sz);\n"
           "    return reinterpret_cast<Pose *>(copy);\n}\n"
           "// Self-contained blob<->PoseChain marshalling. A 4-byte-header varlena\n"
           "// (int32 vl_len_ + flags + srid + link count + the links' doubles) -> VARSIZE out;\n"
           "// the stored BLOB is the raw bytes (mirror Pose).\n"
           "inline string_t PosechainToBlob(Vector &result, PoseChain *pc) {\n"
           "    string_t out = StringVector::AddStringOrBlob(result, (const char *)pc, VARSIZE(pc));\n"
           "    free(pc);\n    return out;\n}\n"
           "inline string_t PosechainToBlobN(Vector &result, PoseChain *pc, ValidityMask &mask, idx_t idx) {\n"
           "    if (!pc) { mask.SetInvalid(idx); return string_t(); }\n    return PosechainToBlob(result, pc);\n}\n"
           "inline PoseChain *BlobToPosechain(string_t blob) {\n"
           "    size_t sz = blob.GetSize();\n"
           "    uint8_t *copy = (uint8_t *)malloc(sz);\n"
           "    memcpy(copy, blob.GetData(), sz);\n"
           "    return reinterpret_cast<PoseChain *>(copy);\n}\n"
           "// Self-contained blob<->Pcpoint/Pcpatch marshalling. Both are varlena (vl_len_\n"
           "// header) -> VARSIZE out; the stored BLOB is the raw bytes (mirror Cbuffer).\n"
           "inline string_t PcpointToBlob(Vector &result, Pcpoint *pt) {\n"
           "    string_t out = StringVector::AddStringOrBlob(result, (const char *)pt, VARSIZE(pt));\n"
           "    free(pt);\n    return out;\n}\n"
           "inline string_t PcpointToBlobN(Vector &result, Pcpoint *pt, ValidityMask &mask, idx_t idx) {\n"
           "    if (!pt) { mask.SetInvalid(idx); return string_t(); }\n    return PcpointToBlob(result, pt);\n}\n"
           "inline Pcpoint *BlobToPcpoint(string_t blob) {\n"
           "    size_t sz = blob.GetSize();\n"
           "    uint8_t *copy = (uint8_t *)malloc(sz);\n"
           "    memcpy(copy, blob.GetData(), sz);\n"
           "    return reinterpret_cast<Pcpoint *>(copy);\n}\n"
           "inline string_t PcpatchToBlob(Vector &result, Pcpatch *pa) {\n"
           "    string_t out = StringVector::AddStringOrBlob(result, (const char *)pa, VARSIZE(pa));\n"
           "    free(pa);\n    return out;\n}\n"
           "inline string_t PcpatchToBlobN(Vector &result, Pcpatch *pa, ValidityMask &mask, idx_t idx) {\n"
           "    if (!pa) { mask.SetInvalid(idx); return string_t(); }\n    return PcpatchToBlob(result, pa);\n}\n"
           "inline Pcpatch *BlobToPcpatch(string_t blob) {\n"
           "    size_t sz = blob.GetSize();\n"
           "    uint8_t *copy = (uint8_t *)malloc(sz);\n"
           "    memcpy(copy, blob.GetData(), sz);\n"
           "    return reinterpret_cast<Pcpatch *>(copy);\n}\n"
           "// Self-contained blob<->Npoint/Nsegment marshalling. Npoint {rid,pos} and Nsegment\n"
           "// {rid,pos1,pos2} are FIXED-size structs (palloc(sizeof)) -> sizeof out.\n"
           "inline string_t NpointToBlob(Vector &result, Npoint *np) {\n"
           "    string_t out = StringVector::AddStringOrBlob(result, (const char *)np, sizeof(Npoint));\n"
           "    free(np);\n    return out;\n}\n"
           "inline string_t NpointToBlobN(Vector &result, Npoint *np, ValidityMask &mask, idx_t idx) {\n"
           "    if (!np) { mask.SetInvalid(idx); return string_t(); }\n    return NpointToBlob(result, np);\n}\n"
           "inline Npoint *BlobToNpoint(string_t blob) {\n"
           "    if (blob.GetSize() != sizeof(Npoint)) {\n"
           "        throw InvalidInputException(\"An npoint value is \" +\n"
           "            std::to_string(sizeof(Npoint)) + \" bytes, and this one holds \" +\n"
           "            std::to_string(blob.GetSize()));\n    }\n"
           "    Npoint *copy = (Npoint *)malloc(sizeof(Npoint));\n"
           "    memcpy(copy, blob.GetData(), sizeof(Npoint));\n    return copy;\n}\n"
           "inline string_t NsegmentToBlob(Vector &result, Nsegment *ns) {\n"
           "    string_t out = StringVector::AddStringOrBlob(result, (const char *)ns, sizeof(Nsegment));\n"
           "    free(ns);\n    return out;\n}\n"
           "inline string_t NsegmentToBlobN(Vector &result, Nsegment *ns, ValidityMask &mask, idx_t idx) {\n"
           "    if (!ns) { mask.SetInvalid(idx); return string_t(); }\n    return NsegmentToBlob(result, ns);\n}\n"
           "inline Nsegment *BlobToNsegment(string_t blob) {\n"
           "    if (blob.GetSize() != sizeof(Nsegment)) {\n"
           "        throw InvalidInputException(\"An nsegment value is \" +\n"
           "            std::to_string(sizeof(Nsegment)) + \" bytes, and this one holds \" +\n"
           "            std::to_string(blob.GetSize()));\n    }\n"
           "    Nsegment *copy = (Nsegment *)malloc(sizeof(Nsegment));\n"
           "    memcpy(copy, blob.GetData(), sizeof(Nsegment));\n    return copy;\n}\n"
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
           "inline string_t TpcboxToBlob(Vector &result, TPCBox *b) {\n"
           "    string_t out = StringVector::AddStringOrBlob(result, (const char *)b, sizeof(TPCBox));\n"
           "    free(b);\n    return out;\n}\n"
           "inline TPCBox *BlobToTpcbox(string_t blob) {\n"
           "    uint8_t *copy = (uint8_t *)malloc(blob.GetSize());\n    memcpy(copy, blob.GetData(), blob.GetSize());\n"
           "    return reinterpret_cast<TPCBox *>(copy);\n}\n"
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

    # ---- ARRAY-RETURN family: <elem>* + int* count -> LIST(<elem>) (getValues/timestamps/
    # spans/tboxes). Standalone pass because these span Set/Temporal/SpanSet inputs and because
    # supported() rejects them (the trailing int* count arg), so they are disjoint from the family
    # loops above. Registration reuses each input family's scope + writer loop; the LIST child type
    # is fixed per fn by the catalog element type. ----
    for f in fns:
        if declared is not None and f["name"] not in declared:
            continue
        a = shape_array(f)
        if a is None:
            continue
        _tag, ec, ib, accs, tail = a
        STATE["grp"] = f.get("group") or "meos_ungrouped"
        fn, sqlfn = f["name"], f["sqlfn"]
        names = reg_names(f, sqlfn, aliases)
        if ib == "Set":
            set_bodies.append(emit_array(f, ec, ib, tail)); n_set += 1; spec_sink = set_specific_regs
        else:
            (span_bodies if ib == "SpanSet" else bodies).append(emit_array(f, ec, ib, tail))
            if ib == "SpanSet": n_span += 1
            else: n_un += 1
            spec_sink = specific_regs
        # The registered signature carries the container plus one DuckDB type per `tail` entry
        # (the split-into-N count), matching the catalog overload the return type is read from.
        sig_tail = "".join(", " + SQL_BASE_TO_DUCK[t] for t in tail)
        for acc in accs:                                    # sig-declared canonical types; per-acc LIST type
            ret = array_ret_duck(f, acc, tail)
            if ret is None:
                continue
            for nm in names:
                spec_sink.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                 f'"{reg_name(nm, f)}", {{{acc}{sig_tail}}}, {ret}, Gen_{fn}));')

    # ---- H3 cell-set prefilter: geoToH3IndexSet + eEq(h3indexset,th3index) ----
    # Two static H3 shapes the generic paths miss (geometry-arg fixed-SRID; (Set,Temporal)
    # tri-state); emitted with a dedicated shape like the geo-temporal spatial rels.
    for f in fns:
        hpk = shape_h3_prefilter(f)
        if hpk is None:
            continue
        STATE["grp"] = f.get("group") or "meos_ungrouped"
        set_bodies.append(emit_h3_prefilter(f, hpk)); n_set += 1
        fn = f["name"]
        names = reg_names(f, f["sqlfn"], aliases)
        if hpk == "geo2set":
            sig, rett = "{MobilityDuckGeometryType(), LogicalType::INTEGER}", "H3indexTypes::h3indexset()"
        elif hpk == "geo2scalar":
            sig, rett = "{MobilityDuckGeometryType(), LogicalType::INTEGER}", "H3indexTypes::h3index()"
        elif hpk.startswith("cell_l:"):
            temp_acc = hpk.split(":", 1)[1]
            sig, rett = "{%s, %s}" % (CELL_BASEVAL[temp_acc], temp_acc), "LogicalType::BOOLEAN"
        elif hpk.startswith("cell_r:"):
            temp_acc = hpk.split(":", 1)[1]
            sig, rett = "{%s, %s}" % (temp_acc, CELL_BASEVAL[temp_acc]), "LogicalType::BOOLEAN"
        else:   # settemp_ebool
            sig, rett = "{H3indexTypes::h3indexset(), H3indexTypes::th3index()}", "LogicalType::BOOLEAN"
        for nm in names:
            set_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                     f'"{reg_name(nm, f)}", {sig}, {rett}, Gen_{fn}));')

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
    grp_fn, grp_call = {}, {}
    for g in reg_groups:
        fname = "RegisterGenerated_" + re.sub(r"[^A-Za-z0-9_]", "_", g)
        b = "void %s(ExtensionLoader &loader) {\n" % fname
        if tgen.get(g):
            b += _loop("for (auto &type : TemporalTypes::AllTypes()) {", tgen[g])
            # the generic Temporal<T> surface registers over the spatial subtypes too (geo +
            # tcbuffer + future) — a spatial family inherits every generic temporal op by being
            # in SPATIAL_ALLTYPES, not via incidental BLOB-alias coercion.
            b += _loop("for (auto &type : std::vector<LogicalType>{" + ", ".join(GENERIC_BLANKET_SPATIAL) + "}) {", tgen[g])
        b += _flat(tspec.get(g, []))
        if sgen.get(g): b += _loop("for (auto &type : SetTypes::AllTypes()) {", sgen[g])
        b += _flat(sspec.get(g, []))
        if spgen.get(g): b += _loop("for (auto &type : SpanTypes::AllTypes()) {", spgen[g])
        b += _flat(spspec.get(g, []))
        if ssgen.get(g): b += _loop("for (auto &type : SpansetTypes::AllTypes()) {", ssgen[g])
        b += _flat(boxg.get(g, []))
        b += _flat(tboxg.get(g, []))
        b += "}\n"
        grp_fn[g] = "\n" + b
        grp_call[g] = "    %s(loader);" % fname

    # ---- one translation unit per chunk of groups ----
    # A single unit of every group is a 21797-line, 3.5-minute, 2.8 GB compile that the build
    # waits on with one core busy, and it is compiled twice -- once into the static library and
    # once into the loadable extension. A group is self-contained: its registrations name only
    # the bodies emitted under the same @ingroup, so a chunk carries its groups' bodies and their
    # RegisterGenerated_<group> functions and needs nothing from a sibling unit. The chunk count
    # is fixed rather than derived so that the file list CMake compiles is stable across
    # regenerations.
    chunk_dir = os.path.dirname(out_path) or "."
    stem = os.path.splitext(os.path.basename(out_path))[0]
    units = []
    for g in sorted(set(body_by_grp) | set(reg_groups)):
        text = ""
        if body_by_grp.get(g):
            text += "\n// ===== @ingroup %s =====\n" % g + "\n".join(body_by_grp[g]) + "\n"
        units.append((g, text, grp_fn.get(g, ""), grp_call.get(g)))
    # Greedy longest-first packing, which keeps the largest group off the critical path and is a
    # pure function of the catalog, so the same catalog writes the same files.
    bins = [[] for _ in range(GENERATED_CHUNKS)]
    sizes = [0] * GENERATED_CHUNKS
    for u in sorted(units, key=lambda u: -(len(u[1]) + len(u[2]))):
        k = sizes.index(min(sizes))
        bins[k].append(u)
        sizes[k] += len(u[1]) + len(u[2])

    written, chunk_texts = [], []
    for k, unit_list in enumerate(bins):
        chunk = src
        chunk += "".join(u[1] for u in unit_list)
        chunk += "} // anonymous\n"
        chunk += "".join(u[2] for u in unit_list)
        chunk += "\n} // namespace duckdb\n"
        path = os.path.join(chunk_dir, "%s_%d.cpp" % (stem, k))
        open(path, "w").write(chunk)
        written.append(path)
        chunk_texts.append(chunk)

    # The entry point every caller already knows, which now only calls the chunks.
    # ⛔ THE ORDER THESE ARE CALLED IN IS THE ORDER THE OVERLOADS RESOLVE IN. DuckDB picks among
    # the candidates of an operator by the order they were registered, so `intset - 2` binds to
    # set-minus-value only while the set group registers before its rivals. The groups are named
    # here one by one, in the order a single unit registers them, so which unit each one is
    # emitted into cannot move a binding.
    decls = "".join("void %s(ExtensionLoader &loader);\n"
                    % ("RegisterGenerated_" + re.sub(r"[^A-Za-z0-9_]", "_", g))
                    for g in reg_groups)
    master = (src.split("namespace duckdb {")[0]
              + "namespace duckdb {\n\n" + decls
              + "\nvoid RegisterGeneratedTemporalUdfs(ExtensionLoader &loader) {\n"
              + "\n".join("    RegisterGenerated_%s(loader);" % re.sub(r"[^A-Za-z0-9_]", "_", g)
                           for g in reg_groups)
              + "\n}\n\n} // namespace duckdb\n")
    open(out_path, "w").write(master)
    GENERATED_TEXT.append(master + "".join(chunk_texts))
    n_reg = (len(generic_regs) + len(specific_regs) + len(set_generic_regs)
             + len(set_specific_regs) + len(span_generic_regs) + len(span_specific_regs)
             + len(spanset_generic_regs) + len(box_regs) + len(temporal_box_regs))
    return n_un, n_bin, n_ter, n_reg, n_set, n_span

# ---------------------------------------------------------------------------
# Collection type registration (set / span / spanset) generated from the
# catalog MeosType enum. The RegisterType / AllTypes / accessor set plus the
# alias->MeosType and Get{Child,Set,Base}Type mappings are uniform macro
# boilerplate whose ONLY per-family variation is the (base-value x container)
# grid — which the catalog enum encodes exactly. Deriving the type list from
# T_<BASE><SUFFIX> membership makes the orthogonality mechanical: a base that
# has no span (text) simply produces no accessor, so a phantom textspan() can
# never be fabricated. The base LogicalType per base value is the same fixed
# map the hand code used.
# The element LogicalType per base value. A core base names a DuckDB built-in; a base the binding
# registers itself names that type's accessor, and BASE_HEADER states the header declaring it so the
# generated file includes exactly the ones the admitted bases need. A base absent from this map has
# no element type the binding can name, so the derivation below does not admit it — which is how
# geom, geog and h3index, whose set types carry a hand-written function surface of their own, stay
# out of the generated registration and register exactly once.
BASE_LOGICAL = {
    "int": "LogicalType::INTEGER", "bigint": "LogicalType::BIGINT",
    "float": "LogicalType::DOUBLE", "text": "LogicalType::VARCHAR",
    "date": "LogicalType::DATE", "tstz": "LogicalType::TIMESTAMP_TZ",
    "jsonb": "TJsonbTypes::jsonb()", "cbuffer": "CbufferTypes::cbuffer()",
    "npoint": "NpointTypes::npoint()", "quadbin": "QuadbinTypes::quadbin()",
    "pose": "PoseTypes::pose()", "posechain": "PosechainTypes::posechain()",
    "pcpoint": "TPcpointTypes::pcpoint()", "pcpatch": "TPcpatchTypes::pcpatch()",
}
BASE_HEADER = {
    "jsonb": "json/tjsonb.hpp", "cbuffer": "cbuffer/tcbuffer.hpp",
    "npoint": "npoint/tnpoint.hpp", "quadbin": "quadbin/tquadbin.hpp",
    "pose": "pose/tpose.hpp", "posechain": "posechain/tposechain.hpp",
    "pcpoint": "pointcloud/tpcpoint.hpp", "pcpatch": "pointcloud/tpcpatch.hpp",
}
BASE_ORDER = ["int", "bigint", "float", "text", "date", "tstz",
              "jsonb", "cbuffer", "npoint", "quadbin", "pose", "posechain",
              "pcpoint", "pcpatch"]
# suffix (longest first so spanset wins over span), class, mapping struct, DEFINE
# macro, and whether the family carries the spanset-only Set/Base child mappings.
TYPEREG_FAMILIES = [
    dict(suffix="spanset", cls="SpansetTypes", mapping="SpansetTypeMapping",
         macro="DEFINE_SPAN_SET_TYPE", child="span", spanset_extra=True),
    dict(suffix="span", cls="SpanTypes", mapping="SpanTypeMapping",
         macro="DEFINE_SPAN_TYPE", child="base", spanset_extra=False),
    dict(suffix="set", cls="SetTypes", mapping="SetTypeMapping",
         macro="DEFINE_SET_TYPE", child="base", spanset_extra=False),
]

def typereg_members(enum_names, suffix):
    """Bases (in canonical order) for which T_<BASE><SUFFIX> exists in the enum."""
    return [b for b in BASE_ORDER if ("T_" + (b + suffix).upper()) in enum_names]

def emit_typereg_family(fam, enum_names):
    cls, mp, suf, macro = fam["cls"], fam["mapping"], fam["suffix"], fam["macro"]
    bases = typereg_members(enum_names, suf)
    names = [b + suf for b in bases]                       # e.g. intspanset
    L = []
    L.append(f"// --- {cls}: {len(names)} type(s) from the catalog MeosType enum ---")
    L.append(f"#define {macro}(NAME)                                          \\")
    L.append(f"    LogicalType {cls}::NAME() {{                               \\")
    L.append( "        auto type = LogicalType(LogicalTypeId::BLOB);          \\")
    L.append( "        type.SetAlias(#NAME);                                  \\")
    L.append( "        return type;                                           \\")
    L.append( "    }")
    for n in names:
        L.append(f"{macro}({n})")
    L.append(f"#undef {macro}")
    L.append("")
    L.append(f"void {cls}::RegisterTypes(ExtensionLoader &loader) {{")
    for n in names:
        L.append(f'    loader.RegisterType("{n}", {n}());')
    L.append("}")
    L.append("")
    L.append(f"const std::vector<LogicalType> &{cls}::AllTypes() {{")
    L.append("    static std::vector<LogicalType> types = {")
    L.append("        " + ", ".join(f"{n}()" for n in names))
    L.append("    };")
    L.append("    return types;")
    L.append("}")
    L.append("")
    L.append(f"MeosType {mp}::GetMeosTypeFromAlias(const std::string &alias) {{")
    L.append("    static const std::unordered_map<std::string, MeosType> alias_to_type = {")
    L.append("        " + ", ".join(f'{{"{n}", T_{n.upper()}}}' for n in names))
    L.append("    };")
    L.append("    auto it = alias_to_type.find(alias);")
    L.append("    return it != alias_to_type.end() ? it->second : T_UNKNOWN;")
    L.append("}")
    L.append("")
    # GetChildType: spanset -> the sibling span type; set/span -> the base LogicalType.
    L.append(f"LogicalType {mp}::GetChildType(const LogicalType &type) {{")
    L.append("    auto alias = type.ToString();")
    for b, n in zip(bases, names):
        rhs = f"SpanTypes::{b}span()" if fam["child"] == "span" else BASE_LOGICAL[b]
        L.append(f'    if (alias == "{n}") return {rhs};')
    L.append('    throw NotImplementedException("GetChildType: unsupported alias: " + alias);')
    L.append("}")
    if fam["spanset_extra"]:
        L.append("")
        L.append(f"LogicalType {mp}::GetSetType(const LogicalType &type) {{")
        L.append("    auto alias = type.ToString();")
        for b, n in zip(bases, names):
            L.append(f'    if (alias == "{n}") return SetTypes::{b}set();')
        L.append('    throw NotImplementedException("GetSetType: unsupported alias: " + alias);')
        L.append("}")
        L.append("")
        L.append(f"LogicalType {mp}::GetBaseType(const LogicalType &type) {{")
        L.append("    auto alias = type.ToString();")
        for b, n in zip(bases, names):
            L.append(f'    if (alias == "{n}") return {BASE_LOGICAL[b]};')
        L.append('    throw NotImplementedException("GetBaseType: unsupported alias: " + alias);')
        L.append("}")
    L.append("")
    return "\n".join(L)

def gen_type_registration(catalog, out_path):
    enum = [e for e in catalog.get("enums", []) if e["name"] == "MeosType"][0]
    enum_names = {v["name"] for v in enum["values"]}
    parts = [
        "// GENERATED by tools/codegen_duck_udfs.py — do not edit by hand.",
        "// Collection type registration (set / span / spanset). The type list of",
        "// each family is the (base-value x container) grid the catalog MeosType",
        "// enum declares, so the orthogonality is mechanical and phantom-free.",
        "",
        '#include "temporal/set.hpp"',
        '#include "temporal/span.hpp"',
        '#include "temporal/spanset.hpp"',
    ] + [
        # The headers stating the element accessors the admitted bases name.
        '#include "%s"' % BASE_HEADER[b]
        for b in BASE_ORDER
        if b in BASE_HEADER and any(("T_" + (b + f["suffix"]).upper()) in enum_names
                                    for f in TYPEREG_FAMILIES)
    ] + [
        '#include "duckdb/main/extension/extension_loader.hpp"',
        "#include <unordered_map>",
        "",
        "extern \"C\" {",
        "    #include <meos.h>",
        "    #include <meos_internal.h>",
        "}",
        "",
        "namespace duckdb {",
        "",
    ]
    for fam in TYPEREG_FAMILIES:
        parts.append(emit_typereg_family(fam, enum_names))
    parts.append("} // namespace duckdb")
    open(out_path, "w").write("\n".join(parts))
    # The accessor DECLARATIONS come from the same derivation as their definitions, one fragment
    # per family, included inside the hand-written class body. A hand-listed declaration is the
    # half of the class that does not follow the catalog: the definitions widen with the enum and
    # the declarations do not, which the compiler reports as `no declaration matches`.
    inc_dir = os.path.join(os.path.dirname(os.path.dirname(out_path)), "include", "generated")
    os.makedirs(inc_dir, exist_ok=True)
    for fam in TYPEREG_FAMILIES:
        names = [b + fam["suffix"] for b in typereg_members(enum_names, fam["suffix"])]
        open(os.path.join(inc_dir, "%s_accessors.hpp" % fam["suffix"]), "w").write(
            "// GENERATED by tools/codegen_duck_udfs.py — do not edit by hand.\n"
            "// The %s accessors the catalog MeosType enum declares, included inside %s.\n"
            % (fam["suffix"], fam["cls"])
            + "".join("static LogicalType %s();\n" % n for n in names))
    counts = {f["cls"]: len(typereg_members(enum_names, f["suffix"])) for f in TYPEREG_FAMILIES}
    return counts

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
    poc = [f for f in fns if shape_emittable(f)]
    print(f"\nCOMPILABLE-POC subset (unary generic-Temporal*, full bodies): {len(poc)}")
    if out:
        hdr = pos[2] if len(pos) > 2 else None
        declared = header_symbols(hdr) if hdr else None
        if declared is not None:
            print(f"pin/ABI gate: {len(declared)} fns declared in {hdr}")
        nu, nb, nt, nr, ns, nsp = gen_cpp(fns, out, declared, aliases)
        print(f"wrote {nu} unary + {nb} binary + {nt} ternary + {ns} set + {nsp} span UDF bodies, {nr} registrations -> {out}")
        # Collection type registration (set/span/spanset) — the (base x container)
        # grid straight from the catalog MeosType enum, emitted as a sibling file.
        treg = os.path.join(os.path.dirname(out), "generated_type_registration.cpp")
        tc = gen_type_registration(d, treg)
        print(f"wrote collection type registration {tc} -> {treg}")
        # Regularity invariant (build-failing): MEOS keeps locale/collation, session
        # timezone, PROJ context and RNGs in thread-local storage, so every UDF body
        # must run EnsureMeosThreadInitialized() before any MEOS call. Verify it for
        # every emitted body so a future emit path cannot silently drop the guard.
        generated_text = GENERATED_TEXT[0]
        body_section = "".join(part.split("} // anonymous")[0]
                               for part in generated_text.split("// GENERATED by"))
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
        gentext = generated_text
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
