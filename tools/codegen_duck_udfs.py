#!/usr/bin/env python3
"""
MobilityDuck UDF generator — projects the MEOS-API catalog (meos-idl.json) into
DuckDB C++ scalar-UDF registrations. MIRRORS the MobilitySpark generator
(tools/codegen_spark_udfs.py, which mirrors JMEOS FunctionsGenerator) — same
catalog, same classify()/supported() selection logic — but emits DuckDB C++
instead of Java, and marshals via the in-process BLOB path (BlobToTemporal /
TemporalToBlob) instead of hex-WKB, since MobilityDuck links MEOS in-process.

North star: bindings are GENERATED from the catalog; this replaces hand-written
UDFs (generate-then-retire). Scope = scalar functions with the tractable
arg/return shapes and the aggregates of the catalog's `aggregates` section
(#gen_aggregates); table functions, casts and full per-type polymorphism
expansion are the documented next increments.

Usage: python3 tools/codegen_duck_udfs.py <path-to-meos-idl.json> [out.cpp]
"""
import collections
import json, sys, re, os, zlib
from collections import defaultdict, Counter

# Transparent MEOS typedefs the catalog leaves unresolved: a family names its own alias for
# a type the rest of the surface spells natively, and every downstream map is keyed on the
# native spelling. Resolving the alias HERE, once, is what keeps the family regular -- the
# alternative is repeating the aliased spelling in each of OUTPRIM / OUTPARAM_LOCAL /
# ARRAY_ELEM / CELL_UINT, i.e. one hand special-case per map.
# S2CellId: meos_s2cell.h `typedef uint64 S2CellId` over pg_basetypes.h `typedef uint64_t
# uint64` -- uint64_t itself, on every platform, so the resolved spelling is not merely
# same-width but the SAME TYPE, and the pointer positions (S2CellId * out-params and array
# returns) are compatible rather than reinterpreted. s2cell spells the cell id natively
# ("unsigned long") in its scalar positions and S2CellId only in the array/out-parameter
# ones, so without this the family answers startValue but not getValues/valueN.
C_TYPEDEF_ALIASES = {"S2CellId": "uint64_t"}
_ALIAS_RE = re.compile(r'\b(%s)\b' % "|".join(map(re.escape, C_TYPEDEF_ALIASES)))

def norm(c):
    # Strip the `const` and `struct` keywords (the 14l catalog renders canonical
    # types as e.g. `const struct Temporal *`); word-boundary so type names that
    # merely contain the substring are untouched.
    t = re.sub(r'\s+', ' ', re.sub(r'\b(const|struct)\b', '', c or '')).strip()
    return _ALIAS_RE.sub(lambda m: C_TYPEDEF_ALIASES[m.group(1)], t)

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
    "JsonPath":     ("TJsonbTypes::jsonpath()", "BlobToJsonpath(%s)"),
    "Pcpoint":      ("TPcpointTypes::pcpoint()", "BlobToPcpoint(%s)"),
    "Pcpatch":      ("TPcpatchTypes::pcpatch()", "BlobToPcpatch(%s)"),
    "Npoint":       ("NpointTypes::npoint()", "BlobToNpoint(%s)"),
    "Pose":         ("PoseTypes::pose()", "BlobToPose(%s)"),
    "PoseChain":    ("PosechainTypes::posechain()", "BlobToPosechain(%s)"),
    "Nsegment":     ("NpointTypes::nsegment()", "BlobToNsegment(%s)"),
    "Raquet":       ("RaquetTypes::raquet()", "BlobToRaquet(%s)"),
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
    "Raquet":       ("RaquetTypes::raquet()", "RaquetToBlob(result, %s)"),
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
# The SCALAR SQL return types, keyed by the name the catalog reads out of the CREATE FUNCTION
# `RETURNS` clause. A registration answers the type MobilityDB DECLARES, not the type the C kernel
# happens to return: one SQL name is commonly served by several typed kernels whose C returns differ
# while every declaration agrees, so keying on C splits one declared type into several registered
# ones — nad_tint_tint / nad_tbigint_tbigint / nad_tfloat_tfloat return int / int64 / double against
# `nearestApproachDistance(...) RETURNS float` for all three.
# The spellings are the ones the catalog actually carries, counted over it rather than assumed: a
# type is written several ways in the SQL sources (float 115, float8 11, double precision 6;
# integer 98, int 2), and a map missing one silently leaves those functions on the C type.
# Deliberately NOT SQL_BASE_TO_DUCK: that map carries the container types, which these two scalar
# paths must never register. `cstring` / `internal` / `record` are I/O and plumbing shapes with no
# scalar rendering and are excluded for the same reason.
# The C returns whose DuckDB rendering OUTRANKS the SQL declaration, because PostgreSQL has no
# unsigned SQL type to declare and says `integer`/`bigint` for want of one (SCALAR_RET_CPP states
# the consequence: a signed registration is range-checked by DuckDB and flips the sign of a hash
# at or above 2**31). Derived from the C type, not from a name.
UNSIGNED_C_RET = {"uint32_t", "uint64_t"}
# The declared scalar types whose conversion from a C return is a PLAIN CAST, and the executor C++
# type each one needs. The declaration outranks the C type only here: a declared text / date /
# timestamp needs a marshaller (TakeText, FromMeosDate, TakeTimestamp), which the per-C-type
# branches already answer, and a cast would discard.
NUMERIC_DUCK_CPP = {"LogicalType::INTEGER": "int32_t", "LogicalType::BIGINT": "int64_t",
                    "LogicalType::DOUBLE": "double", "LogicalType::BOOLEAN": "bool"}
NUMERIC_C_RET = {"int", "int32_t", "int64_t", "double", "bool"}
SQL_SCALAR_RET = {"integer": "LogicalType::INTEGER", "int": "LogicalType::INTEGER",
                  "bigint": "LogicalType::BIGINT",
                  "float": "LogicalType::DOUBLE", "float8": "LogicalType::DOUBLE",
                  "double precision": "LogicalType::DOUBLE",
                  "boolean": "LogicalType::BOOLEAN", "text": "LogicalType::VARCHAR",
                  "date": "LogicalType::DATE", "timestamptz": "LogicalType::TIMESTAMP_TZ"}
OUTPRIM = {"int *": "LogicalType::INTEGER", "int64_t *": "LogicalType::BIGINT",
           "uint64_t *": "LogicalType::BIGINT",
           "double *": "LogicalType::DOUBLE", "bool *": "LogicalType::BOOLEAN",
           "TimestampTz *": "LogicalType::TIMESTAMP_TZ", "DateADT *": "LogicalType::DATE",
           "text **": "LogicalType::VARCHAR"}
# Out-param C pointer -> (by-value local the folded MEOS call writes into, DuckDB executor cpp).
# A uint64 cell id rides in an int64_t (bit-preserving) to match the BIGINT-backed cell type.
OUTPARAM_LOCAL = {"int *": ("int", "int32_t"), "int64_t *": ("int64_t", "int64_t"),
                  "uint64_t *": ("uint64_t", "int64_t"), "double *": ("double", "double"),
                  "bool *": ("bool", "bool"), "TimestampTz *": ("TimestampTz", "timestamp_tz_t"),
                  "DateADT *": ("DateADT", "date_t"), "text **": ("text *", "string_t")}
# Out-param C pointer -> the conversion its local needs on return, where a cast would not do: a
# MEOS timestamp counts from 2000, a DuckDB one from 1970 (TakeTimestamp, as #scalar_ret4 returns
# a by-value TimestampTz), and an owned text is copied into the result vector and freed
# (TakeText, as #scalar_ret4 returns a text *). A MEOS date counts from 2000 too (FromMeosDate, as
# #scalar_ret4 returns a by-value DateADT). The others are returned by the cast.
OUTPARAM_TAKE = {"TimestampTz *": "TakeTimestamp(%s)", "DateADT *": "FromMeosDate((int32_t) %s)",
                 "text **": "TakeText(result, %s)"}

# `base()` answers the sentinel for a DOUBLE pointer, so an out-param written `T **out` — the
# shape MEOS uses to hand back an owned value beside a bool "was there one" — never resolves
# through it. Strip the out-star and read the pointed-to type.
def outptr_base(canon):
    """The PTR_RET base a `T **` out-param hands back, else None."""
    n = norm(canon)
    if not n.endswith("**"):
        return None
    b = base(n[:-1].strip())
    return b if b in PTR_RET else None

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
                  and (norm(p["canonical"]) in OUTPRIM or base(p["canonical"]) in PTR_RET
                       or outptr_base(p["canonical"]))]
        if writable and not others and (lastn in OUTPRIM or base(lastc) in PTR_RET
                                        or outptr_base(lastc)):
            return params[:-1], lastc
        # an owned geometry handed back (tgeo_value_n's GSERIALIZED **result), in a retired
        # group, where no hand registration of the same signature remains
        if writable and not others and lastn == "GSERIALIZED **" and retired(f):
            return params[:-1], lastc
    return params, None

def arg_type(canon, fname=None):
    """(duck_type, marshal_expr_or_None) for an input param, else None if unmappable.

    `fname` is what resolves a by-value cell id, which the type alone cannot: `uint64_t`
    is the h3index and quadbin cell id, `unsigned long` the s2cell one, and both spellings
    are equally an ordinary hash seed. Only the owning function says which, so the ARGUMENT
    side reads the scope exactly as `ret_type` already reads it for a bare cell-id RETURN.
    A caller that passes no name leaves the cell id unmappable, so no other shape's view of
    a uint64 changes.
    """
    nc = norm(canon); b = base(canon)
    if b in CELL_UINT and "*" not in nc and fname:
        sc = reg_scope(fname)
        if sc and sc[0] == "types" and len(sc[1]) == 1 and sc[1][0] in CELL_BASEVAL:
            return (CELL_BASEVAL[sc[1][0]], None)
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
    if b == "Interval" and nc.count("*") == 1:   # a duration: by-value INTERVAL (#shape_arg)
        return ("LogicalType::INTERVAL", None)
    return None

def outprim_ret3(f, out_canon):
    """(registration LogicalType, out-param local C type, executor RET) for a scalar out-param —
    the out-param counterpart of scalar_ret4, and ONE decision for the same reason: ret_type and
    emit_body_outparam must not disagree about the type, or DuckDB meets the wrong vector.

    The declared SQL type outranks the C pointer under the same two conditions as scalar_ret4 (a
    plain numeric cast, and not an unsigned C quantity PostgreSQL can only declare signed)."""
    n = norm(out_canon)
    cloc, cpp = OUTPARAM_LOCAL[n]
    # a Tcell<T> cell id handed back (valueAtTimestamp(tquadbin, timestamptz)) is its cell type,
    # BIGINT-backed, as the binary out-param fold types valueN (#shape_binary)
    sc = reg_scope(f["name"])
    if base(out_canon) in CELL_UINT and sc and sc[0] == "types" and sc[1] and len(sc[1]) == 1 \
            and sc[1][0] in CELL_BASEVAL:
        return (CELL_BASEVAL[sc[1][0]], cloc, cpp)
    sr = SQL_SCALAR_RET.get(f.get("sqlReturnType"))
    if sr and sr in NUMERIC_DUCK_CPP and base(out_canon) not in UNSIGNED_C_RET \
            and cpp in NUMERIC_DUCK_CPP.values():
        return (sr, cloc, NUMERIC_DUCK_CPP[sr])
    return (OUTPRIM[n], cloc, cpp)

def ret_type(f, out_canon):
    """(duck_ret_type, kind) where kind in {ptr,scalar,outprim,outptr}. None if unmappable."""
    if out_canon is not None:
        n = norm(out_canon)
        if n in OUTPRIM:           return (outprim_ret3(f, out_canon)[0], "outprim")
        if base(out_canon) in PTR_RET:  return (PTR_RET[base(out_canon)][0], "outptr")
        ob = outptr_base(out_canon)
        if ob:                          return (PTR_RET[ob][0], "outptr")
        # an owned geometry handed back, which #classify reads as an out-param in a retired group
        if n == "GSERIALIZED **":       return ("MobilityDuckGeometryType()", "outgeo")
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
    "ts2cell", "s2cell",
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
    # unregistered tpose, and reached the compiler once tpose registered.
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
    # The catalog states the classification in `api`, derived from the @ingroup tag:
    # public where a group exists and is not an internal one, internal otherwise —
    # and a function stating NO group is internal. Reading the group's NAME instead
    # re-derives that rule and gets the no-group case backwards: the string test finds
    # nothing to match, so an internal function passes as public and a call to it is
    # emitted against a symbol only a private header declares (trgeometry_merge, which
    # broke the extension build). Read what the catalog decided.
    if f.get("api") != "public":
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
        # A by-value uint64 is either a cell id or a hash seed, and the two need opposite
        # answers. `arg_type` resolves the cell id from the function's own scope, which
        # leaves the *_hash_extended seed: it has no cell scope, so it stays unmappable
        # here even though shape_binary marshals it via SCALAR_ARG. Accept it for those
        # functions alone.
        # An enum MEOS reads from its name (interpType, through interptype_from_string) is a
        # text in a retired group, as #shape_arg marshals it
        if arg_type(p["canonical"], f["name"]) is None and not (
                base(p["canonical"]) in SCALAR_ARG and "*" not in norm(p["canonical"])
                and name.endswith("_hash_extended")) and not (
                retired(f) and base(p["canonical"]) in ENUM_READER
                and "*" not in norm(p["canonical"])):
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

def scalar_ret4(f):
    """(registration LogicalType, call-var C type, executor RET, return expr) for a by-value
    scalar return — ONE decision, so the type a registration DECLARES and the type its executor
    WRITES cannot disagree. DuckDB raises `Expected vector of type X, but found vector of type Y`
    when they do, so scalar_ret_duck and scalar_emit3 are both projections of this tuple.

    The catalog's declared SQL return type outranks the C one for a NUMERIC return, read first
    exactly as ret_temporal_type reads it for a temporal return: several typed kernels commonly
    serve one SQL name with differing C returns while every declaration agrees, and keying on C
    splits one declared type into several registered ones. The conversion is a plain cast, so the
    executor carries the declared type while the MEOS call keeps its own.

    TWO CASES KEEP THE C TYPE, each for a stated reason and neither by name:
    - an UNSIGNED C return (UNSIGNED_C_RET). PostgreSQL has no unsigned SQL type, so a uint32 hash
      is DECLARED `integer` there for want of one; registering that signed makes DuckDB range-check
      the cast and flip the sign of every hash at or above 2**31 (see SCALAR_RET_CPP). The
      declaration records PostgreSQL's limitation, the C type records the quantity.
    - a return needing a MARSHALLER rather than a cast (Interval / TimestampTz / DateADT / text /
      char). Those branches already answer the declared type, and their conversion is a helper call
      a numeric cast would discard."""
    if is_pred_int(f):
        return ("LogicalType::BOOLEAN", "int32_t", "bool", "(r != 0)")
    rb = base(f["returnType"]["canonical"])
    if rb == "Interval":
        return ("LogicalType::INTERVAL", "MeosInterval *", "interval_t", "TakeInterval(r)")
    if rb == "TimestampTz":
        return ("LogicalType::TIMESTAMP_TZ", "TimestampTz", "timestamp_tz_t", "TakeTimestamp(r)")
    if rb == "DateADT":
        return ("LogicalType::DATE", "DateADT", "date_t", "FromMeosDate((int32_t) r)")
    if rb == "text":
        return ("LogicalType::VARCHAR", "text *", "string_t", "TakeText(result, r)")
    if rb == "char":         # const char* = borrowed/static (no free); char* = owned (free)
        if "const" in (f["returnType"]["canonical"] or ""):
            return ("LogicalType::VARCHAR", "const char *", "string_t",
                    "StringVector::AddString(result, r)")
        return ("LogicalType::VARCHAR", "char *", "string_t", "TakeCString(result, r)")
    ct = SCALAR_RET_CPP[rb][0]
    sr = SQL_SCALAR_RET.get(f.get("sqlReturnType"))
    if sr and sr in NUMERIC_DUCK_CPP and rb not in UNSIGNED_C_RET and rb in NUMERIC_C_RET:
        cpp = NUMERIC_DUCK_CPP[sr]
        return (sr, ct, cpp, f"({cpp}) r")
    return (SCALAR_RET_CPP[rb][1], ct, ct, "r")

def scalar_ret_duck(f):
    """DuckDB registration return type for a by-value scalar return."""
    return scalar_ret4(f)[0]

def scalar_emit3(f):
    """(call_var_ctype, executor_RET, return_expr), paired with scalar_ret_duck by construction."""
    return scalar_ret4(f)[1:]

# The by-value guards a PG wrapper returns NULL on, as the catalog states them
# (`shape.nullableResult`, read from the wrapper's `if (<guard>) PG_RETURN_NULL();`): a
# three-valued predicate answering unknown, and the distance sentinel. They name the result
# `result`; the generated body names it `r`. A pointer result's guard (`! result`,
# `result == NULL`) is the NULL pointer the pointer paths already map. The distance sentinel of
# distance_sentinel is the maximum of the type MEOS returns, tested on `r` in that type before
# its conversion to the declared one, as #emit_temporal_box tests it.
SCALAR_NULL_GUARD = {"if (result < 0)": "r < 0",
                     "if (result == DBL_MAX)": "r == DBL_MAX",
                     "if (datum_eq(result, distance_sentinel(basetype), basetype))":
                         "r == std::numeric_limits<decltype(r)>::max()"}

def scalar_exec(f, rett, rexpr):
    """(executor method, extra lambda parameters, lambda return annotation, return statement) for
    a by-value scalar return. Where the catalog states the result can be absent, the executor is
    ExecuteWithNulls and the row is NULL on the guard, as #emit_geo_temporal states `r < 0` for
    its three-valued predicates; everywhere else the plain executor returns the value."""
    guard = SCALAR_NULL_GUARD.get((f.get("shape") or {}).get("nullableResult"))
    if guard is None:
        return ("Execute", "", "", f"return {rexpr};")
    return ("ExecuteWithNulls", ", ValidityMask &mask, idx_t idx", f" -> {rett}",
            f"if ({guard}) {{ mask.SetInvalid(idx); return {rett}(); }}\n"
            f"            return {rexpr};")

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
                                   "QuadbinTypes::tquadbin()", "S2cellTypes::ts2cell()",
                                   "NpointTypes::tnpoint()",
                                   "PoseTypes::tpose()", "TrgeometryTypes::trgeometry()",
                                   "PosechainTypes::tposechain()"]

# The catalog entry for a function name, so a NAME-keyed scope rule can still consult the
# catalog's own `sqlSignatures` -- populated in main() from the same catalog the surface is
# generated against. reg_scope() takes a name (it is called from a dozen shape functions that
# hold only the name), and threading the whole function dict through all of them would churn
# every call site to reach one branch, so the lookup lives here instead.
FN_BY_NAME = {}

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
    # A subtype accessor (tinstant_timestamptz behind getTimestamp) is generic over the temporal
    # types as a temporal_* one is; the catalog's sqlSignatures then narrow it to the declared set
    # (#sig_declared_accs), and MEOS checks the subtype itself.
    if name.startswith(("temporal_", "tinstant_", "tsequence_", "tsequenceset_")):
        return ("all", None)
    # A `<src>_to_<dst>` CONVERSION names its OPERAND with the PREFIX and its RESULT with the
    # suffix, so the `_<type>` token searches below must not read the target as the operand
    # family: `trgeometry_to_tpose` takes a trgeometry, and `_tpose(?=_|$)` matches its tail.
    # Resolve a conversion on its source alone; ret_temporal_type() already maps the target.
    if "_to_" in name:
        src = name.split("_to_")[0]
        if src in TO_TYPE:
            return ("types", [TO_TYPE[src]])
    # The temporal numbers are tint, tbigint and tfloat; which of them a tnumber_* function is
    # declared over is the catalog's to say, read as the tspatial_* prefix reads it below
    # (#sig_declared_accs), keeping the CORE_TYPES list where the catalog declares none of them.
    # A signature over another type is not this kernel's: tnumber_angular_difference carries the
    # tgeompoint signatures of the SQL name it shares with tpoint_angular_difference.
    if name.startswith("tnumber_"):
        numbers = CORE_TYPES["tint"] + CORE_TYPES["tbigint"] + CORE_TYPES["tfloat"]
        declared = [a for a in sig_declared_accs(FN_BY_NAME.get(name) or {}) or [] if a in numbers]
        return ("types", declared or CORE_TYPES["tnumber"])
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
    # the abstract spatial supertype tspatial_* covers the spatial temporal types with
    # type-preserving results (setSRID/transform/transformPipeline preserve the operand type;
    # asText/asEWKT return text); geometry-coupled variants auto-exclude. This is the
    # TSpatial<T> inherited surface.
    # ⛔ WHICH types it covers is the CATALOG's to say, not this list's. Being spatial makes a
    # family ELIGIBLE for the surface; it does not mean MobilityDB publishes every tspatial_*
    # kernel over it. The cell indexes are the proof: MobilityDB CREATE FUNCTIONs asText for
    # them over the GENERIC `Temporal_as_text` and declares asEWKT/SRID/setSRID/transform for
    # them nowhere, so a blanket registration binds them to a kernel whose own sqlSignatures
    # exclude them -- and tspatial_as_text/as_ewkt/transform then raise "Unknown output
    # function in WKT format for type: <cell>" from inside MEOS, on a call MobilityDB never
    # makes. Defer to the declared signature set, keeping the blanket only when the catalog
    # states nothing (sig_declared_accs answers None), which is its documented fallback.
    # ⛔ The catalog restriction applies to the `tspatial_` PREFIX only -- the NAMED inherited
    # functions. The `_tspatial_tspatial` SUFFIX names are the positional/topological
    # OPERATORS (front/back/left/right/over*), carried by @sqlop rather than @sqlfn, and their
    # sqlSignatures are sparse: reading a sparse list as authoritative deletes 132 working
    # operator registrations. An operator keeps the blanket.
    if name.startswith("tspatial_"):
        declared = sig_declared_accs(FN_BY_NAME.get(name) or {})
        return ("types", declared or SPATIAL_ALLTYPES)
    if re.search(r'_tspatial(?=_|$)', name):
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
    # the Google S2 temporal cell index (its own gated spatial type, sibling of
    # tquadbin and th3index): a ts2cell_* prefix, or a _ts2cell token anywhere. The
    # static s2cell/s2cellset-coupled variants auto-exclude on their unmarshallable
    # arg/return.
    if name.startswith("ts2cell_") or re.search(r'_ts2cell(?=_|$)', name):
        return ("types", ["S2cellTypes::ts2cell()"])
    # the DGGS cell-index operations MobilityDB backs with ONE shared kernel over the cell
    # indexes (tcellindex_get_resolution/cell_area/cell_to_parent/cell_to_boundary/
    # cell_to_point/is_valid_cell). The kernel carries no family token of its own, and which
    # families it serves is NOT uniform: a family that answers a slot from its own kernel is
    # absent from this one (cellToPoint is quadbin alone, h3 answering it geodetically from
    # th3index_to_tgeogpoint), and the returns differ per family (cellToBoundary is tgeometry
    # for quadbin and tgeography for h3). No name heuristic expresses that, so defer to the
    # catalog: "all" hands the function to sig_declared_accs, which registers exactly the
    # overloads MobilityDB CREATE FUNCTIONs.
    if name.startswith("tcellindex_") or re.search(r'_tcellindex(?=_|$)', name):
        return ("all", None)
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
    # the temporal network point (base value `npoint`), read as #tposechain_ reads its family
    if name.startswith("tnpoint_") or re.search(r'_tnpoint(?=_|$)', name):
        return ("types", ["NpointTypes::tnpoint()"])
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
    # A retired group's function no name rule scopes (bearing_tpoint_point names its point
    # operands, not a family) takes the temporal types its signatures declare, as the
    # restriction groups take theirs (#sig_declared_accs)
    f = FN_BY_NAME.get(name)
    if f is not None and retired(f):
        accs = sig_declared_accs(f)
        if accs:
            return ("types", accs)
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
           "tquadbin": "QuadbinTypes::tquadbin()", "ts2cell": "S2cellTypes::ts2cell()",
           "tjsonb": "TJsonbTypes::tjsonb()",
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
    "tquadbin":   "QuadbinTypes::tquadbin()",      "ts2cell":    "S2cellTypes::ts2cell()",
    "tjsonb":     "TJsonbTypes::tjsonb()",
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

def sig_tt_pairs(f):
    """[(first accessor, second accessor, declared temporal return accessor or None, commuted,
    SQL name)] for each signature the catalog declares over two temporal operands, as
    #container_scalar_sigs reads them for a container pair. A pair of two types
    (tDistance(trgeometry, tgeompoint)) cannot be read off one scope type, and both C parameters
    are `Temporal *`, so the commuted pair (tDistance(tgeompoint, trgeometry)) states its order
    through sqlArgParams, the C parameters in SQL order, reversed against the kernel's own. The
    name is the one the signature declares: contains_tjsonb_tjsonb answers
    tjsonbContains(tjsonb, tjsonb) and, its operands swapped, tjsonbContained(tjsonb, tjsonb)."""
    ins, _out = classify(f)
    cnames = [p.get("name") for p in ins]
    out = []
    for s in f.get("sqlSignatures") or []:
        a = s.get("args") or []
        if len(a) != 2 or not all(x in SIG_TEMPORAL_ACC for x in a):
            continue
        rev = s.get("sqlArgParams") == cnames[::-1]
        out.append((SIG_TEMPORAL_ACC[a[0]], SIG_TEMPORAL_ACC[a[1]],
                    SIG_TEMPORAL_ACC.get(s.get("ret")), rev, s.get("sqlName") or f["sqlfn"]))
    return out

def sig_commuted_accs(f):
    """The temporal operand types of the signatures the catalog declares in the commuted order
    of a two-parameter kernel (nearestApproachDistance(stbox, trgeometry) over
    nad_trgeometry_stbox(temp, box)): their sqlArgParams reverse the C parameters, as
    #sig_tt_pairs reads them for a temporal pair."""
    ins, _out = classify(f)
    cnames = [p.get("name") for p in ins]
    if len(cnames) != 2:
        return []
    have = {s["args"][1] for s in f.get("sqlSignatures") or []
            if len(s.get("args") or []) == 2 and s.get("sqlArgParams") == cnames[::-1]
            and s["args"][1] in SIG_TEMPORAL_ACC}
    return [SIG_TEMPORAL_ACC[t] for t in SIG_TEMPORAL_ACC if t in have]

def sig_only_commuted(f, acc):
    """True when every two-argument signature over the temporal type of accessor `acc` declares
    the commuted order, and one does, read as #sig_commuted_accs reads that order: the form of
    concat_tjsonb_jsonb binding invert to INVERT answers tConcat(jsonb, tjsonb) alone, so its
    kernel's own order registers nothing."""
    ins, _out = classify(f)
    cnames = [p.get("name") for p in ins]
    if len(cnames) != 2:
        return False
    over = [s for s in f.get("sqlSignatures") or []
            if len(s.get("args") or []) == 2 and any(SIG_TEMPORAL_ACC.get(x) == acc for x in s["args"])]
    return bool(over) and all(s.get("sqlArgParams") == cnames[::-1] for s in over)

def sig_geo_first(f):
    """Which operand a geometry-and-temporal function takes FIRST **in SQL**, read from the
    catalog's `sqlSignatures`: True geometry-first, False temporal-first, None when the declared
    overloads do not agree on one order (or name no such pair).

    The C parameter order answers a different question. A MobilityDB wrapper is free to read its
    arguments in the opposite order and swap them into the MEOS call — `Tpose_apply_geo` reads
    `(geometry, tpose)` and calls `tpose_apply_geo(temp, body)` — so the kernel's order is the
    KERNEL's, and only the signature states the surface's. Where every declared overload agrees on
    one order, that order IS the surface; registering the C one instead publishes a signature
    MobilityDB does not declare. Where the overloads declare BOTH orders (the commuted wrapper
    pairs, `tDistance(trgeometry, geometry)` beside `tDistance(geometry, trgeometry)`) there is no
    single answer, so the caller keeps its own choice and the second order stays unregistered."""
    def _geo(t):
        return t.startswith("geometry") or t.startswith("geography")
    orders = set()
    for s in (f.get("sqlSignatures") or []):
        a = s.get("args") or []
        if len(a) < 2:
            continue
        if _geo(a[0]) and a[1] in SIG_TEMPORAL_ACC:
            orders.add(True)
        elif a[0] in SIG_TEMPORAL_ACC and _geo(a[1]):
            orders.add(False)
    return orders.pop() if len(orders) == 1 else None

def sig_geo_accs(f, geo_first):
    """The temporal accessors the catalog declares for ONE operand order, in canonical order,
    or None where it declares that order for no type.

    `sig_declared_accs` unions the types across every overload, which is the right answer while a
    backing is registered in a single order. A backing declaring BOTH orders needs them apart: the
    commuted overload is registered for the types ITS OWN signatures name, never for the union."""
    def _geo(t):
        return t.startswith("geometry") or t.startswith("geography")
    have = set()
    for s in (f.get("sqlSignatures") or []):
        a = s.get("args") or []
        if len(a) < 2:
            continue
        if geo_first and _geo(a[0]) and a[1] in SIG_TEMPORAL_ACC:
            have.add(a[1])
        elif not geo_first and a[0] in SIG_TEMPORAL_ACC and _geo(a[1]):
            have.add(a[0])
    return [SIG_TEMPORAL_ACC[t] for t in SIG_TEMPORAL_ACC if t in have] or None

# ---------------- SET family (additive; the temporal path is left untouched) ----------------
# Self-contained Blob<->Set marshalling reuses the hand binding's exact method
# (malloc+memcpy in; set_mem_size out). Per-element accessors mirror CORE_TYPES.
# The base values whose collection types the binding registers, in canonical order. ONE list
# serves both the type registration below (SetTypes/SpanTypes/SpansetTypes, filtered per family by
# T_<BASE><SUFFIX> membership in the catalog MeosType enum) and the name-scoping maps here, so a
# base can never be registered as a type while its family functions stay unscoped. geom and geog
# are absent on purpose: their set types carry a hand-written function surface of their own.
BASE_ORDER = ["int", "bigint", "float", "text", "date", "tstz",
              "jsonb", "cbuffer", "npoint", "h3index", "quadbin", "s2cell", "pose", "posechain",
              "pcpoint", "pcpatch"]
SET_TYPES = {b + "set": "SetTypes::%sset()" % b for b in BASE_ORDER}

# The element LogicalType per base value. A core base names a DuckDB built-in; a base the binding
# registers itself names that type's accessor, and BASE_HEADER states the header declaring it so the
# generated file includes exactly the ones the admitted bases need. A base absent from this map has
# no element type the binding can name, so the derivation below does not admit it — which is how
# geom and geog, whose set types carry a hand-written function surface of their own, stay
# out of the generated registration and register exactly once.
BASE_LOGICAL = {
    "int": "LogicalType::INTEGER", "bigint": "LogicalType::BIGINT",
    "float": "LogicalType::DOUBLE", "text": "LogicalType::VARCHAR",
    "date": "LogicalType::DATE", "tstz": "LogicalType::TIMESTAMP_TZ",
    "jsonb": "TJsonbTypes::jsonb()", "cbuffer": "CbufferTypes::cbuffer()",
    "npoint": "NpointTypes::npoint()", "h3index": "H3indexTypes::h3index()",
    "quadbin": "QuadbinTypes::quadbin()",
    "s2cell": "S2cellTypes::s2cell()",
    "pose": "PoseTypes::pose()", "posechain": "PosechainTypes::posechain()",
    "pcpoint": "TPcpointTypes::pcpoint()", "pcpatch": "TPcpatchTypes::pcpatch()",
    "raquet": "RaquetTypes::raquet()",
}
# The base values no set type holds: MEOS declares no T_<BASE>SET for them, so they stay out of
# BASE_ORDER, which SET_TYPES and the set type registration read, and take the base-value arms
# alone. raquet, the raster tile, is one.
BASE_ONLY = ["raquet"]
def set_reg_scope(name):
    """('all',None) for generic set_* | ('types',[acc]) for <elem>set_* | None."""
    if name.startswith("set_"):
        return ("all", None)
    for pre, acc in SET_TYPES.items():
        if name.startswith(pre + "_") or name == pre:
            return ("types", [acc])
    return None
def ret_set_type(name, arg_acc):
    # The set type the catalog declares for the set this registration reads, read as
    # declared_element_pairs reads a signature's `ret`: jsonbset_pretty answers a textset,
    # jsonbset_array_length an intset and npointset_routes a bigintset, none of them the set
    # they read. The generic loop (`type`) registers over every set type and keeps its own.
    f = FN_BY_NAME.get(name)
    if f and arg_acc != "type":
        for s in f.get("sqlSignatures") or []:
            if (s.get("args") and SET_SIG_ACC.get(s["args"][0]) == arg_acc
                    and SET_SIG_ACC.get(s.get("ret"))):
                return SET_SIG_ACC[s["ret"]]
    # `*_to_<settype>` conversion CHANGES type -> target; else preserve the arg's set type.
    m = re.search(r'_to_(intset|bigintset|floatset|textset|dateset|tstzset)$', name)
    return SET_TYPES[m.group(1)] if m else arg_acc

# The geometry and geography sets, whose element GSERIALIZED has no base value marshaller of the
# set loops (BASE_LOGICAL): the generic set loops over SetTypes::AllTypes() do not reach them, and
# the functions over them register per declared signature through the maps below.
GEO_SET_TYPES = {"geomset": "SpatialSetType::geomset()", "geogset": "SpatialSetType::geogset()"}

# SQL set-type name -> the Duck accessor a declared signature registers over.
SET_SQL_TO_ACC = {**SET_TYPES, **GEO_SET_TYPES}

# The set type a declared (set, scalar) -> set signature names: the registered set types.
SET_SIG_ACC = {**SET_TYPES, **GEO_SET_TYPES}

def declared_element_pairs(f, setfirst):
    """[(set accessor, result accessor, None)] for the two-argument signatures the catalog declares
    for f, reading the set at the position the body reads it; None when it declares none.

    The set a (Set, scalar) -> Set kernel works on is the one its signature names, not the set of
    its scalar: h3_uncompact_cells takes a resolution over an h3indexset, and jsonbset_delete a key
    over a jsonbset, and neither integer nor text is an element of the set it reads."""
    two = [s for s in (f.get("sqlSignatures") or []) if len(s["args"]) == 2]
    if not two:
        return None
    pos = 0 if setfirst else 1
    out = []
    for s in two:
        acc, rett = SET_SIG_ACC.get(s["args"][pos]), SET_SIG_ACC.get(s["ret"])
        if acc and rett and (acc, rett, None) not in out:
            out.append((acc, rett, None))
    return out

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

# A MEOS function whose last by-value parameter every SQL signature fixes to one literal: some
# signatures bind it (`boundArgs`, the planar overloads of tdistance binding spheroid to true)
# and the others declare it as a trailing argument with that literal as its default (the
# geodetic overloads, `spheroid boolean DEFAULT true`). Every call MobilityDB declares is then
# answered by the function with that literal, so the surface registers it at the shorter
# arity, through a forwarding overload of the same name that supplies the literal. It reads
# the default as #trailing_arg_default reads it and the binding as #_build_tail reads a
# function-wide `shape.boundArgs`, here stated per signature. The shapes read the projected
# record and emit their usual call; FORWARDED holds the overloads the generated units define.
FORWARDED = []
BOUND_TAIL_SQL = {"bool": "boolean", "int": "integer", "double": "float"}

def project_bound_tail(f, fixed=()):
    """The record of f without its last SQL-facing parameter, when every sqlSignature fixes that
    parameter to the same literal and at least one binds it; else None. A binding the catalog
    states for the whole function (`shape.boundArgs`, every signature agreeing) is projected
    only for a parameter named in `fixed`, the ones this rule already found fixed per
    signature elsewhere in the catalog (spheroid, bound by tdwithin as by tdistance), so the
    functions whose whole-function binding the shapes read through #_build_tail keep it. The
    forwarding overload is appended to FORWARDED."""
    sigs = f.get("sqlSignatures") or []
    params = f.get("params") or []
    shape = f.get("shape") or {}
    outs = set(shape.get("outParams") or [])
    if not sigs:
        return None
    inputs = [p for p in params if p.get("name") not in outs]
    if not inputs:
        return None
    last = inputs[-1]
    sqlt = BOUND_TAIL_SQL.get(base(last["canonical"]))
    if sqlt is None or "*" in norm(last["canonical"]):
        return None
    whole = shape.get("boundArgs")
    if whole:
        if set(whole) != {last["name"]} or last["name"] not in fixed:
            return None
        if any(s.get("boundArgs") or s.get("argDefaults") for s in sigs):
            return None
        sigs = [dict(s, boundArgs=dict(whole)) for s in sigs]
    lits, binds, newsigs = set(), False, []
    for s in sigs:
        bound = s.get("boundArgs") or {}
        defs = s.get("argDefaults")
        if bound:
            if set(bound) != {last["name"]} or defs:
                return None
            lits.add(bound[last["name"]]); binds = True
            ns = {k: v for k, v in s.items() if k != "boundArgs"}
        elif defs and defs[-1] is not None and s["args"] and s["args"][-1] == sqlt:
            if any(d is not None for d in defs[:-1]):
                return None
            lits.add(defs[-1])
            ns = {k: v for k, v in s.items() if k != "argDefaults"}
            ns["args"] = s["args"][:-1]
        else:
            return None
        newsigs.append(ns)
    if not binds or len(lits) != 1:
        return None
    lit = sql_default_to_cpp(str(MACRO_VALUES.get(next(iter(lits)), next(iter(lits)))))
    g = dict(f)
    g["params"] = [p for p in params if p is not last]
    g["sqlSignatures"] = newsigs
    if whole:
        g["shape"] = {k: v for k, v in shape.items() if k != "boundArgs"}
    decl = ", ".join("%s p%d" % (p["cType"], i) for i, p in enumerate(g["params"]))
    call = ", ".join(lit if p is last else "p%d" % g["params"].index(p) for p in params)
    FORWARDED.append("inline %s %s(%s) {\n    return ::%s(%s);\n}\n"
                     % (f["returnType"]["c"], f["name"], decl, f["name"], call))
    PROJECTED_TAIL[f["name"]] = f      # the longer call, for #keep_declared_tail
    return g

PROJECTED_TAIL = {}

def keep_declared_tail(f):
    """[record] for the longer call of a function #project_bound_tail shortened, when its group is
    retired: the catalog's own record, its signatures those declaring the projected parameter
    (with a default) stripped of the default, named `<fn>_full` and forwarded to the MEOS function
    as #project_signature_forms forwards a form; [] otherwise."""
    orig = PROJECTED_TAIL.get(f["name"])
    # the spatial accessors, whose longer call the binary shape registers as declared; the
    # similarity distances and the array relationships keep their shorter call alone, their
    # shapes registering a signature of their own
    if orig is None or not retired(orig) or orig.get("group") != "meos_geo_accessor":
        return []
    sigs = [{k: v for k, v in s.items() if k != "argDefaults"}
            for s in orig.get("sqlSignatures") or [] if s.get("argDefaults")]
    if not sigs:
        return []
    g = dict(orig, name=orig["name"] + "_full", sqlSignatures=sigs)
    decl = ", ".join("%s p%d" % (re.sub(r"\bInterval\b", "MeosInterval", p["cType"]), i)
                     for i, p in enumerate(orig["params"]))
    call = ", ".join("p%d" % i for i in range(len(orig["params"])))
    FORWARDED.append("inline %s %s(%s) {\n    return ::%s(%s);\n}\n"
                     % (orig["returnType"]["c"], g["name"], decl, orig["name"], call))
    FN_BY_NAME.setdefault(g["name"], g)
    return [g]

# A signature the generic kernel (temporal_*) and a type's own kernel both declare is answered by the
# type's own, as the MobilityDB wrapper they share dispatches it: Temporal_instants calls
# trgeometry_instants for a trgeometry and temporal_instants for every other type, and the generic
# kernel cannot read a rigid geometry's reference geometry ("Cannot access geometry from temporal
# rigid geometry"). The generic record keeps the other signatures, as #project_signature_forms
# keeps the ones a form does not take.
GENERIC_PREFIXES = ("temporal_", "tinstant_", "tsequence_", "tsequenceset_")
def yield_to_type_kernels(fns):
    """fns with every signature a non-generic public function also declares on a temporal operand
    (the operand the wrapper dispatches on) removed from the generic records; a generic record
    left with no signature is dropped."""
    # A reader of a retired I/O group answers the temporal type it reads the same way
    # (tintFromMFJSON is tint_from_mfjson's, the generic temporal_from_mfjson reading the type
    # from its result): its signature (text) -> tint is keyed by its result
    own = {(s.get("sqlName") or f["sqlfn"], tuple(s["args"]))
           for f in fns if f.get("api") == "public" and f.get("sqlfn")
           and not f["name"].startswith(GENERIC_PREFIXES)
           for s in f.get("sqlSignatures") or []
           if s["args"] and s["args"][0] in SIG_TEMPORAL_ACC}
    own |= {(s.get("sqlName") or f["sqlfn"], tuple(s["args"]), s["ret"])
            for f in fns if f.get("api") == "public" and f.get("sqlfn") and retired(f)
            and not f["name"].startswith(GENERIC_PREFIXES)
            for s in f.get("sqlSignatures") or []
            if s.get("ret") in SIG_TEMPORAL_ACC and s["args"] in (["text"], ["bytea"])}
    out = []
    for f in fns:
        sigs = f.get("sqlSignatures") or []
        if not f["name"].startswith(GENERIC_PREFIXES) or not sigs or not f.get("sqlfn"):
            out.append(f); continue
        keep = [s for s in sigs if (s.get("sqlName") or f["sqlfn"], tuple(s["args"])) not in own
                and (s.get("sqlName") or f["sqlfn"], tuple(s["args"]), s.get("ret")) not in own]
        if len(keep) == len(sigs):
            out.append(f)
        elif keep:
            out.append(dict(f, sqlSignatures=keep))
    return out

def forward_literal(p, raw):
    """The C++ argument a SQL literal stands for in a call to a parameter of p's type, as the
    defaulted-ternary branch of the registration loop passes a default (#emit_defaulted_ternary):
    a duration or timestamp through the MEOS parser of its type, a by-value literal as itself
    (#sql_default_to_cpp, a MEOS macro read through MACRO_VALUES); None for a type with no such
    literal."""
    b, n = base(p["canonical"]), norm(p["canonical"])
    text = str(raw).strip().strip("'")
    # an enum member and a null pointer are literals of their own, as JMEOS's _bound_literal
    # passes them (tools/codegen_jvm.py): `interp` LINEAR, `maxt` NULL
    if "*" not in n and text in ENUM_MEMBERS.get(b, ()):
        return text
    # an enum the SQL names in text (tsample's interp DEFAULT 'discrete'), read through the reader
    # #shape_arg reads its argument with
    if "*" not in n and b in ENUM_READER:
        return '%s("%s")' % (ENUM_READER[b], text)
    if "*" in n and text == "NULL":
        return "NULL"
    if b == "Interval" and n.count("*") == 1:
        return 'MeosIntervalLiteral("%s")' % text
    if b == "TimestampTz" and "*" not in n:
        return 'timestamptz_in("%s", -1)' % text
    if b == "DateADT" and "*" not in n:
        return 'date_in("%s")' % text
    if b in SCALAR_ARG and "*" not in n:
        return sql_default_to_cpp(str(MACRO_VALUES.get(raw, raw)))
    return None

# A signature form the shapes cannot reach through the record they are given: a parameter one
# signature binds and another does not (segmentMinDuration binds atleast to true where
# segmentMaxDuration binds it to false, and stops(tgeompoint, interval) binds maxdist where
# stops(tgeompoint, float, interval) takes it), or two or more trailing arguments a call omits
# (stops(tgeompoint) takes the defaults of both). Each form is its own record without those
# parameters, forwarded with their literals as #project_bound_tail forwards one, under a name the
# generated units alone define; the function keeps the signatures that bind nothing. Gated on
# retired(f), as #shape_outparam is: the forms appear where the hand registrations are deleted.
def project_signature_forms(f):
    """[records] standing for f: f itself (unless every signature binds a parameter) and one
    record per distinct set of fixed parameters, or [f] when no signature needs one."""
    if not retired(f) or f.get("api") != "public" or not f.get("sqlfn"):
        return [f]
    sigs = f.get("sqlSignatures") or []
    params = f.get("params") or []
    shape = f.get("shape") or {}
    outs = set(shape.get("outParams") or [])
    inputs = [p for p in params if p.get("name") not in outs and p.get("name") not in
              (shape.get("boundArgs") or {})]
    # The parameters the wrapper binds for the whole function join those a signature binds, as
    # JMEOS merges them (#signatures in tools/codegen_jvm.py): appendSequence passes expand false
    # and appendInstant maxdist 0.0, maxt NULL and expand false beside its interpolation. A
    # function whose own shape reads the whole-function binding (#shape_bound_tail,
    # #shape_text_array, #shape_outparam) keeps it there.
    whole = shape.get("boundArgs") or {}
    if whole and (shape_bound_tail(f) or shape_text_array(f) or shape_outparam(f)):
        whole = {}
    forms, keep = {}, []
    for s in sigs:
        bound = {**whole, **(s.get("boundArgs") or {})}
        sqlin = [p for p in inputs if p["name"] not in bound]
        if len(sqlin) != len(s["args"]):
            return [f]
        defs = s.get("argDefaults") or [None] * len(s["args"])
        tails = [0] if bound else []
        k = 0
        while k < len(defs) and defs[len(defs) - 1 - k] is not None:
            k += 1
            if k >= 2:
                tails.append(k)
        if not bound:
            keep.append(s)
        for k in tails:
            fixed = dict(bound)
            fixed.update({p["name"]: defs[len(defs) - k + i]
                          for i, p in enumerate(sqlin[len(sqlin) - k:])})
            key = tuple(sorted(fixed.items()))
            ns = {kk: v for kk, v in s.items() if kk not in ("boundArgs", "argDefaults")}
            ns["args"] = s["args"][:len(s["args"]) - k]
            if not k and s.get("argDefaults"):
                ns["argDefaults"] = s["argDefaults"]
            ns.setdefault("sqlName", s.get("sqlName") or f["sqlfn"])
            forms.setdefault(key, []).append(ns)
    if not forms:
        return [f]
    # the signatures binding nothing keep the function, under the one name they declare when it
    # is not the function's (shiftScaleTime(tint, interval, interval), where shiftTime and
    # scaleTime bind a NULL shift or duration)
    kept_names = {s.get("sqlName") or f["sqlfn"] for s in keep}
    sqlfn = kept_names.pop() if len(kept_names) == 1 else f["sqlfn"]
    out = [dict(f, sqlSignatures=keep, sqlfn=sqlfn)] if keep else []
    names = Counter("_".join(k for k, _v in key) for key in forms)
    # every form is checked before any is recorded: a form left in FN_BY_NAME by a projection
    # that then answers [f] claims the signatures f keeps (tjsonbPathExists, whose jsonb default
    # '{}' no literal forwards), and a signature two public functions claim registers nothing
    allits = {}
    for key, fsigs in forms.items():
        fixed = dict(key)
        lits = {p["name"]: forward_literal(p, fixed[p["name"]]) for p in params if p["name"] in fixed}
        if None in lits.values() or len({s["sqlName"] for s in fsigs}) != 1:
            return [f]
        allits[key] = lits
    for key, fsigs in forms.items():
        fixed = dict(key)
        lits = allits[key]
        suffix = "_".join(k for k, _v in key)
        if names[suffix] > 1:
            suffix = "_".join("%s_%s" % (k, re.sub(r"\W", "", str(v)).lower()) for k, v in key)
        # A subtype operand (appendInstant's `const TInstant *inst`) is taken as the temporal
        # value the shapes read and cast to its subtype in the forwarded call
        kept = [p for p in params if p["name"] not in fixed]
        sub = {p["name"] for p in kept if base(p["canonical"]) in TEMPORAL_CBASES - {"Temporal"}
               and norm(p["canonical"]).endswith("*")}
        g = dict(f, name="%s_%s" % (f["name"], suffix), sqlfn=fsigs[0]["sqlName"],
                 params=[dict(p, canonical=re.sub(r"\bT(Instant|Sequence|SequenceSet)\b",
                                                  "Temporal", p["canonical"]),
                              cType=re.sub(r"\bT(Instant|Sequence|SequenceSet)\b", "Temporal",
                                           p["cType"]))
                         if p["name"] in sub else p for p in kept],
                 sqlSignatures=fsigs)
        # MEOS's Interval is MeosInterval inside namespace duckdb, as #scalar_ret4 spells it
        decl = ", ".join("%s p%d" % (re.sub(r"\bInterval\b", "MeosInterval", p["cType"]), i)
                         for i, p in enumerate(g["params"]))
        call = ", ".join(lits[p["name"]] if p["name"] in lits
                         else ("(%s) p%d" % (p["cType"], kept.index(p)) if p["name"] in sub
                               else "p%d" % kept.index(p))
                         for p in params)
        FORWARDED.append("inline %s %s(%s) {\n    return ::%s(%s);\n}\n"
                         % (f["returnType"]["c"], g["name"], decl, f["name"], call))
        # a form resolves by its own name as the function it stands for does (#reg_scope)
        FN_BY_NAME.setdefault(g["name"], g)
        out.append(g)
    return out

# Macro name -> value, from the catalog's `macros`: a literal a PG wrapper binds is spelled as
# the MEOS macro (OUT_DEFAULT_DECIMAL_DIGITS), whose value the catalog states. Filled in main().
MACRO_VALUES = {}
# Enum name -> its members, and enum name -> the public MEOS function reading it from its name
# (interptype_from_string), from the catalog's `enums` and `functions`, as JMEOS's _enum_parsers
# reads them (tools/codegen_jvm.py). Filled in main().
ENUM_MEMBERS = {}
ENUM_READER = {}

def declared_text_sigs(f):
    """[(set accessor, two, one)] for the text signatures the catalog declares for f over the set
    types a declared signature can name (SET_SIG_ACC, as declared_element_pairs reads them): two
    where the digit count is an argument, one where the one-argument form answers, declared alone
    (asText(pcpointset)) or through the count's SQL default (asText(cbufferset, integer DEFAULT
    15)). geomset and geogset carry a hand-written text surface of their own and are left to it."""
    out = []
    for s in f.get("sqlSignatures") or []:
        acc = SET_SIG_ACC.get(s["args"][0]) if s["args"] else None
        if not acc:
            continue
        n = len(s["args"])
        dflt = (s.get("argDefaults") or [None] * n)[-1] if n == 2 else None
        out.append((acc, n == 2, n == 1 or dflt is not None))
    return out

def text_arities(f):
    """(two, one) for a text rendering (X, scalar) -> char *: `two` is True when the catalog
    declares the two-argument SQL form, `one` the value the one-argument form binds the scalar
    to (the trailing argument's SQL default, else the literal a one-argument signature binds),
    or None when no one-argument form is declared. The registered arities are the declared ones:
    asText(floatset, integer DEFAULT 15) is both, asText(jsonbset) the second alone."""
    sigs = [s for s in (f.get("sqlSignatures") or [])
            if s.get("sqlName", f.get("sqlfn")) == f.get("sqlfn")]
    two = any(len(s["args"]) == 2 for s in sigs)
    one = trailing_arg_default(f)
    if one is None:
        for s in sigs:
            bound = list((s.get("boundArgs") or {}).values())
            if len(s["args"]) == 1 and len(bound) == 1:
                one = str(MACRO_VALUES.get(bound[0], bound[0]))
                break
    return two, one

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

def emit_defaulted_ternary(f, kind, arg2, dval):
    """The shorter (Temporal, scalar)->X overload of a (Temporal, scalar, scalar-param
    DEFAULT)->X function (tquadbin(tgeompoint, integer[, borderInc boolean DEFAULT TRUE])): the
    body of #emit_body_ternary with the trailing default substituted, over a BinaryExecutor, as
    #emit_defaulted_unary_temporal is the shorter overload of a binary function."""
    name = f["name"]; dt2, cpp2, _ = arg2
    pre, e2, post = _arg2_marshal(arg2[2])
    subcast = "" if base(f["returnType"]["canonical"]) == "Temporal" else "(Temporal *) "
    if kind == "temporal":
        return (f"static void Gen_{name}_d(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::ExecuteWithNulls<string_t, {cpp2}, string_t>("
                f"args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t in, {cpp2} a2, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            Temporal *t = BlobToTemporal(in);\n"
                f"            {pre}Temporal *r = {subcast}{name}(t, {e2}, {dval});\n            {post}free(t);\n"
                f"            return TemporalToBlobN(result, r, mask, idx);\n        }});\n}}\n")
    ctype, rett, _rx = scalar_emit3(f)
    ex, prm, ann, ret = scalar_exec(f, rett, _rx)
    return (f"static void Gen_{name}_d(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    BinaryExecutor::{ex}<string_t, {cpp2}, {rett}>("
            f"args.data[0], args.data[1], result, args.size(),\n"
            f"        [&](string_t in, {cpp2} a2{prm}){ann} {{\n"
            f"            Temporal *t = BlobToTemporal(in);\n"
            f"            {pre}{ctype} r = {name}(t, {e2}, {dval});\n            {post}free(t);\n"
            f"            {ret}\n"
            f"        }});\n}}\n")

def emit_defaulted_unary_temporal_scalar(f, dval):
    """The shorter (Temporal)->scalar overload of a (Temporal, scalar-param DEFAULT)->scalar
    function (duration(temporal[,boolean]), asText/asEWKT(tspatial[,int])): a UnaryExecutor
    calling the MEOS fn with the trailing default substituted, marshalling the by-value/owned
    scalar return exactly like emit_body's scalar branch (scalar_emit3)."""
    name = f["name"]
    cct, rett, rexpr = scalar_emit3(f)
    ex, prm, ann, ret = scalar_exec(f, rett, rexpr)
    return (f"static void Gen_{name}_d(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    UnaryExecutor::{ex}<string_t, {rett}>(args.data[0], result, args.size(),\n"
            f"        [&](string_t in{prm}){ann} {{\n"
            f"            Temporal *t = BlobToTemporal(in);\n"
            f"            {cct} r = {name}(t, {dval});\n            free(t);\n"
            f"            {ret}\n"
            f"        }});\n}}\n")

def emit_defaulted_unary_geo(f, dval):
    """The shorter (Temporal)->geometry overload of a (Temporal, scalar-param DEFAULT)->geometry
    function (traversedArea(tgeometry[, unary_union boolean DEFAULT FALSE])), as
    #emit_defaulted_unary_temporal_scalar is the shorter overload of a scalar one: the owned
    geometry marshalled and freed as #emit_body's geo branch marshals it."""
    name = f["name"]
    return (f"static void Gen_{name}_d(DataChunk &args, ExpressionState &state, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(args.data[0], result, args.size(),\n"
            f"        [&](string_t in, ValidityMask &mask, idx_t idx) -> string_t {{\n"
            f"            Temporal *t = BlobToTemporal(in);\n"
            f"            GSERIALIZED *r = {name}(t, {dval});\n"
            f"            free(t);\n"
            f"            if (!r) {{ mask.SetInvalid(idx); return string_t(); }}\n"
            f"            string_t out = GSerializedToGeometry(r, state, result);\n"
            f"            free(r);\n"
            f"            return out;\n"
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
    # a cell id a set answers (startValue(quadbinset)) is the static cell surface's, which types
    # it as its cell; claiming it here as well would emit its body twice
    if shape_static_cell(f): return None
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
    # (Set, scalar element) -> Set : setUnion/setMinus/setIntersection with an element value,
    # in either operand order. MobilityDB declares both — setIntersection(bigint, bigintset)
    # beside setIntersection(bigintset, bigint) — and the bool-returning branch above already
    # carries the pair as setsc/scset, so the Set-returning one carries it the same way rather
    # than emitting one direction and silently dropping the other.
    if len(ins) == 2 and rb == "Set" and rn.endswith("*"):
        e0, e1 = selem(ins[0]), selem(ins[1])
        if setp(ins[0]) and e1: return ("setsc_set:" + e1, "LogicalType::BLOB")
        if setp(ins[1]) and e0: return ("scset_set:" + e0, "LogicalType::BLOB")
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
    # (Set, scalar PARAM) -> owned C string: the text rendering a set publishes with its digit
    # count (asText(floatset, integer)), as shape_span's u2text arm renders a span; name-scoped
    # like setcsc, or else scoped by the set types its signatures declare, as the generic
    # spatialset_as_text is for cbufferset, poseset, posechainset, pcpointset and pcpatchset.
    # The kind names the declared arity: u2text when the two-argument form is declared (its
    # one-argument form, if any, follows from the default), u2text_d when only the one-argument
    # form is, bound to the literal its signature passes (asText(jsonbset)).
    if (len(ins) == 2 and setp(ins[0]) and base(ins[1]["canonical"]) in SCALAR_ARG
            and "*" not in norm(ins[1]["canonical"]) and rb == "char" and rn.endswith("*")
            and (set_reg_scope(f["name"]) or declared_text_sigs(f))):
        two, one = text_arities(f)
        if two:
            return ("u2text:" + base(ins[1]["canonical"]), "LogicalType::VARCHAR")
        if one is not None:
            return ("u2text_d:" + str(sql_default_to_cpp(one)), "LogicalType::VARCHAR")
        return None
    if set_reg_scope(f["name"]) is None: return None
    if len(ins) == 1 and setp(ins[0]):
        if rb == "Set" and rn.endswith("*"):       return ("u_set", "LogicalType::BLOB")
        if rb in BYVAL_RET and "*" not in rn: return ("u_scalar:" + rb, byval_ret_duck(rb))
        if rb in ("text", "char") and rn.endswith("*"): return ("u_scalar:" + rb, "LogicalType::VARCHAR")
        # (Set) -> the family base value a set of it holds (cbufferset_start_value -> Cbuffer *),
        # the set twin of #shape_emittable's baseval arm; `const` returns are borrowed peeks.
        if (rb in BASEVAL_PTR_RET and rn.endswith("*")
                and "const" not in f["returnType"]["canonical"]):
            return ("u_baseval:" + rb, PTR_RET[rb][0])
        return None
    return None

def emit_set(f, kind):
    name = f["name"]
    if kind.startswith("u2text:"):  # (Set, by-value scalar) -> owned C string (asText), as emit_span
        _dt, cpp2, marsh = SCALAR_ARG[kind.split(':', 1)[1]]
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::Execute<string_t, {cpp2}, string_t>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t a, {cpp2} a2) {{\n"
                f"            Set *s = BlobToSet(a);\n            char *r = {name}(s, {marsh});\n            free(s);\n"
                f"            return TakeCString(result, r);\n        }});\n}}\n")
    if kind.startswith("u2text_d:"):  # the same, with the scalar bound to its SQL DEFAULT
        dflt = kind.split(':', 1)[1]
        return (f"static void Gen_{name}_d(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    UnaryExecutor::Execute<string_t, string_t>(args.data[0], result, args.size(),\n"
                f"        [&](string_t a) {{\n"
                f"            Set *s = BlobToSet(a);\n            char *r = {name}(s, {dflt});\n            free(s);\n"
                f"            return TakeCString(result, r);\n        }});\n}}\n")
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
        # An element operation answers the EMPTY set as a NULL pointer — setMinus('{3}', 3) and
        # setIntersection('{1}', 2) both do — so the result marshals through SetToBlobN, which
        # maps that NULL to SQL NULL. The (Set,Set) twin above already answers NULL for the same
        # emptiness; Execute + SetToBlob raises "Null pointer not allowed" instead.
        e = kind.split(':')[1]
        if e == "text":
            return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                    f"    EnsureMeosThreadInitialized();\n"
                    f"    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(args.data[0], args.data[1], result, args.size(),\n"
                    f"        [&](string_t a, string_t a2, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                    f"            Set *s = BlobToSet(a);\n            text *t2 = MakeText(a2);\n"
                    f"            Set *r = {name}(s, t2);\n            free(t2); free(s);\n"
                    f"            return SetToBlobN(result, r, mask, idx);\n        }});\n}}\n")
        _dt, cpp2, marsh = SCALAR_ARG[e]
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::ExecuteWithNulls<string_t, {cpp2}, string_t>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t a, {cpp2} a2, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            Set *s = BlobToSet(a);\n            Set *r = {name}(s, {marsh});\n            free(s);\n"
                f"            return SetToBlobN(result, r, mask, idx);\n        }});\n}}\n")
    if kind.startswith("scset_set:"):   # (scalar element, Set) -> Set
        # The reversed operand order of setsc_set above, and NULL-safe for the same reason: the
        # empty result arrives as a NULL pointer, which SetToBlobN carries to SQL NULL.
        e = kind.split(':')[1]
        if e == "text":
            return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                    f"    EnsureMeosThreadInitialized();\n"
                    f"    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(args.data[0], args.data[1], result, args.size(),\n"
                    f"        [&](string_t a1, string_t b, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                    f"            text *t1 = MakeText(a1);\n            Set *s = BlobToSet(b);\n"
                    f"            Set *r = {name}(t1, s);\n            free(t1); free(s);\n"
                    f"            return SetToBlobN(result, r, mask, idx);\n        }});\n}}\n")
        _dt, cpp1, marsh = SCALAR_ARG[e]; marsh = marsh.replace("a2", "a1")
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::ExecuteWithNulls<{cpp1}, string_t, string_t>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&]({cpp1} a1, string_t b, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            Set *s = BlobToSet(b);\n            Set *r = {name}({marsh}, s);\n            free(s);\n"
                f"            return SetToBlobN(result, r, mask, idx);\n        }});\n}}\n")
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
    if kind.startswith("u_baseval:"):   # (Set) -> base value ptr, as emit_body_unary's baseval
        rb = kind.split(":", 1)[1]
        rb = PTR_RET[rb][1].split("(", 1)[0][:-len("ToBlob")]
        cct = norm(f["returnType"]["canonical"])
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(args.data[0], result, args.size(),\n"
                f"        [&](string_t in, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            Set *s = BlobToSet(in);\n            {cct} r = {name}(s);\n            free(s);\n"
                f"            return {rb}ToBlobN(result, r, mask, idx);\n        }});\n}}\n")
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
    "S2cellTypes::ts2cell()": "S2cellTypes::s2cell()",
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
    # The operand is a temporal value of any subtype (TEMPORAL_CBASES): a TInstant/TSequence/
    # TSequenceSet operand reads the same blob and is passed cast to its type (#emit_body).
    if base(ins[0]["canonical"]) not in TEMPORAL_CBASES or not norm(ins[0]["canonical"]).endswith("*"):
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
        # accessors have no hand reg, until the geo hand layer is retired; a retired group's
        # (meos_geo_accessor) is the geo hand layer's replacement.
        if sc[0] == "types" and set(sc[1]).issubset(set(GEO_ALLTYPES)) and not retired(f):
            return None
        return ("geo", "MobilityDuckGeometryType()")
    return None

def emit_body(f, kind):
    """The unary body of #emit_body_binary's family, with a subtype operand (TInstant */TSequence */
    TSequenceSet *) passed cast to its type -- the operand-side twin of the `subcast` a subtype
    return takes below: the blob reads as a Temporal *, and the MEOS function checks the subtype."""
    src = emit_body_unary(f, kind)
    ob = base(f["params"][0]["canonical"]) if f.get("params") else "Temporal"
    if ob != "Temporal" and ob in TEMPORAL_CBASES:
        src = src.replace(f"{f['name']}(t)", f"{f['name']}(({ob} *) t)")
    return src

def emit_body_unary(f, kind):
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
        # The marshaller is the one PTR_RET states for the C type, NULL-safe: PoseChain marshals
        # through PosechainToBlob, which the C type does not spell.
        rb = PTR_RET[rb][1].split("(", 1)[0][:-len("ToBlob")]
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
    ex, prm, ann, ret = scalar_exec(f, rett, rexpr)
    return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    UnaryExecutor::{ex}<string_t, {rett}>(args.data[0], result, args.size(),\n"
            f"        [&](string_t in{prm}){ann} {{\n"
            f"            Temporal *t = BlobToTemporal(in);\n"
            f"            {cct} r = {name}(t);\n"
            f"            free(t);\n"
            f"            {ret}\n"
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
    # any other enum MEOS reads from its name, through the reader the catalog states
    if bb in ENUM_READER and "*" not in nn:
        return ("LogicalType::VARCHAR", "string_t", "%s(a2.GetString().c_str())" % ENUM_READER[bb])
    if bb == "char" and nn.count("*") == 1:
        return VARCHAR_ARG[bb]
    # A duration MEOS reads as `const Interval *`, passed in one expression as SCALAR_ARG's
    # TimestampTz is: MeosIntervalArg converts the DuckDB interval for the length of the call.
    if bb == "Interval" and nn.count("*") == 1:
        return ("LogicalType::INTERVAL", "interval_t", "MeosIntervalArg(a2)")
    return None

def scalar_arg_for(f, p):
    """The SCALAR_ARG triple for a by-value argument, with a cell id typed as its own cell type.

    `SCALAR_ARG["uint64_t"]` is the `*_hash_extended` SEED, a native UBIGINT. A `Tcell<T>`
    cell id is the same C type and a DIFFERENT DuckDB one, and only the function's own
    registration scope separates them — which is what `arg_type` and the out-param branch
    of `shape_binary` already read. Taking the seed's triple for a cell id registers
    `eEq(th3index, UBIGINT)` where the family declares `eEq(th3index, h3index)`, so the
    call no longer resolves. Only the DuckDB type moves; the C local and the marshal
    expression are the seed's, because the value IS a uint64 either way.
    """
    t = shape_arg(p)
    if t is None:
        return None
    if base(p["canonical"]) in CELL_UINT and "*" not in norm(p["canonical"]):
        sc = reg_scope(f["name"])
        if sc and sc[0] == "types" and len(sc[1]) == 1 and sc[1][0] in CELL_BASEVAL:
            # A cell type is a BIGINT alias, so its vector is int64_t and the seed's
            # uint64_t local reads it as the wrong physical type ("Expected vector of
            # type UINT64, but found vector of type INT64"). The cell id keeps its bits
            # across the cast, which is what the return side already writes back with.
            return (CELL_BASEVAL[sc[1][0]], "int64_t", "(uint64_t) a2")
    # A signed integer argument is registered as the SQL type every signature declares for it
    # when that type is no wider than the C one, as #scalar_ret4 registers a numeric return:
    # tbigint_value_n reads its position as an int64 where valueN(tbigint, integer) is declared,
    # and the int32 the registration reads widens into it.
    if base(p["canonical"]) in SQL_INT_WIDTH and "*" not in norm(p["canonical"]):
        names = [q["name"] for q in f.get("params") or []]
        k = names.index(p["name"]) if p["name"] in names else None
        sigs = f.get("sqlSignatures") or []
        decl = {s["args"][k] if k is not None and k < len(s["args"]) else None for s in sigs}
        if len(decl) == 1:
            d = decl.pop()
            if d in SQL_INT_ARG and SQL_INT_WIDTH[d] < SQL_INT_WIDTH[base(p["canonical"])]:
                return SQL_INT_ARG[d]
    return t
# The signed integer widths, by C type and by SQL declaration, and the SCALAR_ARG triple each
# declaration registers.
SQL_INT_WIDTH = {"int": 32, "int32_t": 32, "int64_t": 64, "integer": 32, "bigint": 64}
SQL_INT_ARG = {"integer": SCALAR_ARG["int"], "bigint": SCALAR_ARG["int64_t"]}

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
    # a duration, as #shape_arg passes it; gated on retired(f) as #shape_outparam is, so the
    # generated form appears where the hand registration of the same signature is deleted
    is_dur2 = (b2 == "Interval" and n2.count("*") == 1 and retired(f))
    # an enum read from its name (setInterp's interpType) and a C string (trgeometrySeq's
    # interpolation), as #shape_arg marshals them, likewise gated on retired(f)
    is_named2 = retired(f) and ((b2 in ENUM_READER and "*" not in n2)
                                or (b2 == "char" and n2.count("*") == 1))
    if not is_text2 and not is_baseptr2 and not is_dur2 and not is_named2 and \
            (b2 not in SCALAR_ARG or "*" in n2):
        return None
    sc = reg_scope(f["name"])
    if sc is None: return None
    if is_text2:
        arg2 = ("LogicalType::VARCHAR", "string_t", "__TEXT__")
    elif is_baseptr2:                                 # marshal via PTR_IN (BlobTo<Base>) + free after
        arg2 = (PTR_IN[b2][0], "string_t", "__PTRFREE__:" + b2)
    else:
        arg2 = scalar_arg_for(f, ins[1])
    if out is not None:
        # Out-param fold: bool <name>(Temporal, <scalar>, <base *out>) -> the folded base value.
        # The inherited Temporal<T> value accessor valueN (meos_temporal_accessor). Only a
        # concrete scalar out-param is folded here; the generic `Datum *` overload (temporal_value_n)
        # drops out (varying per-type return) so exactly the per-type <t>_value_n emit, once each.
        no = norm(out)
        if no == "GSERIALIZED **":                   # an owned geometry (#classify)
            return ("outgeo", "MobilityDuckGeometryType()", arg2)
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
    # an owned geometry (traversedArea(tgeometry, boolean)) in a retired group, as #emit_body
    # returns one for a single temporal operand
    if (retired(f) and rb == "GSERIALIZED" and rn.endswith("*")
            and "const" not in f["returnType"]["canonical"]):
        return ("geo", "MobilityDuckGeometryType()", arg2)
    return None

def emit_body_binary_rev(f, kind, arg2):
    """The body of #emit_body_binary in the commuted order a declared signature states
    (nearestApproachDistance(cbuffer, tcbuffer) over nad_tcbuffer_cbuffer(temp, cb), its
    sqlArgParams reversing the C parameters), as `Gen_<fn>_rev`, as #emit_temporal_box emits its
    commuted body: the executor reads the second operand off the first vector and the temporal off
    the second, and the call keeps the kernel's own order."""
    name, cpp2 = f["name"], arg2[1]
    body = emit_body_binary(f, kind, arg2)
    return (body.replace(f"Gen_{name}(", f"Gen_{name}_rev(", 1)
                .replace(f"<string_t, {cpp2},", f"<{cpp2}, string_t,", 1)
                .replace(f"(string_t in, {cpp2} a2", f"({cpp2} a2, string_t in", 1))

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
                f"            return {OUTPARAM_TAKE.get(kind.split(':', 1)[1], '(' + exec_cpp + ') %s') % 'v'};\n"
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
    if kind == "outgeo":            # bool <name>(t, a2, &gs) -> the owned geometry; false -> SQL NULL
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &state, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::ExecuteWithNulls<string_t, {cpp2}, string_t>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t in, {cpp2} a2, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            Temporal *t = BlobToTemporal(in);\n"
                f"            {pre}GSERIALIZED *v = NULL;\n"
                f"            bool ok = {name}(t, {call2}, &v);\n            {post}free(t);\n"
                f"            if (!ok || !v) {{ mask.SetInvalid(idx); return string_t(); }}\n"
                f"            string_t out = GSerializedToGeometry(v, state, result);\n"
                f"            free(v);\n"
                f"            return out;\n"
                f"        }});\n}}\n")
    if kind == "geo":               # owned GSERIALIZED return -> DuckDB GEOMETRY, as #emit_body does
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &state, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::ExecuteWithNulls<string_t, {cpp2}, string_t>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t in, {cpp2} a2, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            Temporal *t = BlobToTemporal(in);\n"
                f"            {pre}GSERIALIZED *r = {name}(t, {call2});\n            {post}free(t);\n"
                f"            if (!r) {{ mask.SetInvalid(idx); return string_t(); }}\n"
                f"            string_t out = GSerializedToGeometry(r, state, result);\n"
                f"            free(r);\n"
                f"            return out;\n"
                f"        }});\n}}\n")
    ctype, rett, _rx = scalar_emit3(f)
    ex, prm, ann, ret = scalar_exec(f, rett, _rx)
    inner = f"{pre}{ctype} r = {name}(t, {call2});\n            {post}free(t);\n            {ret}"
    return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    BinaryExecutor::{ex}<string_t, {cpp2}, {rett}>(args.data[0], args.data[1], result, args.size(),\n"
            f"        [&](string_t in, {cpp2} a2{prm}){ann} {{\n"
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
                    "meos_pointcloud.h", "meos_pose.h", "meos_quadbin.h",
                    "meos_rgeo.h", "meos_s2cell.h",
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
    # A box as the second argument (atStbox(tgeompoint, stbox, boolean DEFAULT TRUE)), registered
    # as its own type and decoded and freed per row as #emit_body_binary decodes a base value;
    # gated on retired(f) as #shape_binary gates a duration
    b2 = base(ins[1]["canonical"]); n2 = norm(ins[1]["canonical"])
    if a2 is None and b2 in BOX_MARSH and n2.count("*") == 1 and retired(f):
        a2 = (BOX_MARSH[b2][1], "string_t", "__PTRFREE__:" + b2)
    # A set, span or span set the same way (deleteTime(tint, tstzset, boolean DEFAULT TRUE)),
    # registered as the SQL type every signature declares for it (#CONT_SQL_ACC)
    if a2 is None and b2 in CONT_BLOBTO and n2.count("*") == 1 and retired(f):
        t2 = {s["args"][1] for s in f.get("sqlSignatures") or [] if len(s.get("args") or []) > 1}
        acc2 = CONT_SQL_ACC[b2].get(next(iter(t2))) if len(t2) == 1 else None
        if acc2:
            a2 = (acc2, "string_t", "__PTRFREE__:" + b2)
    if a2 is None or a3 is None: return None
    if reg_scope(f["name"]) is None: return None
    rb = base(f["returnType"]["canonical"]); rn = norm(f["returnType"]["canonical"])
    # An owned temporal return of any subtype (TEMPORAL_PTR_RET: stops answers a TSequenceSet *),
    # upcast in the body as #emit_body_unary upcasts a unary one
    if rb in TEMPORAL_PTR_RET and rn.endswith("*") and "const" not in f["returnType"]["canonical"]:
        return ("temporal", "MD_TEMPORAL", a2, a3)
    if rb in BYVAL_RET and "*" not in rn:
        return ("scalar:" + rb, scalar_ret_duck(f), a2, a3)
    # an owned text (asMFJSON(tint, integer, integer)), as #shape_binary answers one
    if retired(f) and rb in ("text", "char") and rn.endswith("*"):
        return ("scalar:" + rb, scalar_ret_duck(f), a2, a3)
    return None

def _arg2_marshal(marsh):
    """(statement before the call, the call's argument, statement after it) for the second
    argument: a pointer is decoded and freed around the call as #emit_body_binary does it,
    any other argument is its marshal expression."""
    if marsh.startswith("__PTRFREE__:"):
        pb = marsh.split(":", 1)[1]
        return (f"{pb} *a2p = {PTR_IN[pb][1] % 'a2'};\n            ", "a2p", "free(a2p); ")
    return ("", marsh, "")

def emit_body_ternary(f, kind, arg2, arg3):
    name = f["name"]; dt2, cpp2, _ = arg2; dt3, cpp3, _ = arg3
    pre, e2, post = _arg2_marshal(arg2[2])
    e3 = arg3[2].replace("a2", "a3")   # SCALAR_ARG marshal is templated on "a2"
    subcast = "" if base(f["returnType"]["canonical"]) == "Temporal" else "(Temporal *) "
    if kind == "temporal":          # ternary -> Temporal (NULL-safe pointer return)
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    TernaryExecutor::ExecuteWithNulls<string_t, {cpp2}, {cpp3}, string_t>("
                f"args.data[0], args.data[1], args.data[2], result, args.size(),\n"
                f"        [&](string_t in, {cpp2} a2, {cpp3} a3, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            Temporal *t = BlobToTemporal(in);\n"
                f"            {pre}Temporal *r = {subcast}{name}(t, {e2}, {e3});\n            {post}free(t);\n"
                f"            return TemporalToBlobN(result, r, mask, idx);\n        }});\n}}\n")
    ctype, rett, _rx = scalar_emit3(f)
    ex, prm, ann, ret = scalar_exec(f, rett, _rx)
    inner = f"{pre}{ctype} r = {name}(t, {e2}, {e3});\n            {post}free(t);\n            {ret}"
    return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    TernaryExecutor::{ex}<string_t, {cpp2}, {cpp3}, {rett}>("
            f"args.data[0], args.data[1], args.data[2], result, args.size(),\n"
            f"        [&](string_t in, {cpp2} a2, {cpp3} a3{prm}){ann} {{\n"
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
    # A value the binding registers as its own BLOB type (jsonpath, cbuffer, …) arrives as a
    # string_t and marshals through the same helper every other shape uses for it.
    if b in PTR_IN and n.endswith("*") and n.count("*") == 1:
        dt, expr = PTR_IN[b]
        if dt != "MD_TEMPORAL":
            return (dt, "string_t", expr, "free")
    # Any other by-value scalar the shapes already marshal — a timestamp and a date carry a
    # conversion, which is why they are read from SCALAR_ARG rather than assumed to pass through.
    if b in SCALAR_ARG and "*" not in n:
        dt, cpp, expr = SCALAR_ARG[b]
        return (dt, cpp, expr.replace("a2", "%s"), "")
    return None

# A per-call setting that is a POINTER: read once outside the executor and freed after it. The
# catalog marks the nullable ones (`shape.nullable`) and MobilityDB gives them a SQL default, so
# an omitted or NULL argument reaches MEOS as NULL rather than a fabricated value.
def _const_ptr(p):
    b = base(p["canonical"]); n = norm(p["canonical"])
    if b in PTR_IN and n.endswith("*") and n.count("*") == 1:
        dt, expr = PTR_IN[b]
        if dt != "MD_TEMPORAL":
            return (dt, b, expr)
    return None

# ── the tail a shape carries after its leading operands ────────────────────────────────
# A MEOS parameter past the leading operands is one of three things, and both shapes below
# read them the same way: a literal the PG wrapper binds (`shape.boundArgs`), a by-value
# per-call setting, or a pointer-valued one. Keeping the reading in one place is what lets a
# new argument kind reach every shape at once.
def _build_tail(params, bound):
    """[(kind, payload)] for the parameters after the leading operands, or None if one of
    them is a kind no shape can carry."""
    tail = []
    for p in params:
        nm = p.get("name")
        if nm in bound:
            tail.append(("lit", bound[nm])); continue
        st = SETTING_ARG.get(base(p["canonical"])) if "*" not in norm(p["canonical"]) else None
        if st is not None:
            tail.append(("col", st)); continue
        pt = _const_ptr(p)
        if pt is None: return None
        tail.append(("ptr", pt))
    return tail

def _tail_sig(tail):
    """The DuckDB types the tail exposes — a bound literal is emitted, never exposed."""
    return [st[0] for k, st in tail if k in ("col", "ptr")]

def _emit_tail_consts(L, tail, col=2):
    """Append the per-call reads for `tail` to `L`; answer (call arguments, pointers to free).
    A setting is read ONCE outside the executor, which is what lets a BinaryExecutor run over
    the operands that vary — DuckDB has no wider executor."""
    call, frees = [], []
    for kindi, payload in tail:
        if kindi == "lit":
            call.append(payload); continue
        if kindi == "ptr":
            _d, cbase_, marshal = payload
            L.append(f"    {cbase_} *c{col} = NULL;")
            L.append(f"    {{ auto &cv = args.data[{col}]; cv.Flatten(rc); Value v = cv.GetValue(0);"
                     " if (!v.IsNull()) c%d = %s; }" % (col, marshal % "string_t(StringValue::Get(v))"))
            frees.append(f"c{col}")
        else:
            _d, ctype, cexpr = payload
            L.append(f"    {ctype} c{col} = ({ctype}) 0;")
            L.append(f"    {{ auto &cv = args.data[{col}]; cv.Flatten(rc); Value v = cv.GetValue(0);"
                     f" if (!v.IsNull()) c{col} = {cexpr}; }}")
        call.append(f"c{col}")
        col += 1
    return call, frees

def _close_body(L, frees):
    """Release the pointer settings, then close the function."""
    L += ["    free(%s);" % fr for fr in frees]
    L += ["}", ""]
    return "\n".join(L)

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
    tail = _build_tail(ins[2:], bound)
    if tail is None: return None
    if not any(k == "col" for k, _ in tail): return None
    if reg_scope(f["name"]) is None: return None
    rb = base(f["returnType"]["canonical"]); rn = norm(f["returnType"]["canonical"])
    if not (rb == "Temporal" and rn.endswith("*")): return None
    # the registered signature is the type, the varying arg, then each exposed setting
    sig = [v[0]] + _tail_sig(tail)
    return ("temporal", "MD_TEMPORAL", v, tail, sig)

def emit_body_bound_tail(f, kind, vary, tail, sig):
    name = f["name"]
    _dt, cpp2, expr2, cleanup = vary
    L = [f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{",
         "    EnsureMeosThreadInitialized();",
         "    auto rc = args.size();"]
    call, frees = _emit_tail_consts(L, tail)
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
          "        });"]
    return _close_body(L, frees)

def shape_text_array(f):
    """(Temporal *, text **elems, int count, <bound literals and settings...>) -> Temporal *.

    A `text **` and the `int` that follows it are ONE SQL argument — MobilityDB declares
    `text[]` (`sqlSignatures.args`), so DuckDB registers LIST(VARCHAR) and the count is
    supplied by the list's own length rather than exposed."""
    ins, out = classify(f)
    if out is not None or len(ins) < 3: return None
    # The path array reads the same over a SET as over a temporal — MobilityDB declares
    # `jsonbsetExtractPath(jsonbset, text[], text)` beside `tjsonbExtractPath(tjsonb, ...)` —
    # so the operand family is what varies, not the shape. A set scopes through
    # set_reg_scope (`jsonbset_*` -> SetTypes::jsonbset()), which is a different map from the
    # temporal reg_scope: reg_scope answers None for EVERY `jsonbset_*` name, including the
    # ones the set shapes already emit, so reading it for a set operand rejects the family.
    ob = base(ins[0]["canonical"])
    if ob not in ("Temporal", "Set") or not norm(ins[0]["canonical"]).endswith("*"):
        return None
    # `base()` answers the sentinel for a double pointer, so the test reads the normalised
    # spelling: a `text **` is the path/keys array MobilityDB declares as `text[]`.
    if norm(ins[1]["canonical"]).replace(" ", "") != "text**":
        return None
    if not (base(ins[2]["canonical"]) == "int" and "*" not in norm(ins[2]["canonical"])):
        return None
    sigs = f.get("sqlSignatures") or []
    if not any("text[]" in (sg.get("args") or []) for sg in sigs):
        return None                      # the SQL surface is the SoT for the pairing
    # A kernel whose signatures name MORE THAN ONE SQL function is the shared generic behind a
    # family (`tjsonb_exists_array` carries both tjsonbExistsAny and tjsonbExistsAll, selected by
    # the `any` flag it binds). Each of those names has its own dedicated kernel, so registering
    # the generic too would register one DuckDB signature twice — which the extension refuses at
    # LOAD, on a green build.
    if len({sg.get("sqlName") for sg in sigs if sg.get("sqlName")}) > 1:
        return None
    bound = ((f.get("shape") or {}).get("boundArgs") or {})
    tail = _build_tail(ins[3:], bound)
    if tail is None: return None
    if (set_reg_scope if ob == "Set" else reg_scope)(f["name"]) is None: return None
    rb = base(f["returnType"]["canonical"]); rn = norm(f["returnType"]["canonical"])
    # The operand family and the result family are the same one: a set path operation answers a
    # set (`jsonbset_extract_path` -> `Set *`), a temporal one a temporal. Accepting a crossed
    # pair would emit a body marshalling the result through the wrong `*ToBlobN`.
    if not (rb == ob and rn.endswith("*")): return None
    sig = ["LogicalType::LIST(LogicalType::VARCHAR)"] + _tail_sig(tail)
    # `MD_SET` is the set counterpart of the `MD_TEMPORAL` sentinel: the registration resolves it
    # per registered accessor, so the result reads as `jsonbset` rather than the raw BLOB the
    # value travels in. Registering the storage type instead builds green and passes the suite
    # while `asText(jsonbsetExtractPath(...))` answers "No function matches asText(BLOB)".
    return (("set", "MD_SET") if ob == "Set" else ("temporal", "MD_TEMPORAL")) + (tail, sig)

def emit_body_text_array(f, kind, tail, sig):
    name = f["name"]
    L = [f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{",
         "    EnsureMeosThreadInitialized();",
         "    auto rc = args.size();",
         "    auto &lst = args.data[1]; lst.Flatten(rc);",
         "    auto &child = ListVector::GetEntry(lst);",
         "    child.Flatten(ListVector::GetListSize(lst));"]
    call, frees = _emit_tail_consts(L, tail)
    tailargs = "".join(", " + a for a in call)
    # The operand and result marshal through the family the shape resolved: a set reads with
    # BlobToSet and answers through SetToBlobN, a temporal with BlobToTemporal/TemporalToBlobN.
    ctype, blob_in, blob_out = (("Set", "BlobToSet", "SetToBlobN") if kind == "set"
                                else ("Temporal", "BlobToTemporal", "TemporalToBlobN"))
    L += ["    BinaryExecutor::ExecuteWithNulls<string_t, list_entry_t, string_t>("
          "args.data[0], lst, result, rc,",
          "        [&](string_t in, list_entry_t le, ValidityMask &mask, idx_t idx) -> string_t {",
          f"            {ctype} *t = {blob_in}(in);",
          "            int n = 0;",
          "            text **elems = ListToTextArr(child, le, &n);",
          f"            {ctype} *r = {name}(t, elems, n{tailargs});",
          "            free(t); FreeTextArr(elems, n);",
          f"            return {blob_out}(result, r, mask, idx);",
          "        });"]
    return _close_body(L, frees)

def shape_outparam(f):
    """(Temporal *, [varying], <settings...>) with a trailing out-parameter -> that value.

    MEOS hands an owned result back through a trailing pointer beside a bool saying whether
    there is one, so the SQL surface is the value and the false answer is SQL NULL.

    Gated on retired(f): the hand layer registers these accessors today, so the generated form
    appears exactly where the hand one is deleted and the two never both register a name."""
    if not retired(f): return None
    if supported(f) is not None: return None
    ins, out = classify(f)
    if out is None or not ins: return None
    rt = ret_type(f, out)
    if rt is None: return None
    dret, kind = rt
    if kind not in ("outprim", "outptr"): return None
    if base(ins[0]["canonical"]) != "Temporal" or not norm(ins[0]["canonical"]).endswith("*"):
        return None
    bound = ((f.get("shape") or {}).get("boundArgs") or {})
    rest, v = ins[1:], None
    if rest and rest[0].get("name") not in bound:
        v = _vary(rest[0])
        if v is not None: rest = rest[1:]
    tail = _build_tail(rest, bound)
    if tail is None: return None
    if reg_scope(f["name"]) is None: return None
    sig = ([v[0]] if v else []) + _tail_sig(tail)
    return ("outparam", dret, kind, v, tail, sig, out)

def shape_cont_outparam(f):
    """([argument marshals], registration type, out local, executor type) for a retired group's
    set, span or span set followed by by-value arguments, handing its value back through a
    trailing scalar out-parameter (valueN(intset, integer), dateN(datespanset, integer)): the
    container counterpart of #shape_outparam, the false answer SQL NULL. None otherwise."""
    if not retired(f) or f.get("api") != "public" or not f.get("sqlfn"):
        return None
    if supported(f) is not None:
        return None
    ins, out = classify(f)
    if out is None or not ins or cont_out_ret4(f, out) is None:
        return None
    if base(ins[0]["canonical"]) not in CONT_BLOBTO or not norm(ins[0]["canonical"]).endswith("*"):
        return None
    tail = [shape_arg(p) for p in ins[1:]]
    if None in tail:
        return None
    dt, cloc, cpp, _give = cont_out_ret4(f, out)
    return (tail, dt, cloc, cpp, out)

def cont_out_ret4(f, out):
    """(registration LogicalType, out local C type, executor type, C++ expression giving the value
    in `o`) for the out-parameter of #shape_cont_outparam, or None: ONE decision both the shape and
    #emit_cont_outparam read, as #outprim_ret3 is for a scalar. A scalar is its #outprim_ret3
    triple; the base value of another family (valueN(cbufferset, integer) -> Cbuffer **) is the
    pointer #emit_body_outparam's outptr arm hands back, marshalled and freed by PTR_RET."""
    n = norm(out)
    if n in OUTPRIM:
        dt, cloc, cpp = outprim_ret3(f, out)
        # a cell id handed back (valueN(quadbinset, integer)) is the cell type its signature
        # declares, BIGINT-backed, as #shape_static_cell types the start and end values
        if base(out) in CELL_UINT and f.get("sqlReturnType") in CELL_SQL:
            dt = CELL_SQL[f["sqlReturnType"]]
        return (dt, cloc, cpp, OUTPARAM_TAKE.get(n, f"({cpp}) %s") % "o")
    ob = outptr_base(out)
    if ob in BASEVAL_PTR_RET:
        return (PTR_RET[ob][0], f"{ob} *", "string_t", PTR_RET[ob][1] % "o")
    # an owned geometry handed back (valueN(geomset, integer)), written as the GEOMETRY a
    # geometry result is (#static_cell_ret) and freed by #emit_cont_outparam
    if n == "GSERIALIZED **":
        return ("MobilityDuckGeometryType()", "GSERIALIZED *", "string_t",
                "GSerializedToGeometry(o, state, result)")
    return None

def emit_cont_outparam(f, tail, cloc, cpp, out):
    """The per-row body of #shape_cont_outparam, read as #emit_nary_scalars reads its operands,
    the value returned as #emit_body_outparam returns an out-parameter (OUTPARAM_TAKE)."""
    name = f["name"]; n = 1 + len(tail)
    cb = base(classify(f)[0][0]["canonical"])
    give = cont_out_ret4(f, out)[3]
    geo = norm(out) == "GSERIALIZED **"
    state = "state" if geo else ""
    L = [f"static void Gen_{name}(DataChunk &args, ExpressionState &{state}, Vector &result) {{",
         "    EnsureMeosThreadInitialized();",
         "    const idx_t rows = args.size();",
         f"    for (idx_t c = 0; c < {n}; c++) args.data[c].Flatten(rows);",
         "    auto in0 = FlatVector::GetData<string_t>(args.data[0]);"]
    for k, (_dt, c, _m) in enumerate(tail, start=1):
        L.append(f"    auto in{k} = FlatVector::GetData<{c}>(args.data[{k}]);")
    L += [f"    auto out = FlatVector::GetData<{cpp}>(result);",
          "    auto &outv = FlatVector::Validity(result);",
          "    for (idx_t i = 0; i < rows; i++) {",
          "        bool valid = true;",
          f"        for (idx_t c = 0; c < {n}; c++)",
          "            if (!FlatVector::Validity(args.data[c]).RowIsValid(i)) { valid = false; break; }",
          "        if (!valid) { outv.SetInvalid(i); continue; }",
          f"        {cb} *x0 = {CONT_BLOBTO[cb]}(in0[i]);",
          f"        {cloc} o = ({cloc}) 0;"]
    call = ", ".join(["x0"] + [m.replace("a2", f"in{k}[i]") for k, (_d, _c, m) in enumerate(tail, start=1)]
                     + ["&o"])
    L += [f"        bool ok = {name}({call});",
          "        free(x0);",
          "        if (!ok) { outv.SetInvalid(i); continue; }",
          f"        out[i] = {give};"]
    if geo:
        L.append("        free(o);")
    L += ["    }",
          "    if (rows == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);",
          "}", ""]
    return "\n".join(L)

def emit_body_outparam(f, dret, kind, vary, tail, sig, out_canon):
    name = f["name"]
    L = [f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{",
         "    EnsureMeosThreadInitialized();",
         "    auto rc = args.size();"]
    call, frees = _emit_tail_consts(L, tail, col=(2 if vary else 1))
    tailargs = "".join(", " + a for a in call)
    if kind == "outptr":
        ob = base(out_canon) if base(out_canon) in PTR_RET else outptr_base(out_canon)
        decl, rett = f"{ob} *o = NULL;", "string_t"
        give, empty = "return %s;" % (PTR_RET[ob][1] % "o"), "return string_t();"
    else:
        _dt, cloc, cpp = outprim_ret3(f, out_canon)   # paired with ret_type by construction
        decl, rett = f"{cloc} o = ({cloc}) 0;", cpp
        give = "return %s;" % (OUTPARAM_TAKE.get(norm(out_canon), f"({cpp}) %s") % "o")
        empty = f"return {cpp}();"
    if vary:
        _dt, cpp2, expr2, cleanup = vary
        lead = expr2 % "a2"
        L += [f"    BinaryExecutor::ExecuteWithNulls<string_t, {cpp2}, {rett}>("
              "args.data[0], args.data[1], result, rc,",
              f"        [&](string_t in, {cpp2} a2, ValidityMask &mask, idx_t idx) -> {rett} {{"]
    else:
        lead, cleanup = None, ""
        L += [f"    UnaryExecutor::ExecuteWithNulls<string_t, {rett}>(args.data[0], result, rc,",
              f"        [&](string_t in, ValidityMask &mask, idx_t idx) -> {rett} {{"]
    L += ["            Temporal *t = BlobToTemporal(in);", f"            {decl}"]
    if vary and cleanup:
        L += [f"            auto p2 = {lead};",
              f"            bool ok = {name}(t, p2{tailargs}, &o);",
              f"            free(t); {cleanup}(p2);"]
    elif vary:
        L += [f"            bool ok = {name}(t, {lead}{tailargs}, &o);", "            free(t);"]
    else:
        L += [f"            bool ok = {name}(t{tailargs}, &o);", "            free(t);"]
    L += [f"            if (! ok) {{ mask.SetInvalid(idx); {empty} }}",
          f"            {give}", "        });"]
    return _close_body(L, frees)

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
    # A retired group's pair also answers an instant (nearestApproachInstant) and an owned
    # geometry (shortestLine), as #emit_body answers them for one temporal operand
    if retired(f) and rn.endswith("*") and "const" not in f["returnType"]["canonical"]:
        if rb == "TInstant":
            return ("temporal", "MD_TEMPORAL")
        if rb == "GSERIALIZED":
            return ("geo", "MobilityDuckGeometryType()")
    return None

def emit_binary_tt(f, kind, rev=False, dflt=None):
    """The body of a temporal pair. `rev` reads the operands off the vectors in the commuted
    order a declared signature states (its sqlArgParams reverse the C parameters), emitted as
    `Gen_<fn>_rev` beside `Gen_<fn>`; both make the same call. `dflt` is the C++ literal of a
    trailing SQL default, the shorter call of a pair with a third argument (insert(tint, tint)
    for insert(tint, tint, connect boolean DEFAULT TRUE)), emitted as `Gen_<fn>_d` as
    #emit_defaulted_ternary emits the shorter call of a ternary."""
    name = f["name"]
    sfx = "_rev" if rev else ("_d" if dflt is not None else "")
    tail = "" if dflt is None else ", " + dflt
    call = f"{name}(t2, t1{tail})" if rev else f"{name}(t1, t2{tail})"
    if kind == "temporal":          # (Temporal,Temporal) -> Temporal (NULL-safe pointer return)
        return (f"static void Gen_{name}{sfx}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t in1, string_t in2, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            Temporal *t1 = BlobToTemporal(in1);\n            Temporal *t2 = BlobToTemporal(in2);\n"
                f"            Temporal *r = (Temporal *) {call};\n            free(t1); free(t2);\n"
                f"            return TemporalToBlobN(result, r, mask, idx);\n        }});\n}}\n")
    if kind == "geo":               # (Temporal,Temporal) -> owned GSERIALIZED -> DuckDB GEOMETRY
        return (f"static void Gen_{name}{sfx}(DataChunk &args, ExpressionState &state, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t in1, string_t in2, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            Temporal *t1 = BlobToTemporal(in1);\n            Temporal *t2 = BlobToTemporal(in2);\n"
                f"            GSERIALIZED *r = {call};\n            free(t1); free(t2);\n"
                f"            if (!r) {{ mask.SetInvalid(idx); return string_t(); }}\n"
                f"            string_t out = GSerializedToGeometry(r, state, result);\n"
                f"            free(r);\n"
                f"            return out;\n        }});\n}}\n")
    ctype, rett, _rx = scalar_emit3(f)
    ex, prm, ann, ret = scalar_exec(f, rett, _rx)
    inner = f"{ctype} r = {call};\n            free(t1); free(t2);\n            {ret}"
    return (f"static void Gen_{name}{sfx}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    BinaryExecutor::{ex}<string_t, string_t, {rett}>(args.data[0], args.data[1], result, args.size(),\n"
            f"        [&](string_t in1, string_t in2{prm}){ann} {{\n"
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
    if "*" in norm(ins[2]["canonical"]): return None
    # a retired group's third argument may also be an enum read from its name (#shape_arg)
    a3 = SCALAR_ARG.get(b3) or (shape_arg(ins[2]) if retired(f) and b3 in ENUM_READER else None)
    if a3 is None: return None
    if reg_scope(f["name"]) is None: return None
    rb = base(f["returnType"]["canonical"]); rn = norm(f["returnType"]["canonical"])
    if rb == "Temporal" and rn.endswith("*"):
        return ("temporal", "MD_TEMPORAL", a3)
    if rb in BYVAL_RET and "*" not in rn:
        return ("scalar:" + rb, scalar_ret_duck(f), a3)
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
    ex, prm, ann, ret = scalar_exec(f, rett, _rx)
    inner = f"{ctype} r = {name}(t1, t2, {e3});\n            free(t1); free(t2);\n            {ret}"
    return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    TernaryExecutor::{ex}<string_t, string_t, {cpp3}, {rett}>("
            f"args.data[0], args.data[1], args.data[2], result, args.size(),\n"
            f"        [&](string_t in1, string_t in2, {cpp3} a2{prm}){ann} {{\n"
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
    # The SQL operand order, when the catalog states one, OVERRIDES the C parameter order: the
    # wrapper, not the kernel, decides what the surface looks like (sig_geo_first).
    geo_first = (bs[0] == "GSERIALIZED")
    _declared_first = sig_geo_first(f)
    if _declared_first is not None:
        geo_first = _declared_first
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
    # A retired group's geometry pair also answers a distance (nearestApproachDistance), an
    # instant (nearestApproachInstant) and an owned geometry (shortestLine)
    if retired(f):
        if rb == "double" and "*" not in rn:
            return ("double", scalar_ret_duck(f), geo_first, has_dbl)
        if rn.endswith("*") and "const" not in f["returnType"]["canonical"]:
            if rb == "TInstant":
                return ("temporal", "MD_TEMPORAL", geo_first, has_dbl)
            if rb == "GSERIALIZED":
                return ("geo", "MobilityDuckGeometryType()", geo_first, has_dbl)
    return None

def emit_geo_temporal(f, kind, geo_first, has_dbl, suffix=""):
    name = f["name"]
    # TWO INDEPENDENT ORDERS. `geo_first` is the SQL one — which vector the registration binds to
    # which operand, so the lambda reads its arguments in it. The MEOS call keeps the KERNEL's own
    # parameter order, a separate fact and the only one the C signature admits: where the wrapper
    # swaps (tpose_apply_geo takes the temporal first and backs applyPose(geometry, tpose)) the two
    # differ, and driving both from one flag passes a GSERIALIZED * where a Temporal * is expected.
    ins, _out = classify(f)
    c_geo_first = base(ins[0]["canonical"]) == "GSERIALIZED"
    decl = "string_t in_g, string_t in_t" if geo_first else "string_t in_t, string_t in_g"
    call_args = "gs, t" if c_geo_first else "t, gs"
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
    state = ""
    if kind == "temporal":    # -> Temporal tbool (NULL-safe pointer return)
        head = exec_head.format(ret="string_t"); rett = "string_t"
        body = (f"            Temporal *r = (Temporal *) {call};\n{freeing}"
                f"            return TemporalToBlobN(result, r, mask, idx);\n")
    elif kind == "geo":       # -> owned GSERIALIZED -> DuckDB GEOMETRY, as #emit_body marshals it
        head = exec_head.format(ret="string_t"); rett = "string_t"; state = "state"
        body = (f"            GSERIALIZED *r = {call};\n{freeing}"
                f"            if (!r) {{ mask.SetInvalid(idx); return string_t(); }}\n"
                f"            string_t out = GSerializedToGeometry(r, state, result);\n"
                f"            free(r);\n"
                f"            return out;\n")
    elif kind == "double":    # -> double, NULL on the guard the catalog states (#scalar_exec)
        ctype, rett, rexpr = scalar_emit3(f)
        head = exec_head.format(ret=rett)
        guard = SCALAR_NULL_GUARD.get((f.get("shape") or {}).get("nullableResult"))
        test = (f"            if ({guard}) {{ mask.SetInvalid(idx); return {rett}(); }}\n"
                if guard else "")
        body = f"            {ctype} r = {call};\n{freeing}{test}            return {rexpr};\n"
    else:                     # ea int -> nullable BOOLEAN (hand semantics: ret<0 -> SQL NULL)
        head = exec_head.format(ret="bool"); rett = "bool"
        body = (f"            int r = {call};\n{freeing}"
                f"            if (r < 0) {{ mask.SetInvalid(idx); return false; }}\n"
                f"            return r != 0;\n")
    return (f"static void Gen_{name}{suffix}(DataChunk &args, ExpressionState &{state}, Vector &result) {{\n"
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
f"static unique_ptr<GlobalTableFunctionState> PathInit_{name}(ClientContext &context, TableFunctionInitInput &input) {{\n"
f"    EnsureMeosThreadInitialized(context);\n"
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
    # An owned temporal (the copies temporal_instants/temporal_sequences/temporal_segments hand
    # back) is stored and freed as a temporal result is (TemporalToBlob); the LIST's logical
    # element is the temporal type the signature declares (array_ret_duck).
    "TInstant *":  ("LogicalType::BLOB", "string_t",
                    "cd[off + j] = TemporalToBlob(child_vector, (Temporal *) arr[j]);"),
    "TSequence *": ("LogicalType::BLOB", "string_t",
                    "cd[off + j] = TemporalToBlob(child_vector, (Temporal *) arr[j]);"),
    # and the temporal values makeSimple hands back, its pieces of any subtype
    "Temporal *":  ("LogicalType::BLOB", "string_t",
                    "cd[off + j] = TemporalToBlob(child_vector, arr[j]);"),
}
# and the base values of the other families a set of them hands back (getValues(cbufferset) ->
# Cbuffer **), each stored and freed by the marshaller PTR_RET states for it, as the owned
# temporal elements above are by TemporalToBlob
ARRAY_ELEM.update({
    f"{b} *": ("LogicalType::BLOB", "string_t",
               f"cd[off + j] = {PTR_RET[b][1].split('(', 1)[0]}(child_vector, arr[j]);")
    for b in sorted(BASEVAL_PTR_RET)})
# container-input family (base of the single non-count param) -> (C container type, Blob->container fn)
ARRAY_IN = {
    "Set":          ("Set",      "BlobToSet"),
    "Span":         ("Span",     "BlobToSpan"),
    "SpanSet":      ("SpanSet",  "BlobToSpanSet"),
    "Temporal":     ("Temporal", "BlobToTemporal"),
    "TInstant":     ("Temporal", "BlobToTemporal"),
    "TSequence":    ("Temporal", "BlobToTemporal"),
    "TSequenceSet": ("Temporal", "BlobToTemporal"),
    "GSERIALIZED":  ("GSERIALIZED", "GeometryToGSerialized"),     # read with SRID 0 (#emit_array)
    # a box of a retired group (quadSplit(stbox) -> stbox[]), scoped by #box_reg_scope
    "STBox":        ("STBox",    "BlobToStbox"),
    "TBox":         ("TBox",     "BlobToTbox"),
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
    ex, prm, _ann, ret = scalar_exec(f, "double", "r")
    return (
f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
f"    EnsureMeosThreadInitialized();\n"
f"    auto &lv1 = args.data[0]; auto &lv2 = args.data[1];\n"
f"    auto &child1 = ListVector::GetEntry(lv1); child1.Flatten(ListVector::GetListSize(lv1));\n"
f"    auto &child2 = ListVector::GetEntry(lv2); child2.Flatten(ListVector::GetListSize(lv2));\n"
f"    BinaryExecutor::{ex}<list_entry_t, list_entry_t, double>(\n"
f"        lv1, lv2, result, args.size(),\n"
f"        [&](list_entry_t le1, list_entry_t le2{prm}) -> double {{\n"
f"            int n1, n2;\n"
f"            const Temporal **a1 = ListToTemporalArr(child1, le1, &n1);\n"
f"            const Temporal **a2 = ListToTemporalArr(child2, le2, &n2);\n"
f"            double r = {name}(a1, n1, a2, n2);\n"
f"            FreeTemporalArr(a1, n1); FreeTemporalArr(a2, n2);\n"
f"            {ret}\n"
f"        }});\n}}\n")

def shape_temporal_array(f):
    """The temporal types of the arrays MobilityDB declares for a retired group's
    (Temporal **arr, int count) -> Temporal * function (merge(tint[]), the catalog's inputArrays
    naming the array and its count), each a LIST of temporal blobs read as #emit_tgeoarr_scalar
    reads its two; an element type a type's own kernel declares (trgeometry_merge_array) is left
    to it, as #yield_to_type_kernels leaves a scalar signature. None when f is no such function."""
    if not retired(f) or f.get("api") != "public" or not f.get("sqlfn"):
        return None
    ins, out = classify(f)
    ia = (f.get("shape") or {}).get("inputArrays") or []
    if out is not None or len(ins) != 2 or len(ia) != 1:
        return None
    if norm(ins[0]["canonical"]) != "Temporal **" or ia[0].get("param") != ins[0]["name"]:
        return None
    if (ia[0].get("lengthFrom") or {}).get("name") != ins[1]["name"]:
        return None
    rb, rn = base(f["returnType"]["canonical"]), norm(f["returnType"]["canonical"])
    if rb != "Temporal" or not rn.endswith("*") or reg_scope(f["name"]) is None:
        return None
    def elems(g):
        return {a[:-2] for s in g.get("sqlSignatures") or [] for a in (s.get("args") or [])[:1]
                if a.endswith("[]") and a[:-2] in SIG_TEMPORAL_ACC}
    own = set()
    if f["name"].startswith(GENERIC_PREFIXES):
        for g in FN_BY_NAME.values():
            if (g is not f and g.get("api") == "public" and g.get("sqlfn") == f["sqlfn"]
                    and not g["name"].startswith(GENERIC_PREFIXES)):
                own |= elems(g)
    have = elems(f) - own
    return [t for t in SIG_TEMPORAL_ACC if t in have] or None

def emit_temporal_array(f):
    """DuckDB scalar over one LIST(temporal-blob) -> a temporal value (merge(tint[])): the LIST row
    is marshalled to a fresh array by ListToTemporalArr, as #emit_tgeoarr_scalar marshals its two,
    the MEOS kernel called and the array freed."""
    name = f["name"]
    return (
f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
f"    EnsureMeosThreadInitialized();\n"
f"    auto &lv = args.data[0];\n"
f"    auto &child = ListVector::GetEntry(lv); child.Flatten(ListVector::GetListSize(lv));\n"
f"    UnaryExecutor::ExecuteWithNulls<list_entry_t, string_t>(lv, result, args.size(),\n"
f"        [&](list_entry_t le, ValidityMask &mask, idx_t idx) -> string_t {{\n"
f"            int n;\n"
f"            const Temporal **a = ListToTemporalArr(child, le, &n);\n"
f"            Temporal *r = {name}((Temporal **) a, n);\n"
f"            FreeTemporalArr(a, n);\n"
f"            return TemporalToBlobN(result, r, mask, idx);\n"
f"        }});\n}}\n")

def ctor_duck_type(sqlt):
    """The DuckDB type of an argument or the return of a container constructor: a base value or a
    span (SQL_BASE_TO_DUCK), a set or a span set, or a LIST of one."""
    if sqlt.endswith("[]"):
        inner = ctor_duck_type(sqlt[:-2])
        return "LogicalType::LIST(%s)" % inner if inner else None
    # a base value of another family (cbuffer), a cell (h3index), a geometry or a geography (both
    # a DuckDB GEOMETRY), and a geometry set
    return (SQL_BASE_TO_DUCK.get(sqlt) or SET_TYPES.get(sqlt) or SPANSET_TYPES.get(sqlt)
            or GEO_SET_TYPES.get(sqlt) or BASE_LOGICAL.get(sqlt) or CELL_SQL.get(sqlt)
            or ("MobilityDuckGeometryType()" if sqlt in ("geometry", "geography") else None))

def span_operand(f, p, k):
    """The #shape_arg triple of a span operand at argument k, typed as #scalar_arg_for types an
    integer, by the one SQL type every signature declares there (tbox(integer, tstzspan),
    stbox(geometry, tstzspan)); None for any other operand or for signatures that disagree. The
    marshal decodes the blob into a span the emitter frees after the call (#hoist_span_operands)."""
    if base(p["canonical"]) != "Span" or norm(p["canonical"]).count("*") != 1:
        return None
    decl = {(s.get("args") or [])[k] if k < len(s.get("args") or []) else None
            for s in f.get("sqlSignatures") or []}
    if len(decl) != 1:
        return None
    d = decl.pop()
    return (SPAN_TYPES[d], "string_t", "BlobToSpan(a2)") if d in SPAN_TYPES else None

def hoist_span_operands(args, indent):
    """The arguments of a call with each span operand decoded into a local and freed after the
    call, as #emit_nary_scalars decodes and frees its first operand x0: (the lines declaring the
    locals, the arguments, the lines freeing them). `args` holds each argument's marshal
    expression already read at the row."""
    decl, call, free_ = [], [], []
    # a geometry and a temporal value are decoded and freed the same way (#shape_nary_scalars)
    owned = (("BlobToSpan(", "Span"), ("GeometryToGSerialized(", "GSERIALIZED"),
             ("BlobToTemporal(", "Temporal"))
    for k, a in enumerate(args):
        ct = next((c for pre, c in owned if a.startswith(pre)), None)
        if ct is not None:
            decl.append(f"{indent}{ct} *p{k} = {a};")
            call.append(f"p{k}")
            free_.append(f"{indent}free(p{k});")
        else:
            call.append(a)
    return decl, call, free_

def shape_container_make(f):
    """A retired group's public constructor of a set, a span, a span set or a box: ('scalars', [the
    #shape_arg triple of each parameter], None, ret) for one taking by-value values and durations
    only (intspan_make, the bin of a value tstzspan_get_bin, the tile of a value tbox_get_value_tile),
    ('basearr', C element, (DuckDB type, C++ element, marshal), ret) for one taking the base values
    of a SQL array with their count (intset_make, textset_make), ('spanarr', 'Span', None, ret) for
    one taking spans (spanset_make). The array is the catalog's `inputArrays`, read from a LIST as
    #shape_temporal_array reads one; None otherwise."""
    if not retired(f) or f.get("api") != "public" or not f.get("sqlfn"):
        return None
    rb, rn = base(f["returnType"]["canonical"]), norm(f["returnType"]["canonical"])
    if rb not in ("Set", "Span", "SpanSet", "TBox", "STBox") or not rn.endswith("*") \
            or "const" in f["returnType"]["canonical"]:
        return None
    ps = f.get("params") or []
    ia = (f.get("shape") or {}).get("inputArrays") or []
    if not ia:
        # a by-value value, or a duration read as #shape_arg reads one, and past the first a span
        # (tbox(integer, tstzspan)) as #span_operand reads one
        def _value(p):
            b, n = base(p["canonical"]), norm(p["canonical"])
            return (b in SCALAR_ARG and "*" not in n) or (b == "Interval" and n.count("*") == 1)
        if not ps or not _value(ps[0]):
            return None
        # a cell id (h3index_to_set) is read from the int64_t vector of its BIGINT-backed cell
        # type, as #scalar_arg_for reads one
        cell0 = base(ps[0]["canonical"]) in CELL_UINT and "*" not in norm(ps[0]["canonical"])
        info = [("LogicalType::BIGINT", "int64_t", "(uint64_t) a2") if cell0 else shape_arg(ps[0])]
        for k, p in enumerate(ps[1:], start=1):
            t = shape_arg(p) if _value(p) else span_operand(f, p, k)
            if t is None:
                return None
            info.append(t)
        # every signature names one argument per parameter: a constructor whose parameters the
        # catalog binds (stbox_make behind stboxX) is not read argument by argument
        if any(len(s.get("args") or []) != len(ps) for s in f.get("sqlSignatures") or []):
            return None
        return ("scalars", info, None, rb)
    if rb in ("TBox", "STBox"):
        return None
    if len(ia) != 1 or len(ps) != 2 or ia[0].get("param") != ps[0]["name"] \
            or (ia[0].get("lengthFrom") or {}).get("name") != ps[1]["name"]:
        return None
    n0 = norm(ps[0]["canonical"]).replace("const ", "").strip()
    if n0 == "text **":
        return ("basearr", "text *", ("LogicalType::VARCHAR", "string_t", "MakeText(a2)"), rb)
    b0 = base(ps[0]["canonical"])
    # the base values of another family or the geometries of a SQL array, as pointers each read
    # from its blob (jsonbset_make, geoset_make), or as the fixed-size structs a blob copies
    # (cbufferset_make, npointset_make)
    if n0.endswith("**"):
        b2 = base(n0[:-1])
        if b2 in BASEVAL_PTR_IN or b2 == "GSERIALIZED":
            return ("ptrarr", b2, None, rb)
        return None
    if n0.count("*") != 1:
        return None
    if b0 == "Span":
        return ("spanarr", "Span", None, rb)
    if b0 in BASEVAL_PTR_IN:
        return ("structarr", b0, None, rb)
    # a cell id (h3indexset_make), whose cell type is a BIGINT alias: its vector holds int64_t,
    # the bits of the uint64 id, as #scalar_arg_for reads one
    if b0 in CELL_UINT:
        return ("basearr", b0, ("LogicalType::BIGINT", "int64_t", "(uint64_t) a2"), rb)
    if b0 in SCALAR_ARG:
        return ("basearr", b0, SCALAR_ARG[b0], rb)
    return None

def emit_value_array_make(f, kind, b0, rb):
    """The body of a set built from a SQL array of base values of another family or of
    geometries (#shape_container_make's ptrarr and structarr): each element read from its blob as
    #base_operand reads one (BlobToCbuffer, GeometryToGSerialized), into an array of pointers or
    of the structs they point to, the array and its elements freed after the call. A NULL element
    raises PostgreSQL's message for it, as the other arrays do."""
    name = f["name"]
    writer = PTR_RET[rb][1] % "r"
    read = ("GeometryToGSerialized(cd[le.offset + j], 0)" if b0 == "GSERIALIZED"
            else PTR_IN[b0][1] % "cd[le.offset + j]")
    if kind == "ptrarr":
        decl = f"{b0} **arr = ({b0} **) malloc(sizeof({b0} *) * (n > 0 ? n : 1));"
        put = f"arr[j] = {read};"
        release = "for (int j = 0; j < got; j++) free(arr[j]); "
        cast = "(%s) " % f["params"][0]["canonical"]
    else:
        decl = f"{b0} *arr = ({b0} *) malloc(sizeof({b0}) * (n > 0 ? n : 1));"
        put = f"{b0} *v = {read};\n                arr[j] = *v;\n                free(v);"
        release = ""
        cast = ""
    return (
f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
f"    EnsureMeosThreadInitialized();\n"
f"    auto &lv = args.data[0];\n"
f"    auto &child = ListVector::GetEntry(lv); child.Flatten(ListVector::GetListSize(lv));\n"
f"    auto cd = FlatVector::GetData<string_t>(child);\n"
f"    auto &cv = FlatVector::Validity(child);\n"
f"    UnaryExecutor::ExecuteWithNulls<list_entry_t, string_t>(lv, result, args.size(),\n"
f"        [&](list_entry_t le, ValidityMask &mask, idx_t idx) -> string_t {{\n"
f"            int n = (int) le.length, got = 0;\n"
f"            {decl}\n"
f"            for (int j = 0; j < n; j++, got++) {{\n"
f"                if (!cv.RowIsValid(le.offset + j)) {{\n"
f"                    {release}free(arr);\n"
f"                    throw InvalidInputException(\"null array element not allowed in this context\");\n"
f"                }}\n"
f"                {put}\n"
f"            }}\n"
f"            {rb} *r = {name}({cast}arr, n);\n"
f"            {release}free(arr);\n"
f"            if (!r) {{ mask.SetInvalid(idx); return string_t(); }}\n"
f"            return {writer};\n"
f"        }});\n}}\n")

def ctor_forms(f, s, kind):
    """The (number of SQL arguments read, body suffix) forms a signature registers: the full one and,
    for by-value values whose last argument carries a SQL default, the form without it
    (span(integer, integer, boolean) beside the four-argument form). A defaulted tail of two or more
    arguments is a function of its own, which #project_signature_forms derives
    (intspan_make_lower_inc_upper_inc for span(integer, integer)), so it is not repeated here."""
    n = len(s.get("args") or [])
    forms = [(n, "")]
    defs = s.get("argDefaults") or []
    if kind == "scalars" and len(defs) == n and n and defs[n - 1] is not None:
        forms.append((n - 1, "_a%d" % (n - 1)))
    return forms

def emit_container_make(f, kind, info, rb, k=None):
    """The body of #shape_container_make. The by-value form reads every argument per row as
    #emit_nary_scalars does, and passes the SQL default of each argument past the first `k`. The
    array forms read a LIST as #emit_temporal_array does into an array of the C element: a base
    value through its SCALAR_ARG marshal or MakeText, a span copied from its blob as the hand
    constructor copied one. A NULL element raises PostgreSQL's own message for it
    (deconstruct_array); MEOS refuses an empty array (ensure_positive)."""
    name = f["name"]
    writer = PTR_RET[rb][1] % "r"
    if kind == "scalars":
        ps = f["params"]
        n = len(ps)
        k = n if k is None else k
        sfx = "" if k == n else "_a%d" % k
        defs = next((s.get("argDefaults") for s in f.get("sqlSignatures") or []
                     if s.get("argDefaults")), None) or [None] * n
        L = [f"static void Gen_{name}{sfx}(DataChunk &args, ExpressionState &, Vector &result) {{",
             "    EnsureMeosThreadInitialized();",
             "    const idx_t rows = args.size();",
             f"    for (idx_t c = 0; c < {k}; c++) args.data[c].Flatten(rows);"]
        for c in range(k):
            L.append(f"    auto in{c} = FlatVector::GetData<{info[c][1]}>(args.data[{c}]);")
        L += ["    auto out = FlatVector::GetData<string_t>(result);",
              "    auto &outv = FlatVector::Validity(result);",
              "    for (idx_t i = 0; i < rows; i++) {",
              "        bool valid = true;",
              f"        for (idx_t c = 0; c < {k}; c++)",
              "            if (!FlatVector::Validity(args.data[c]).RowIsValid(i)) { valid = false; break; }",
              "        if (!valid) { outv.SetInvalid(i); continue; }"]
        call = [info[c][2].replace("a2", f"in{c}[i]") for c in range(k)]
        call += [forward_literal(ps[c], defs[c]) for c in range(k, n)]
        decl, call, free_ = hoist_span_operands(call, "        ")
        L += decl
        L += [f"        {rb} *r = {name}({', '.join(call)});"] + free_
        L += ["        if (!r) { outv.SetInvalid(i); continue; }",
              f"        out[i] = {writer};",
              "    }",
              "}"]
        return "\n".join(L) + "\n"
    if kind in ("ptrarr", "structarr"):
        return emit_value_array_make(f, kind, info[0], rb)
    elem, conv = info if kind == "basearr" else ("Span", None)
    cpp = "string_t" if kind == "spanarr" else conv[1]
    if kind == "spanarr":
        put = ("if (a2.GetSize() != sizeof(Span)) { free(arr); "
               "throw InvalidInputException(\"Invalid span in the input array\"); }\n"
               "                memcpy(&arr[j], a2.GetData(), sizeof(Span));")
    else:
        put = f"arr[j] = {conv[2]};"
    free_elems = ("for (int j = 0; j < got; j++) free(arr[j]); " if elem == "text *" else "")
    return (
f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
f"    EnsureMeosThreadInitialized();\n"
f"    auto &lv = args.data[0];\n"
f"    auto &child = ListVector::GetEntry(lv); child.Flatten(ListVector::GetListSize(lv));\n"
f"    auto cd = FlatVector::GetData<{cpp}>(child);\n"
f"    auto &cv = FlatVector::Validity(child);\n"
f"    UnaryExecutor::ExecuteWithNulls<list_entry_t, string_t>(lv, result, args.size(),\n"
f"        [&](list_entry_t le, ValidityMask &mask, idx_t idx) -> string_t {{\n"
f"            int n = (int) le.length, got = 0;\n"
f"            {elem} *arr = ({elem} *) malloc(sizeof({elem}) * (n > 0 ? n : 1));\n"
f"            for (int j = 0; j < n; j++, got++) {{\n"
f"                if (!cv.RowIsValid(le.offset + j)) {{\n"
f"                    {free_elems}free(arr);\n"
f"                    throw InvalidInputException(\"null array element not allowed in this context\");\n"
f"                }}\n"
f"                auto a2 = cd[le.offset + j];\n"
f"                {put}\n"
f"            }}\n"
f"            {rb} *r = {name}(arr, n);\n"
f"            {free_elems}free(arr);\n"
f"            if (!r) {{ mask.SetInvalid(idx); return string_t(); }}\n"
f"            return {writer};\n"
f"        }});\n}}\n")

def shape_unnest(f):
    """(engine name, [temporal accessors], value DuckDB type, value writer, C element) for a
    function the catalog declares set-returning under the name the other engines take
    (`altSqlName`, the `@altsqlfn temporalUnnest` of Temporal_unnest):
    `SpanSet **<t>_unnest(const Temporal *, <v> **values, int *count)`, one row per distinct value
    with its time. The writer stores `values[k]` into the value column the way the scalar, text,
    geometry and base-value returns store theirs (TakeText, GSerializedToGeometry,
    <Base>ToBlob); a cell id as the cellscalar return writes it into its BIGINT-backed cell type.
    None otherwise."""
    if f.get("api") != "public":
        return None
    sigs = [s for s in f.get("sqlSignatures") or [] if s.get("retSet") and s.get("altSqlName")]
    ps = f.get("params") or []
    if not sigs or len(ps) != 3 or norm(f["returnType"]["canonical"]) != "SpanSet **":
        return None
    if base(ps[0]["canonical"]) != "Temporal" or norm(ps[2]["canonical"]) != "int *":
        return None
    names = {s["altSqlName"] for s in sigs}
    if len(names) != 1:
        return None
    vt = re.sub(r"^struct\s+", "", norm(ps[1]["canonical"]))
    if not vt.endswith("**"):
        return None
    elem = vt[:-1].strip()                        # what values points to: int *, text **, ...
    eb = elem.rstrip(" *")                        # base() reads a `T **` as internal
    accs = [SIG_TEMPORAL_ACC[s["args"][0]] for s in sigs if s["args"][0] in SIG_TEMPORAL_ACC]
    if not accs:
        return None
    byval = {"int *": ("int32_t", "LogicalType::INTEGER"), "int64_t *": ("int64_t", "LogicalType::BIGINT"),
             "double *": ("double", "LogicalType::DOUBLE"), "bool *": ("bool", "LogicalType::BOOLEAN")}
    if elem == "uint64_t *":
        if len(accs) != 1 or accs[0] not in CELL_BASEVAL:
            return None
        byval[elem] = ("int64_t", CELL_BASEVAL[accs[0]])
    if elem in byval:
        cpp, dt = byval[elem]
        write = f"FlatVector::GetData<{cpp}>(v0)[i] = ({cpp}) x;"
    elif elem == "text **":
        dt, write = "LogicalType::VARCHAR", "FlatVector::GetData<string_t>(v0)[i] = TakeText(v0, x);"
    elif eb == "GSERIALIZED" and elem.endswith("**"):
        dt = "MobilityDuckGeometryType()"
        write = "FlatVector::GetData<string_t>(v0)[i] = GSerializedToGeometry(x, arena, v0); free(x);"
    elif eb in PTR_RET and elem.endswith("**"):
        dt = PTR_RET[eb][0]
        write = "FlatVector::GetData<string_t>(v0)[i] = %s;" % (PTR_RET[eb][1].replace("result", "v0") % "x")
    else:
        return None
    return (next(iter(names)), accs, dt, write, elem[:-1].strip())

def emit_unnest(f, name, dt, write, ctype):
    """The bind, local state and in-out function of #shape_unnest's table function, after the
    splits of src/temporal/temporal.cpp (TemporalSplitInOut): the temporal value comes from a
    literal or from a LATERAL column, as MobilityDB's unnest takes either; each input row makes
    the one MEOS call when the output reaches it, and its rows are handed out a vector at a time,
    each value and time freed as it is written; the state frees what was never read. A NULL
    temporal value has no row, as the strict unnest of MobilityDB answers none."""
    n = f["name"]
    S = f"Unnest_{n}"
    geo = "GSerializedToGeometry" in write
    lines = [
        f"struct {S}State : public LocalTableFunctionState {{",
        f"    {ctype} *values = nullptr; SpanSet **times = nullptr; int count = 0; int idx = 0;",
        "    idx_t row = 0; bool loaded = false;",
        "    void Release() {",
        "        for (int k = idx; k < count; k++) {",
    ]
    if ctype.endswith("*"):
        lines.append("            free(values[k]);")
    lines += [
        "            free(times[k]);",
        "        }",
        "        free(values); free(times);",
        "        values = nullptr; times = nullptr; count = 0; idx = 0;",
        "    }",
        f"    ~{S}State() {{ Release(); }}",
        "};",
        f"static unique_ptr<FunctionData> {S}BindFn(ClientContext &, TableFunctionBindInput &,",
        "                                          vector<LogicalType> &types, vector<string> &names) {",
        f"    types = {{{dt}, SpansetTypes::tstzspanset()}};",
        "    names = {\"value\", \"time\"};",
        "    return make_uniq<TableFunctionData>();",
        "}",
        f"static unique_ptr<LocalTableFunctionState> {S}InitFn(ExecutionContext &context, TableFunctionInitInput &,",
        "                                          GlobalTableFunctionState *) {",
        "    EnsureMeosThreadInitialized(context.client);",
        f"    return make_uniq<{S}State>();",
        "}",
        f"static OperatorResultType {S}InOutFn(ExecutionContext &, TableFunctionInput &data, DataChunk &input,",
        "                                          DataChunk &output) {",
        f"    auto &st = data.local_state->Cast<{S}State>();",
        "    auto &v0 = output.data[0]; auto &v1 = output.data[1];",
    ]
    if geo:
        lines.append("    ArenaAllocator arena(Allocator::DefaultAllocator());")
    lines += [
        "    idx_t i = 0;",
        "    while (i < STANDARD_VECTOR_SIZE) {",
        "        if (!st.loaded) {",
        "            if (st.row >= input.size()) {",
        "                if (i > 0) {",
        "                    output.SetCardinality(i);",
        "                    return OperatorResultType::HAVE_MORE_OUTPUT;",
        "                }",
        "                st.row = 0;",
        "                return OperatorResultType::NEED_MORE_INPUT;",
        "            }",
        "            UnifiedVectorFormat in;",
        "            input.data[0].ToUnifiedFormat(input.size(), in);",
        "            auto r = in.sel->get_index(st.row);",
        "            if (in.validity.RowIsValid(r)) {",
        "                Temporal *t = BlobToTemporal(UnifiedVectorFormat::GetData<string_t>(in)[r]);",
        f"                st.times = {n}(t, &st.values, &st.count);",
        "                free(t);",
        "                if (!st.times) st.count = 0;",
        "            }",
        "            st.loaded = true;",
        "        }",
        "        if (st.idx >= st.count) {",
        "            st.Release();",
        "            st.loaded = false;",
        "            st.row++;",
        "            continue;",
        "        }",
        f"        {ctype} x = st.values[st.idx];",
        f"        {write}",
        "        SpanSet *ss = st.times[st.idx];",
        "        FlatVector::GetData<string_t>(v1)[i] = StringVector::AddStringOrBlob(v1, (const char *) ss, spanset_mem_size(ss));",
        "        free(ss);",
        "        st.idx++; i++;",
        "    }",
        "    output.SetCardinality(i);",
        "    return OperatorResultType::HAVE_MORE_OUTPUT;",
        "}",
        f"static TableFunction {S}Table(const LogicalType &type) {{",
        f"    TableFunction fn(\"{name}\", {{type}}, nullptr, {S}BindFn, nullptr, {S}InitFn);",
        f"    fn.in_out_function = {S}InOutFn;",
        "    return fn;",
        "}",
        "",
    ]
    return "\n".join(lines)

def shape_set_unnest(f):
    """(engine name, [set accessors], element DuckDB type, element C type, element writer) for a
    function the catalog declares set-returning over a set under the name the other engines take
    (`altSqlName`, the `@altsqlfn setUnnest` of Set_unnest): `<elem> *<set>_values(const Set *,
    int *count)`, one row per value of the set. The writer stores the value `x` into the column
    `v0` the way #emit_array stores an element of getValues (ARRAY_ELEM), and a geometry as
    #shape_unnest stores one. None otherwise."""
    if f.get("api") != "public":
        return None
    ps = f.get("params") or []
    if len(ps) != 2 or base(ps[0]["canonical"]) != "Set" or norm(ps[1]["canonical"]) != "int *":
        return None
    sigs = [s for s in f.get("sqlSignatures") or []
            if s.get("retSet") and s.get("altSqlName") and len(s.get("args") or []) == 1]
    names = {s["altSqlName"] for s in sigs}
    if len(names) != 1:
        return None
    ec = re.sub(r"^struct\s+", "", norm(f["returnType"]["canonical"]))
    if not ec.endswith("*"):
        return None
    ec = ec[:-1].strip()                       # what the array holds: int, text *, Cbuffer *, ...
    if ec == "GSERIALIZED *":
        write = "FlatVector::GetData<string_t>(v0)[i] = GSerializedToGeometry(x, arena, v0); free(x);"
    elif ec in ARRAY_ELEM:
        _lt, ccpp, marsh = ARRAY_ELEM[ec]
        write = (marsh.replace("cd[off + j]", "FlatVector::GetData<%s>(v0)[i]" % ccpp)
                 .replace("arr[j]", "x").replace("child_vector", "v0"))
    else:
        return None
    accs = []
    for s in sigs:
        acc = SET_SIG_ACC.get(s["args"][0])
        dt = ("MobilityDuckGeometryType()" if s["ret"] in ("geometry", "geography")
              else SQL_BASE_TO_DUCK.get(s["ret"]) or BASE_LOGICAL.get(s["ret"]))
        if acc and dt:
            accs.append((acc, dt))
    if not accs or len({dt for _a, dt in accs}) != 1:
        return None
    return (next(iter(names)), [a for a, _dt in accs], accs[0][1], ec, write)

def emit_set_unnest(f, name, dt, ctype, write):
    """The bind, local state and in-out function of #shape_set_unnest's table function, as
    #emit_unnest builds temporalUnnest's: the set comes from a literal or from a LATERAL column,
    each input row makes the one MEOS call when the output reaches it, and its values are handed
    out a vector at a time in the order of the set, each freed as it is written; the state frees
    what was never read. A NULL set has no row, as the strict unnest of MobilityDB answers none.
    The column is named unnest, as PostgreSQL names the column of SELECT * FROM unnest(set)."""
    n = f["name"]
    S = f"SetUnnest_{n}"
    geo = "GSerializedToGeometry" in write
    lines = [
        f"struct {S}State : public LocalTableFunctionState {{",
        f"    {ctype} *values = nullptr; int count = 0; int idx = 0;",
        "    idx_t row = 0; bool loaded = false;",
        "    void Release() {",
    ]
    if ctype.endswith("*"):
        lines += ["        for (int k = idx; k < count; k++) {",
                  "            free(values[k]);",
                  "        }"]
    lines += [
        "        free(values);",
        "        values = nullptr; count = 0; idx = 0;",
        "    }",
        f"    ~{S}State() {{ Release(); }}",
        "};",
        f"static unique_ptr<FunctionData> {S}BindFn(ClientContext &, TableFunctionBindInput &,",
        "                                          vector<LogicalType> &types, vector<string> &names) {",
        f"    types = {{{dt}}};",
        "    names = {\"unnest\"};",
        "    return make_uniq<TableFunctionData>();",
        "}",
        f"static unique_ptr<LocalTableFunctionState> {S}InitFn(ExecutionContext &context, TableFunctionInitInput &,",
        "                                          GlobalTableFunctionState *) {",
        "    EnsureMeosThreadInitialized(context.client);",
        f"    return make_uniq<{S}State>();",
        "}",
        f"static OperatorResultType {S}InOutFn(ExecutionContext &, TableFunctionInput &data, DataChunk &input,",
        "                                          DataChunk &output) {",
        f"    auto &st = data.local_state->Cast<{S}State>();",
        "    auto &v0 = output.data[0];",
    ]
    if geo:
        lines.append("    ArenaAllocator arena(Allocator::DefaultAllocator());")
    lines += [
        "    idx_t i = 0;",
        "    while (i < STANDARD_VECTOR_SIZE) {",
        "        if (!st.loaded) {",
        "            if (st.row >= input.size()) {",
        "                if (i > 0) {",
        "                    output.SetCardinality(i);",
        "                    return OperatorResultType::HAVE_MORE_OUTPUT;",
        "                }",
        "                st.row = 0;",
        "                return OperatorResultType::NEED_MORE_INPUT;",
        "            }",
        "            UnifiedVectorFormat in;",
        "            input.data[0].ToUnifiedFormat(input.size(), in);",
        "            auto r = in.sel->get_index(st.row);",
        "            if (in.validity.RowIsValid(r)) {",
        "                Set *s = BlobToSet(UnifiedVectorFormat::GetData<string_t>(in)[r]);",
        f"                st.values = {n}(s, &st.count);",
        "                free(s);",
        "                if (!st.values) st.count = 0;",
        "            }",
        "            st.loaded = true;",
        "        }",
        "        if (st.idx >= st.count) {",
        "            st.Release();",
        "            st.loaded = false;",
        "            st.row++;",
        "            continue;",
        "        }",
        f"        {ctype} x = st.values[st.idx];",
        f"        {write}",
        "        st.idx++; i++;",
        "    }",
        "    output.SetCardinality(i);",
        "    return OperatorResultType::HAVE_MORE_OUTPUT;",
        "}",
        f"static TableFunction {S}Table(const LogicalType &type) {{",
        f"    TableFunction fn(\"{name}\", {{type}}, nullptr, {S}BindFn, nullptr, {S}InitFn);",
        f"    fn.in_out_function = {S}InOutFn;",
        "    return fn;",
        "}",
        "",
    ]
    return "\n".join(lines)

# ── the tiles of a box: a set-returning function of (index, tile) rows ─────────────────────
# MobilityDB declares timeTiles, valueTiles, valueTimeTiles, spaceTiles and spaceTimeTiles as
# SETOF index_tbox / index_stbox (`CREATE TYPE index_tbox AS (index integer, tile tbox)`); the
# catalog states it per signature (`retSet`, `columns` reading the ordinal and the return), with the
# SQL defaults (`argDefaults`) and the arguments a short form binds (`boundArgs`, the sizes a
# one-size form leaves at 0). PostgreSQL and DuckDB call a set-returning function in FROM
# (Unnesting), so each declared form is a table function whose bind keeps the
# arguments, whose init makes the one MEOS call and whose exec hands the rows out. A literal
# default is read in init, the time origin through
# the MEOS parser in the session time zone as MobilityDB reads it.
TILE_BOX_IN = {"TBox": ("BlobToTbox", "TboxType::tbox()", None),
               "STBox": ("BlobToStbox", "StboxType::stbox()", "srid")}
TILE_BOX_OUT = {"TBox": "TboxToBlob", "STBox": "StboxToBlob"}
# The (name, argument types) of every tile form registered: a form a trailing SQL default
# allows is also a record of #project_signature_forms, and calls the same function with the
# same literals, so it is registered once.
TILE_REGISTERED = set()


def _tile_arg(p):
    """(DuckDB type, C expression over the Value `v`) for a tile argument, or None."""
    b, n = base(p["canonical"]), norm(p["canonical"])
    if b == "Interval" and n.count("*") == 1:
        return ("LogicalType::INTERVAL", "MeosIntervalArg(v.GetValue<interval_t>())")
    if b == "GSERIALIZED" and n.count("*") == 1:
        return ("MobilityDuckGeometryType()", None)
    if b in SCALAR_ARG and "*" not in n:
        dt, cpp, expr = SCALAR_ARG[b]
        return (dt, expr.replace("a2", "v.GetValue<%s>()" % cpp))
    return None


def shape_box_tiles(f):
    """[(sqlSignature, inputs, bound)] for a retired group's public function answering the tiles of
    a box as a set of (index, tile) rows; None otherwise."""
    if not retired(f) or f.get("api") != "public" or not f.get("sqlfn"):
        return None
    ar = (f.get("shape") or {}).get("arrayReturn") or {}
    eb = base((ar.get("element") or {}).get("canonical") or "")
    lf = (ar.get("lengthFrom") or {}).get("name")
    if eb not in TILE_BOX_OUT or not lf:
        return None
    ps = [p for p in f.get("params") or [] if p.get("name") != lf]
    if not ps or base(ps[0]["canonical"]) not in TILE_BOX_IN:
        return None
    forms = []
    for s in f.get("sqlSignatures") or []:
        cols = s.get("columns") or []
        if not s.get("retSet") or [c.get("from") for c in cols] != ["ordinal", "return"]:
            return None
        bound = s.get("boundArgs") or {}
        ins = [p for p in ps if p["name"] not in bound]
        if len(ins) != len(s.get("args") or []):
            return None
        if any(_tile_arg(p) is None for p in ins[1:]):
            return None
        forms.append((s, ins, bound))
    return forms or None


def emit_box_tiles(f, tag, s, ins, bound, k):
    """The bind, init and exec of one form of #shape_box_tiles, reading the first `k` declared
    arguments and the SQL default of the others."""
    name = f["name"]
    ps = [p for p in f["params"] if p.get("name") != f["shape"]["arrayReturn"]["lengthFrom"]["name"]]
    b0 = base(ps[0]["canonical"])
    blobto, _acc, srid = TILE_BOX_IN[b0]
    eb = base(f["shape"]["arrayReturn"]["element"]["canonical"])
    S = f"Tiles_{name}_{tag}"
    defs = s.get("argDefaults") or [None] * len(ins)
    L = [f"struct {S}Bind : public TableFunctionData {{ vector<Value> in; }};",
         f"struct {S}State : public GlobalTableFunctionState {{",
         f"    {eb} *tiles = nullptr; int count = 0; idx_t idx = 0;",
         f"    ~{S}State() {{ free(tiles); }}",
         "};",
         f"static unique_ptr<FunctionData> {S}BindFn(ClientContext &, TableFunctionBindInput &input,",
         "                                          vector<LogicalType> &types, vector<string> &names) {",
         f"    auto b = make_uniq<{S}Bind>();",
         "    for (auto &v : input.inputs) {",
         f"        if (v.IsNull()) throw BinderException(\"{f['sqlfn']}: expects non-null arguments\");",
         "        b->in.push_back(v);",
         "    }",
         f"    types = {{LogicalType::INTEGER, {TILE_BOX_IN[eb][1]}}};",
         "    names = {\"index\", \"tile\"};",
         "    return std::move(b);",
         "}",
         f"static unique_ptr<GlobalTableFunctionState> {S}InitFn(ClientContext &context, TableFunctionInitInput &input) {{",
         "    EnsureMeosThreadInitialized(context);",
         f"    auto &b = input.bind_data->Cast<{S}Bind>();",
         f"    auto st = make_uniq<{S}State>();",
         "    std::string a0 = StringValue::Get(b.in[0]);",
         f"    {b0} *x0 = {blobto}(string_t(a0.data(), (uint32_t) a0.size()));"]
    geos, call = [], []
    byname = {p["name"]: i for i, p in enumerate(ins)}
    for p in ps:
        nm = p["name"]
        if p is ps[0]:
            call.append("x0"); continue
        if nm in bound:
            call.append(str(bound[nm])); continue
        i = byname[nm]
        dt, expr = _tile_arg(p)
        if dt == "MobilityDuckGeometryType()":
            g = f"g{i}"
            sr = "x0->srid" if srid else "0"
            if i < k:
                L.append(f"    std::string {g}_blob = StringValue::Get(b.in[{i}]);")
                L.append(f"    GSERIALIZED *{g} = GeometryToGSerialized(string_t({g}_blob.data(), "
                         f"(uint32_t) {g}_blob.size()), {sr});")
            else:
                lit = str(defs[i]).strip().strip("'")
                L.append(f"    GSERIALIZED *{g} = geom_in(\"{lit}\", -1);")
            geos.append(g); call.append(g); continue
        if i < k:
            L.append(f"    auto c{i} = [&]() {{ auto &v = b.in[{i}]; return {expr}; }};")
            call.append(f"c{i}()")
        else:
            lit = forward_literal(p, defs[i])
            if lit is None:
                raise ValueError(f"{name}: no literal for {nm}")
            call.append(lit)
    L += [f"    st->tiles = {name}({', '.join(call)}, &st->count);",
          f"    free(x0);" + "".join(f" free({g});" for g in geos),
          "    if (!st->tiles) st->count = 0;",
          "    return std::move(st);",
          "}",
          f"static void {S}ExecFn(ClientContext &, TableFunctionInput &input, DataChunk &output) {{",
          f"    auto &st = input.global_state->Cast<{S}State>();",
          "    idx_t rows = MinValue<idx_t>(STANDARD_VECTOR_SIZE, (idx_t) st.count - st.idx);",
          "    auto &v1 = output.data[1];",
          "    for (idx_t i = 0; i < rows; i++, st.idx++) {",
          "        FlatVector::GetData<int32_t>(output.data[0])[i] = (int32_t) st.idx + 1;",
          f"        FlatVector::GetData<string_t>(v1)[i] = StringVector::AddStringOrBlob(v1, "
          f"(const char *) &st.tiles[st.idx], sizeof({eb}));",
          "    }",
          "    output.SetCardinality(rows);",
          "}",
          ""]
    return "\n".join(L)


# ── the boxes of a spatial value and the tile of a point: one scalar per row ───────────────
# spaceBoxes and spaceTimeBoxes answer stbox[] from a spatial temporal value, its sizes, an origin
# geometry, a time origin and two flags; getSpaceTile and getSpaceTimeTile answer one stbox from a
# point. Their signatures differ in the sizes a one-size form binds (`boundArgs`) and share the
# trailing SQL defaults (`argDefaults`), as the tiles of a box do, so each form reads the first
# `k` declared arguments per row and passes the bound and defaulted ones as literals. A geometry
# past the first operand is read in the SRID of the first, as the hand executors read the
# origin: tspatial_srid of a temporal value, 0 for a geometry.
def shape_box_scalars(f):
    """[(first kind, [(sigs, ins, bound)]), ret kind] for a retired group's public function
    answering stbox[] from a spatial temporal value whose tail #shape_array does not take, or one
    stbox from a geometry followed by a geometry; None otherwise. Signatures with the same tail and
    the same bindings share one body."""
    if not retired(f) or f.get("api") != "public" or not f.get("sqlfn"):
        return None
    ar = (f.get("shape") or {}).get("arrayReturn") or {}
    lf = (ar.get("lengthFrom") or {}).get("name")
    ps = [p for p in f.get("params") or [] if p.get("name") != lf]
    if len(ps) < 2:
        return None
    b0, n0 = base(ps[0]["canonical"]), norm(ps[0]["canonical"])
    if n0.count("*") != 1 or b0 not in ("Temporal", "GSERIALIZED"):
        return None
    if ar:
        if base((ar.get("element") or {}).get("canonical") or "") != "STBox" or not lf \
                or b0 != "Temporal":
            return None
        if all(norm(p["canonical"]) == "int" for p in ps[1:]) and len(ps) == 2:
            return None                          # #shape_array's split-into-N tail
        ret = "list"
    else:
        rc = f.get("returnType") or {}
        if base(rc.get("canonical") or "") != "STBox" or norm(rc["canonical"]).count("*") != 1 \
                or b0 != "GSERIALIZED" or not any(base(p["canonical"]) == "GSERIALIZED"
                                                   for p in ps[1:]):
            return None
        ret = "box"
    groups = {}
    for s in f.get("sqlSignatures") or []:
        if s.get("retSet"):
            return None
        bound = s.get("boundArgs") or {}
        ins = [p for p in ps if p["name"] not in bound]
        args = s.get("args") or []
        if len(ins) != len(args) or any(_tile_arg(p) is None for p in ins[1:]):
            return None
        if b0 == "Temporal" and args[0] not in SIG_TEMPORAL_ACC:
            return None
        if b0 == "GSERIALIZED" and args[0] != "geometry":
            return None
        key = (tuple(args[1:]), tuple(sorted(bound.items())), tuple(s.get("argDefaults") or []))
        groups.setdefault(key, (s, ins, bound, []))[3].append(args[0])
    if not groups:
        return None
    return ("temporal" if b0 == "Temporal" else "geo", list(groups.values()), ret)


def emit_box_scalars(f, first, tag, s, ins, bound, k, ret):
    """The per-row body of one form of #shape_box_scalars reading the first `k` declared
    arguments, the others from their SQL defaults."""
    name = f["name"]
    ar = (f.get("shape") or {}).get("arrayReturn") or {}
    lf = (ar.get("lengthFrom") or {}).get("name")
    ps = [p for p in f["params"] if p.get("name") != lf]
    defs = s.get("argDefaults") or [None] * len(ins)
    byname = {p["name"]: i for i, p in enumerate(ins)}
    L = [f"static void Gen_{name}_{tag}(DataChunk &args, ExpressionState &, Vector &result) {{",
         "    EnsureMeosThreadInitialized();",
         "    const idx_t rows = args.size();",
         f"    for (idx_t c = 0; c < {k}; c++) args.data[c].Flatten(rows);",
         "    auto &outv = FlatVector::Validity(result);"]
    if ret == "list":
        L += ["    auto list_entries = FlatVector::GetData<list_entry_t>(result);",
              "    idx_t off = 0;"]
    else:
        L.append("    auto out = FlatVector::GetData<string_t>(result);")
    L += ["    for (idx_t i = 0; i < rows; i++) {",
          "        bool valid = true;",
          f"        for (idx_t c = 0; c < {k}; c++)",
          "            if (!FlatVector::Validity(args.data[c]).RowIsValid(i)) { valid = false; break; }",
          "        if (!valid) {",
          "            outv.SetInvalid(i);" + (" list_entries[i] = list_entry_t{off, 0};"
                                              if ret == "list" else ""),
          "            continue;",
          "        }",
          "        string_t a0 = FlatVector::GetData<string_t>(args.data[0])[i];"]
    if first == "temporal":
        L += ["        Temporal *x0 = BlobToTemporal(a0);",
              "        int32_t srid0 = tspatial_srid(x0);"]
    else:
        L += ["        GSERIALIZED *x0 = GeometryToGSerialized(a0, 0);",
              "        int32_t srid0 = 0;"]
    call, geos = ["x0"], []
    for p in ps[1:]:
        nm = p["name"]
        if nm in bound:
            call.append(str(bound[nm]))
            continue
        i = byname[nm]
        bb = base(p["canonical"])
        if bb == "GSERIALIZED":
            g = f"g{i}"
            if i < k:
                L.append(f"        GSERIALIZED *{g} = GeometryToGSerialized("
                         f"FlatVector::GetData<string_t>(args.data[{i}])[i], srid0);")
            else:
                lit = str(defs[i]).strip().strip("'")
                L.append(f"        GSERIALIZED *{g} = geom_in(\"{lit}\", -1);")
            geos.append(g)
            call.append(g)
            continue
        if i >= k:
            lit = forward_literal(p, defs[i])
            if lit is None:
                raise ValueError(f"{name}: no literal for {nm}")
            call.append(lit)
        elif bb == "Interval":
            call.append(f"MeosIntervalArg(FlatVector::GetData<interval_t>(args.data[{i}])[i])")
        else:
            _dt, cpp, expr = SCALAR_ARG[bb]
            call.append(expr.replace("a2", f"FlatVector::GetData<{cpp}>(args.data[{i}])[i]"))
    frees = "free(x0);" + "".join(f" free({g});" for g in geos)
    if ret == "list":
        L += ["        int count = 0;",
              f"        STBox *arr = {name}({', '.join(call)}, &count);",
              f"        {frees}",
              "        int n = (arr && count > 0) ? count : 0;",
              "        ListVector::Reserve(result, off + n);",
              "        ListVector::SetListSize(result, off + n);",
              "        list_entries[i] = list_entry_t{off, (uint64_t) n};",
              "        if (n > 0) {",
              "            auto &child_vector = ListVector::GetEntry(result);",
              "            child_vector.SetVectorType(VectorType::FLAT_VECTOR);",
              "            auto *cd = FlatVector::GetData<string_t>(child_vector);",
              "            for (int j = 0; j < n; ++j) { %s }" % ARRAY_ELEM["STBox"][2],
              "            off += n;",
              "        }",
              "        free(arr);"]
    else:
        L += [f"        STBox *r = {name}({', '.join(call)});",
              f"        {frees}",
              "        if (!r) { outv.SetInvalid(i); continue; }",
              "        out[i] = %s;" % (PTR_RET["STBox"][1] % "r")]   # frees r
    L += ["    }", "}", ""]
    return "\n".join(L)


def box_tile_forms(s, ins):
    """The numbers of declared arguments a signature registers: all of them, and each shorter
    form its trailing SQL defaults allow."""
    n = len(ins)
    defs = s.get("argDefaults") or [None] * n
    ks = [n]
    k = n
    while k > 1 and defs[k - 1] is not None:
        k -= 1
        ks.append(k)
    return ks

def shape_nary_scalars(f):
    """(first operand kind, [scalar marshals], return kind) for a retired group's function whose
    first operand is a temporal value or a geometry, followed by three or more by-value scalars,
    answering a temporal value or a geometry (rotate(tgeompoint, float, float, float), affine over
    twelve), past the three operands DuckDB's executors take; None otherwise. The shape reads every
    operand per row, as the hand Tgeo_affine_12 executor does, and answers NULL for any NULL one.
    It also reads the operands no other shape takes beside a temporal value or a geometry: a
    duration, an enum read from its name, a text, a second geometry or temporal value, an array of
    geometries first, a by-value result of a geometry and a geometry handed back through an
    out-parameter."""
    if not retired(f) or f.get("api") != "public" or not f.get("sqlfn"):
        return None
    ins, out = classify(f)
    if not ins:
        return None
    b0, n0 = base(ins[0]["canonical"]), norm(ins[0]["canonical"])
    rb, rn = base(f["returnType"]["canonical"]), norm(f["returnType"]["canonical"])
    # a set, span or span set followed by up to two arguments a value takes, answering a set, span
    # or span set: shiftScale(intspan, integer, integer), shiftScale(tstzset, interval, interval),
    # and a container of another kind, span(intspanset) -> intspan, spanN(intspanset, integer),
    # dates(datespanset) -> dateset
    if out is None and b0 in CONT_BLOBTO and n0.endswith("*") and len(ins) <= 3:
        # a span past the first answering a box is a period (tbox(intspan, tstzspan)), read as
        # #span_operand reads one
        tail = [shape_arg(p) or (span_operand(f, p, k) if rb in ("TBox", "STBox") else None)
                for k, p in enumerate(ins[1:], start=1)]
        if None in tail or rb not in CONT_BLOBTO or not rn.endswith("*") \
                or "const" in f["returnType"]["canonical"]:
            return None
        # its own kind back, from nothing or one by-value scalar, is #shape_set's and
        # #shape_span's (shift(intspan, integer)); a duration is this shape's (shift(tstzspan,
        # interval))
        if rb == b0 and (len(ins) == 1 or (len(ins) == 2 and base(ins[1]["canonical"]) in SCALAR_ARG
                                           and "*" not in norm(ins[1]["canonical"]))):
            return None
        return ("cont:" + b0, tail, "cont:" + rb)
    # an array of geometries with its count (collect(geometry[]), makeLine(geometry[])), the
    # array and its length the catalog's `inputArrays` names, read from a LIST of GEOMETRY
    arr = ((f.get("shape") or {}).get("inputArrays") or [None])[0]
    if n0.replace("*", "").strip() == "GSERIALIZED" and n0.endswith("**") and arr and len(ins) >= 2 \
            and arr.get("param") == ins[0]["name"] \
            and (arr.get("lengthFrom") or {}).get("name") == ins[1]["name"] \
            and base(ins[1]["canonical"]) == "int" and "*" not in norm(ins[1]["canonical"]):
        first, ins = "geoarr", [ins[0]] + ins[2:]
    elif not n0.endswith("*") or n0.endswith("**") or b0 not in ("Temporal", "GSERIALIZED"):
        return None
    else:
        first = "temporal" if b0 == "Temporal" else "geo"
    # a geometry handed back through an out-parameter, the call answering whether there is one
    # (geoMeasure(tgeompoint, tfloat, boolean) over tpoint_tfloat_to_geomeas)
    if out is not None and not (norm(out) == "GSERIALIZED **"
                                and norm(f["returnType"]["canonical"]) == "bool"):
        return None
    # a temporal value with fewer scalars is the unary, binary and ternary shapes'; one with a
    # geometry or a second temporal value among them, or a geometry handed back, is this one's
    # where no other shape takes it (rotate(tgeompoint, float, geometry)); a geometry, which no
    # other shape takes first, is this one's whatever its scalars (reverse(geometry))
    if first == "temporal" and len(ins) < 4:
        later = {base(p["canonical"]) for p in ins[1:] if norm(p["canonical"]).count("*") == 1}
        if not (later & {"GSERIALIZED", "Temporal"} or out is not None):
            return None
        if any(s(f) for s in (shape_binary, shape_ternary, shape_geo_temporal, shape_binary_tt,
                              shape_binary_tt_scalar, shape_outparam)):
            return None
    sigs = f.get("sqlSignatures") or []
    tail = []
    for k, p in enumerate(ins[1:], start=1):
        bb, nn = base(p["canonical"]), norm(p["canonical"])
        # the period of a geometry's box (stbox(geometry, tstzspan)), read as #span_operand reads one
        sp = span_operand(f, p, k) if first == "geo" and rb == "STBox" else None
        if sp is not None:
            tail.append(sp)
            continue
        # a geometry, read in the SRID of the temporal value beside it as #emit_geo_temporal
        # reads one, else in SRID 0, and freed after the call (#hoist_span_operands)
        if bb == "GSERIALIZED" and nn.count("*") == 1:
            srid = "tspatial_srid(x0)" if first == "temporal" else "0"
            tail.append(("MobilityDuckGeometryType()", "string_t",
                         "GeometryToGSerialized(a2, %s)" % srid))
            continue
        # a temporal value, typed by the one SQL type every signature declares there
        # (geoMeasure(tgeompoint, tfloat, boolean)) and freed after the call
        if bb == "Temporal" and nn.count("*") == 1:
            decl = {s["args"][k] for s in sigs if len(s.get("args") or []) == len(ins)}
            acc = SIG_TEMPORAL_ACC.get(decl.pop()) if len(decl) == 1 else None
            if acc is None:
                return None
            tail.append((acc, "string_t", "BlobToTemporal(a2)"))
            continue
        # a by-value scalar, a duration, an enum read from its name or a text, as #shape_arg
        # reads one (tsample(tint, interval, timestamptz, text), buffer(geometry, float, text))
        sa = shape_arg(p)
        if sa is None:
            return None
        tail.append(sa)
    if out is not None:
        return (first, tail, "geo:out")
    # a by-value result of a geometry or an array of them (numPoints(geometry),
    # distance(geometry, geometry)), as #scalar_ret4 types and writes it
    if first != "temporal" and rb in SCALAR_RET_CPP and "*" not in rn:
        return (first, tail, "scalar")
    if not rn.endswith("*") or "const" in f["returnType"]["canonical"]:
        return None
    if rb in TEMPORAL_PTR_RET:
        ret = "temporal"
    elif rb == "GSERIALIZED":
        ret = "geo"
    elif rb in CONT_BLOBTO and first == "geo":
        # the box of a geometry (stbox(geometry)), typed by the signature as a container is
        ret = "cont:" + rb
    elif rb == "char" and first == "temporal":
        # the text of a temporal value (asMFJSON(tfloat, integer, integer, integer))
        ret = "text"
    else:
        return None
    return (first, tail, ret)

def emit_nary_scalars(f, first, tail, ret, dval=None):
    """The per-row body of #shape_nary_scalars: every operand flattened and read at the row, a
    temporal operand decoded by BlobToTemporal and a geometry by GeometryToGSerialized with SRID 0
    as #emit_geo_pair_out reads one, the result marshalled as #emit_body returns it. With `dval`,
    the shorter `Gen_<fn>_d` the trailing SQL default answers, that literal in the last
    argument's place, as #emit_defaulted_ternary is the shorter body of a ternary one."""
    name = f["name"]
    sfx = "_d" if dval is not None else ""
    if dval is not None:
        tail = tail[:-1]
    n = 1 + len(tail)
    # a by-value result is written in the type #scalar_ret4 registers it as
    _sreg, sct, scpp, sexpr = scalar_ret4(f) if ret == "scalar" else (None, None, "string_t", None)
    L = [f"static void Gen_{name}{sfx}(DataChunk &args, ExpressionState &state, Vector &result) {{",
         "    EnsureMeosThreadInitialized();",
         "    const idx_t rows = args.size();",
         f"    for (idx_t c = 0; c < {n}; c++) args.data[c].Flatten(rows);"]
    if first == "geoarr":
        L += ["    auto in0 = ListVector::GetData(args.data[0]);",
              "    auto &ch0 = ListVector::GetEntry(args.data[0]);",
              "    ch0.Flatten(ListVector::GetListSize(args.data[0]));",
              "    auto ch0d = FlatVector::GetData<string_t>(ch0);",
              "    auto &ch0v = FlatVector::Validity(ch0);"]
    else:
        L.append("    auto in0 = FlatVector::GetData<string_t>(args.data[0]);")
    for k, (_dt, cpp, _m) in enumerate(tail, start=1):
        L.append(f"    auto in{k} = FlatVector::GetData<{cpp}>(args.data[{k}]);")
    L += [f"    auto out = FlatVector::GetData<{scpp}>(result);",
          "    auto &outv = FlatVector::Validity(result);",
          "    for (idx_t i = 0; i < rows; i++) {",
          "        bool valid = true;",
          f"        for (idx_t c = 0; c < {n}; c++)",
          "            if (!FlatVector::Validity(args.data[c]).RowIsValid(i)) { valid = false; break; }",
          "        if (!valid) { outv.SetInvalid(i); continue; }"]
    free_x0 = ["        free(x0);"]
    if first == "temporal":
        L.append("        Temporal *x0 = BlobToTemporal(in0[i]);")
    elif first.startswith("cont:"):
        cb = first.split(":", 1)[1]
        L.append(f"        {cb} *x0 = {CONT_BLOBTO[cb]}(in0[i]);")
    elif first == "geoarr":
        # the geometries of the list, its NULL elements left out as PostGIS leaves them out of
        # an array, and no geometry answering NULL as the SQL function does (count == 0)
        L += ["        const list_entry_t le0 = in0[i];",
              "        GSERIALIZED **x0 = (GSERIALIZED **) malloc(sizeof(GSERIALIZED *) * (le0.length + 1));",
              "        int n0 = 0;",
              "        for (idx_t j = 0; j < le0.length; j++)",
              "            if (ch0v.RowIsValid(le0.offset + j))",
              "                x0[n0++] = GeometryToGSerialized(ch0d[le0.offset + j], 0);",
              "        if (n0 == 0) { free(x0); outv.SetInvalid(i); continue; }"]
        free_x0 = ["        for (int j = 0; j < n0; j++) free(x0[j]);", "        free(x0);"]
    else:
        L.append("        GSERIALIZED *x0 = GeometryToGSerialized(in0[i], 0);")
    decl, args_, free_ = hoist_span_operands(
        ["x0"] + (["n0"] if first == "geoarr" else [])
        + [m.replace("a2", f"in{k}[i]") for k, (_d, _c, m) in enumerate(tail, start=1)]
        + ([dval] if dval is not None else []), "        ")
    L += decl
    call = ", ".join(args_)
    if ret.startswith("cont:"):
        cb = ret.split(":", 1)[1]
        L += [f"        {cb} *r = {name}({call});"] + free_ + free_x0 + [
              "        if (!r) { outv.SetInvalid(i); continue; }",
              "        out[i] = %s;" % (PTR_RET[cb][1] % "r")]
    elif ret == "temporal":
        L += [f"        Temporal *r = (Temporal *) {name}({call});"] + free_ + free_x0 + [
              "        if (!r) { outv.SetInvalid(i); continue; }",
              "        out[i] = TemporalToBlob(result, r);"]
    elif ret == "text":
        L += [f"        char *r = {name}({call});"] + free_ + free_x0 + [
              "        if (!r) { outv.SetInvalid(i); continue; }",
              "        out[i] = StringVector::AddString(result, r);",
              "        free(r);"]
    elif ret == "scalar":
        guard = SCALAR_NULL_GUARD.get((f.get("shape") or {}).get("nullableResult"))
        L += [f"        {sct} r = {name}({call});"] + free_ + free_x0
        if guard:
            L.append(f"        if ({guard}) {{ outv.SetInvalid(i); continue; }}")
        L.append(f"        out[i] = {sexpr};")
    elif ret == "geo:out":
        # the geometry handed back through the last parameter, NULL where the call finds none
        L += ["        GSERIALIZED *r = NULL;",
              f"        bool found = {name}({call}, &r);"] + free_ + free_x0 + [
              "        if (!found || !r) { outv.SetInvalid(i); continue; }",
              "        out[i] = GSerializedToGeometry(r, state, result);",
              "        free(r);"]
    else:
        L += [f"        GSERIALIZED *r = {name}({call});"] + free_ + free_x0 + [
              "        if (!r) { outv.SetInvalid(i); continue; }",
              "        out[i] = GSerializedToGeometry(r, state, result);",
              "        free(r);"]
    L += ["    }",
          "    if (rows == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);",
          "}", ""]
    return "\n".join(L)

def shape_geo_pair_out(f):
    """(DuckDB return, C local) for a retired group's (const GSERIALIZED *, const GSERIALIZED *,
    <scalar> *result) -> bool function (bearing(geometry, geometry) over bearing_point_point), the
    by-value result the out-parameter hands back as #shape_binary folds one; None otherwise."""
    if not retired(f) or f.get("api") != "public" or not f.get("sqlfn"):
        return None
    ins, out = classify(f)
    if out is None or len(ins) != 2 or norm(out) not in OUTPARAM_LOCAL:
        return None
    if not all(base(p["canonical"]) == "GSERIALIZED" and norm(p["canonical"]).endswith("*")
               for p in ins):
        return None
    return (OUTPRIM[norm(out)], OUTPARAM_LOCAL[norm(out)])

def emit_geo_pair_out(f, local):
    """Two geometries, each read as #emit_geo_temporal reads one; with no temporal operand to
    take an SRID from, both are read with SRID 0, the planar plane DuckDB's GEOMETRY stands for."""
    name = f["name"]; ctype, ecpp = local
    return (
f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
f"    EnsureMeosThreadInitialized();\n"
f"    BinaryExecutor::ExecuteWithNulls<string_t, string_t, {ecpp}>(args.data[0], args.data[1], result, args.size(),\n"
f"        [&](string_t in1, string_t in2, ValidityMask &mask, idx_t idx) -> {ecpp} {{\n"
f"            GSERIALIZED *g1 = GeometryToGSerialized(in1, 0);\n"
f"            GSERIALIZED *g2 = GeometryToGSerialized(in2, 0);\n"
f"            {ctype} v;\n"
f"            bool ok = {name}(g1, g2, &v);\n"
f"            free(g1); free(g2);\n"
f"            if (!ok) {{ mask.SetInvalid(idx); return {ecpp}(); }}\n"
f"            return ({ecpp}) v;\n"
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
    if acc == "MobilityDuckGeometryType()":
        return "geometry"
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

def sig_declared_ret(f, acc, arity):
    """The DuckDB return accessor the catalog declares for the overload taking `acc` first at
    `arity` arguments, else None. `ret_temporal_type` answers the OPERAND'S OWN TYPE for a
    Temporal->Temporal function, which holds only where the family is CLOSED under the
    operation; where MobilityDB says the answer leaves the family the catalog is the SoT --
    centroid(tgeometry) is tgeompoint, cellToBoundary(th3index) is tgeography,
    minDistSimplify(tgeography,float) is tgeometry, tDistance(tint,tint) is tint. Only an
    UNAMBIGUOUS match counts (one signature on that operand at that arity), so a shape the
    catalog describes with several overloads keeps the heuristic. A signature answers for a
    shorter arity whose omitted trailing arguments all carry a default, read from argDefaults as
    #trailing_arg_default reads them: the shorter form is the same SQL function,
    tprecision(tint, interval) a tfloat as tprecision(tint, interval, timestamptz) is."""
    def _callable_at(s):
        args = s.get("args") or []
        defs = s.get("argDefaults") or [None] * len(args)
        return len(args) == arity or (len(args) > arity and all(d is not None for d in defs[arity:]))
    on = [s for s in (f.get("sqlSignatures") or [])
          if (s.get("args") or [None])[0] == _acc_sqlname(acc) and _callable_at(s)]
    if len(on) == 1 and on[0].get("ret") in SIG_TEMPORAL_ACC:
        return SIG_TEMPORAL_ACC[on[0]["ret"]]
    return None

def array_tail_sql(f, n):
    """The SQL types of the `n` scalar arguments after an array-return's container, as the
    signatures returning an array declare them, read from the same signatures and with the same
    filter as #array_declared_accs; () without any, None when no array signature takes `n` of
    them or the signatures disagree."""
    if not n:
        return ()
    tails = {tuple(s["args"][1:]) for s in f.get("sqlSignatures") or []
             if len(s.get("args") or []) == 1 + n and (s.get("ret") or "").endswith("[]")}
    return tails.pop() if len(tails) == 1 else None

def array_tail_params(f):
    """The scalar parameters after an array-return's container, its count parameter left out,
    split as #shape_array splits them (`ins[1:]`)."""
    lf = ((f.get("shape") or {}).get("arrayReturn") or {}).get("lengthFrom") or {}
    return [p for p in f["params"] if p.get("name") != lf.get("name")][1:]

def array_declared_accs(f, tail=()):
    """The registered DuckDB accessors this array-return is CREATE FUNCTION'd for, from the catalog
    sqlSignatures whose SQL return is an array (`<base>[]`). Extended/unregistered arg types map to
    no accessor and drop out. This is the SoT for a generic (scope='all') array-return's type set,
    excluding types whose canonical return is NOT an array (e.g. set_spans over textset: no sig).
    `tail` matches the fixed scalar arguments after the container, as in _sig_ret_for."""
    m = {**SET_TYPES, **SPAN_TYPES, **SPANSET_TYPES, **SIG_TEMPORAL_ACC,
         **CONT_SQL_ACC["TBox"], **CONT_SQL_ACC["STBox"]}
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
    "timestamptz": "LogicalType::TIMESTAMP_TZ", "interval": "LogicalType::INTERVAL",
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
    # the base value of another family is its registered type (cbuffer[] -> LIST(cbuffer))
    d = SQL_BASE_TO_DUCK.get(r[:-2]) or SIG_TEMPORAL_ACC.get(r[:-2]) or BASE_LOGICAL.get(r[:-2])
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
    # The scalars after the container are read as the multi-argument shapes read theirs
    # (scalar_arg_for), and named by the SQL types the array-returning signatures declare for
    # them: one integer for the split-into-N family, a size, an origin and a border flag for the
    # bins of a span (bins(intspan, integer, integer, boolean) -> intspan[]).
    tailp = ins[1:]
    targs = [scalar_arg_for(f, p) for p in tailp]
    if any(t is None for t in targs):
        return None
    tail = array_tail_sql(f, len(tailp))
    if tail is None or any(SQL_BASE_TO_DUCK.get(s) != t[0] for s, t in zip(tail, targs)):
        return None
    ib = base(ins[0]["canonical"])
    # A cell id first, by value: the array a static cell function answers over the cell types its
    # signatures declare with an array return (quadbinCellToTile(quadbin) -> integer[])
    if ib in CELL_UINT and "*" not in norm(ins[0]["canonical"]) and not tail:
        accs = [CELL_SQL[s["args"][0]] for s in f.get("sqlSignatures") or []
                if len(s.get("args") or []) == 1 and s["args"][0] in CELL_SQL
                and (s.get("ret") or "").endswith("[]")]
        return ("array", ec, "cell", accs, tail) if accs else None
    # A geometry first, in a retired group: DuckDB's one GEOMETRY type stands for the geometry and
    # geography signatures alike, read with SRID 0 as #emit_nary_scalars reads one
    # (stboxes(geometry), splitNStboxes(geometry, integer))
    if ib == "GSERIALIZED" and retired(f) and norm(ins[0]["canonical"]).endswith("*"):
        want = [*tail]
        if any((s.get("args") or [None])[0] in ("geometry", "geography")
               and list((s.get("args") or [])[1:]) == want for s in f.get("sqlSignatures") or []):
            return ("array", ec, ib, ["MobilityDuckGeometryType()"], tail)
        return None
    if ib not in ARRAY_IN or not norm(ins[0]["canonical"]).endswith("*"):
        return None
    sc = (set_reg_scope(name) if ib == "Set" else spanset_reg_scope(name) if ib == "SpanSet"
          else span_reg_scope(name) if ib == "Span"
          else box_reg_scope(CONT_SQL_ACC[ib][ib.lower()])(name) if ib in ("STBox", "TBox")
          else reg_scope(name))
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
    if ib == "cell":
        # a by-value cell id, read as the int64 a cell type carries in DuckDB
        read_in = ("        uint64_t in = static_cast<uint64_t>("
                   "FlatVector::GetData<int64_t>(in_vec)[i]);\n")
        free_in = ""
    else:
        cbase, blobto = ARRAY_IN[ib]
        decode = f"{blobto}(blob, 0)" if ib == "GSERIALIZED" else f"{blobto}(blob)"
        read_in = (f"        string_t blob = FlatVector::GetData<string_t>(in_vec)[i];\n"
                   f"        {cbase} *in = {decode};\n")
        free_in = "        free(in);\n"
    _lt, ccpp, marsh = ARRAY_ELEM[ec]
    retc = norm(f["returnType"]["canonical"])          # e.g. "TimestampTz *", "text **", "Span *"
    # One vector per scalar after the container, read per row through its #scalar_arg_for triple
    # and passed in the order the function declares it; a NULL in any of them answers a NULL list.
    # The first keeps the n_vec / n_arg names of the split-into-N count.
    targs = [scalar_arg_for(f, p) for p in array_tail_params(f)] if tail else []
    nm = ["n" if k == 1 else f"n{k}" for k in range(1, len(targs) + 1)]
    n_vec = "".join(f"    auto &{v}_vec = args.data[{k}];\n"
                    f"    {v}_vec.Flatten(row_count);\n" for k, v in enumerate(nm, start=1))
    n_null = "".join(f" || {v}_vec.GetValue(i).IsNull()" for v in nm)
    n_read = "".join(f"        {cpp} {v}_arg = FlatVector::GetData<{cpp}>({v}_vec)[i];\n"
                     for v, (_dt, cpp, _m) in zip(nm, targs))
    call_args = ", ".join(["in"] + [m.replace("a2", f"{v}_arg")
                                    for v, (_dt, _cpp, m) in zip(nm, targs)] + ["&count"])
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
f"{read_in}"
f"{n_read}"
f"        int count = 0;\n"
f"        {retc} arr = {name}({call_args});\n"
f"{free_in}"
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

def box_ret3(f, ret_key):
    """(registration LogicalType, MEOS-call C type, executor RET) for a Temporal x box scalar —
    the third paired decision, for the same reason as scalar_ret4 and outprim_ret3.

    The C type stays on the CALL and on the sentinel test: the no-distance sentinel is the maximum
    of the type MEOS returns, so testing it against the declared type would compare against the
    wrong maximum. The conversion happens after the test."""
    ccpp = "bool" if ret_key == "bool" else SCALAR_RET_CPP[ret_key][0]
    dflt = "LogicalType::BOOLEAN" if ret_key == "bool" else SCALAR_RET_CPP[ret_key][1]
    sr = SQL_SCALAR_RET.get(f.get("sqlReturnType"))
    if sr and sr in NUMERIC_DUCK_CPP and ret_key not in UNSIGNED_C_RET and ret_key in NUMERIC_C_RET:
        return (sr, ccpp, NUMERIC_DUCK_CPP[sr])
    return (dflt, ccpp, ccpp)

def emit_temporal_box(f, kind, rev=False):
    """The body of a temporal and a box. `rev` reads them off the vectors in the commuted order a
    declared signature states (nearestApproachDistance(stbox, trgeometry) over
    nad_trgeometry_stbox(temp, box)) and keeps the kernel's own call order, emitted as
    `Gen_<fn>_rev` beside `Gen_<fn>` as #emit_container_scalar emits a commuted container pair."""
    name = f["name"]; parts = kind.split(":")
    side, box = parts[0], parts[1]
    sentinel = parts[2] == "sentinel"
    ret = parts[-1]
    blobto = BOX_MARSH[box][0]
    _dt, cpp, ecpp = box_ret3(f, ret)   # paired with the registration by construction
    call = f"{name}(t, bx)" if side == "tb_r" else f"{name}(bx, t)"
    read_tb = (side == "tb_r") != rev   # the temporal sits on the first vector
    marshal = (f"            Temporal *t = BlobToTemporal(a);\n            {box} *bx = {blobto}(b);\n"
               if read_tb else
               f"            {box} *bx = {blobto}(a);\n            Temporal *t = BlobToTemporal(b);\n")
    frees = "free(t); free(bx);" if read_tb else "free(bx); free(t);"
    sfx = "_rev" if rev else ""
    if not sentinel:
        body = (f"{marshal}            {cpp} r = {call};\n            {frees}\n"
                f"            return ({ecpp}) r;")
        return (f"static void Gen_{name}{sfx}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::Execute<string_t, string_t, {ecpp}>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t a, string_t b) {{\n{body}\n        }});\n}}\n")
    # The maximum of the type MEOS RETURNS says there is no distance, which is NULL here; the
    # conversion to the declared type happens after that test, never before it.
    body = (f"{marshal}            {cpp} r = {call};\n            {frees}\n"
            f"            if (r == std::numeric_limits<{cpp}>::max()) {{ mask.SetInvalid(idx); return {ecpp}(); }}\n"
            f"            return ({ecpp}) r;")
    return (f"static void Gen_{name}{sfx}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    BinaryExecutor::ExecuteWithNulls<string_t, string_t, {ecpp}>(args.data[0], args.data[1], result, args.size(),\n"
            f"        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> {ecpp} {{\n{body}\n        }});\n}}\n")

# Temporal + span -> bool: topological predicates across the value (numspan) or time
# (tstzspan) dimension. numspan PAIRS to the tnumber's value type; tstzspan is fixed and
# applies to every temporal type. ALL_TEMPORAL_ACCS = the full temporal type set (core + all
# spatial subtypes via SPATIAL_ALLTYPES, so a new spatial family inherits the time-restriction
# surface atTime/minusTime/deleteTime over tstzspan/tstzset/tstzspanset).
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
    # The container the other operand carries, in either order. A BOX is admissible only where
    # the function answers a temporal value, which is a restriction: `shape_temporal_box` owns
    # the pairings of a temporal value with a box that answer a SCALAR — the topological
    # predicates and the nearest approach. Claiming those here too emits their body TWICE, which
    # the compiler rejects as a redefinition; a registration set cannot show it, because two
    # identical registrations collapse in a set while two definitions do not.
    conts = RESTRICT_CONT_BLOB if retk == "T" else CONT_BLOB
    if b0 == "Temporal" and b1 in conts:   side, cont = "ts_r", b1
    elif b1 == "Temporal" and b0 in conts: side, cont = "ts_l", b0
    else: return None
    # WHICH overloads exist is the catalog's statement, so a function the catalog declares none
    # for is not claimed here. Reading the function NAME to decide instead answers a question a
    # name cannot: it admitted whatever spelling it matched whether or not the extension declares
    # an overload, and turned away every restriction spelled otherwise — which is why a second
    # shape existed to catch them, and why the two had to agree on which claimed what.
    if not any(len(s.get("args") or ()) == 2 for s in (f.get("sqlSignatures") or ())):
        return None
    return (side + ":" + cont + ":" + retk, None)

CONT_BLOB = {"Span": "BlobToSpan", "Set": "BlobToSet", "SpanSet": "BlobToSpanSet"}
def emit_temporal_span(f, kind):
    name = f["name"]; side, cont, retk = kind.split(":"); blob = RESTRICT_CONT_BLOB[cont]
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
# straight from the catalog, so no name is read to decide it. The container return
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
# The set a temporal value answers its values as (getValues=temporal_valueset over every temporal
# type, valueSet over the temporal numbers) resolves as #declared_element_pairs resolves a declared
# set: through SET_SIG_ACC, and the spatial sets through the types their own unit registers
# (SpatialSetType). Only the result is named here:
# the functions over geomset and geogset keep that unit's surface.
TEMPORAL_TO_CONTAINER_ACC = {**SQL_CONTAINER_ACC, **SET_SIG_ACC,
                             "geomset": "SpatialSetType::geomset()",
                             "geogset": "SpatialSetType::geogset()"}
def shape_temporal_to_container(f):
    """(Temporal) -> Set/Span/SpanSet/TBox/STBox conversion, registered per catalog sqlSignature.
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
    if rb not in ("Set", "Span", "SpanSet", "TBox", "STBox") or not rn.endswith("*"):
        return None
    # Each overload keeps the SQL name its signature states, as the temporal-by-span pairs do
    # (#shape_temporal_span): temporal_valueset is getValues over most types and valueSet over
    # the temporal numbers.
    pairs = []
    for s in (f.get("sqlSignatures") or []):
        if len(s["args"]) != 1:
            continue
        aacc = SIG_TEMPORAL_ACC.get(s["args"][0]); racc = TEMPORAL_TO_CONTAINER_ACC.get(s.get("ret"))
        trip = (aacc, racc, s.get("sqlName") or f["sqlfn"])
        if aacc and racc and trip not in pairs:
            pairs.append(trip)
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

# The containers a two-operand restriction takes as its other operand, and the accessors their SQL
# type names resolve to. The marshalling map extends CONT_BLOB with the box types
# (BlobToTbox/BlobToStbox already emitted); the accessor map spans the sets, the spans and the
# boxes alike, because which of them an overload takes is the catalog's statement rather than a
# property of the shape. `shape_temporal_span` reads both: one shape covers every two-operand
# restriction, over either operand order and returning a temporal value or a boolean. The
# 3-operand box restrictions (atStbox with a border bool) are a separate shape.
RESTRICT_CONT_BLOB = {**CONT_BLOB, "TBox": "BlobToTbox", "STBox": "BlobToStbox",
                      "TPCBox": "BlobToTpcbox"}
# The spatial and h3 sets are operands too (atValues(tgeompoint, geomset)), under the types
# #TEMPORAL_TO_CONTAINER_ACC answers them as.
FINITE_SUBSET_ACC = {**SET_TYPES, **SQL_CONTAINER_ACC,
                     "geomset": "SpatialSetType::geomset()",
                     "geogset": "SpatialSetType::geogset()"}

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
    arg1 = (("LogicalType::VARCHAR", "string_t", "__TEXT__") if is_text1
            else scalar_arg_for(f, ins[0]))
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
    ex, prm, ann, ret = scalar_exec(f, rett, _rx)
    inner = f"{pre}{ctype} r = {name}({call1}, t);\n            {post}free(t);\n            {ret}"
    return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    BinaryExecutor::{ex}<{cpp1}, string_t, {rett}>(args.data[0], args.data[1], result, args.size(),\n"
            f"        [&]({cpp1} a1, string_t in{prm}){ann} {{\n"
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
# A set operation may answer a container OTHER than its operand's (a span minus a value is a span
# SET), so the result is marshalled and typed by the RETURN base, never by the operand descriptor.
# MEOS names a per-value set-operation entry after the VALUE'S TYPE — intersection_span_int,
# minus_timestamptz_spanset — so the element is a token of the function name. That is what separates
# a set operation on a value from a container function whose trailing int means something else:
# spanset_span_n takes an INDEX and spanset_make a COUNT, and neither names a type. Without this the
# element map alone reads any trailing int as an element and registers `spanN(intspanset, INTEGER)`,
# which is wrong for every other span set.
ELEM_NAME_TOKEN = {"int": "int", "int32_t": "int", "int64_t": "bigint", "double": "float",
                   "DateADT": "date", "TimestampTz": "timestamptz"}
RET_CONT_TOBLOB = {"Span": "SpanToBlob", "SpanSet": "SpanSetToBlob"}
RET_CONT_ACC = {"Span": ELEM_TO_SPAN, "SpanSet": ELEM_TO_SPANSET}
SPAN_C    = dict(cbase="Span",    blobto="BlobToSpan",    toblob="SpanToBlob",
                 elem=ELEM_TO_SPAN,    scope=span_reg_scope,    ret=ret_span_type,
                 alltypes="SpanTypes::AllTypes()")
SPANSET_C = dict(cbase="SpanSet", blobto="BlobToSpanSet", toblob="SpanSetToBlob",
                 elem=ELEM_TO_SPANSET, scope=spanset_reg_scope, ret=ret_spanset_type,
                 alltypes="SpansetTypes::AllTypes()")
# Box families: SINGLE type each (no AllTypes loop, no element predicates). Fixed-size
# struct -> sizeof(STBox)/sizeof(TBox) (verified single_tile_getters.cpp). Reuse the same
# (X,X)->bool|X shape machinery via the descriptor; the loop registers the concrete accessor.
def box_reg_scope(single):
    """The scope of a box function, the counterpart of #span_reg_scope for a container of one
    type: a box function names no type the way intspan_lower does, so it is scoped to its box when
    its group is retired. The accessors and transformations of tbox and stbox (hasX, getSpace,
    round, expandValue) then reach the arms a span reaches through its name, and the box groups
    the hand layer still registers stay out."""
    return lambda n: (("types", [single]) if n in FN_BY_NAME and retired(FN_BY_NAME[n])
                      else None)
STBOX_C = dict(cbase="STBox", blobto="BlobToStbox", toblob="StboxToBlob",
               elem={}, scope=box_reg_scope("StboxType::stbox()"), ret=lambda n, a: a,
               single="StboxType::stbox()")
TBOX_C  = dict(cbase="TBox",  blobto="BlobToTbox",  toblob="TboxToBlob",
               elem={}, scope=box_reg_scope("TboxType::tbox()"), ret=lambda n, a: a,
               single="TboxType::tbox()")
TPCBOX_C = dict(cbase="TPCBox", blobto="BlobToTpcbox", toblob="TpcboxToBlob",
                elem={}, scope=None, ret=lambda n, a: a, single="TpcboxType::tpcbox()")

# BASE VALUE containers: the base types the binding registers itself, each a single DuckDB type
# carrying one MEOS value behind a BLOB alias — the same shape as a box, so they reach the surface
# through the same descriptor machinery. The C struct name is the one datum a lowercase base does
# not spell (PoseChain against posechain), so it is stated here and checked against the catalog in
# main(); the marshallers and the DuckDB accessor follow from the base name and BASE_LOGICAL.
# Bases carrying their value BY VALUE rather than behind a pointer have no blob to marshal and are
# absent: quadbin and h3index are the eight bytes of the Datum, so they take no descriptor here.
BASE_CBASE = {"jsonb": "Jsonb", "cbuffer": "Cbuffer", "npoint": "Npoint", "pose": "Pose",
              "posechain": "PoseChain", "pcpoint": "Pcpoint", "pcpatch": "Pcpatch",
              "raquet": "Raquet"}
BASEVAL_C = [dict(cbase=BASE_CBASE[b], blobto="BlobTo" + b.capitalize(),
                  toblob=b.capitalize() + "ToBlob", elem={}, scope=None,
                  ret=(lambda n, a: a), single=BASE_LOGICAL[b])
             for b in BASE_ORDER + BASE_ONLY if b in BASE_CBASE]
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
            # A single-type container out of scope (tpcbox, a base value: scope=None, or a box
            # function whose group is not retired) has no name-scoped accessor map, so a bare
            # relax here would pull in its whole scalar-accessor group, which the hand layer
            # registers. Carve out ONLY the bare unary `hash` -> uint32 accessor by sqlfn, gated
            # on C.get("single") so this never fires for Span/SpanSet (which reach hash via their
            # own scope above).
            if (C.get("single") and f.get("sqlfn") == "hash"
                    and rb in BYVAL_RET and "*" not in rn):
                return ("u_scalar:" + rb, byval_ret_duck(rb))
            return None
        if rb == cb and rn.endswith("*"):           return ("u_span", "LogicalType::BLOB")
        if rb in BYVAL_RET and "*" not in rn:       return ("u_scalar:" + rb, byval_ret_duck(rb))
        if rb == "Interval" and rn.endswith("*"):   return ("u_scalar:Interval", "LogicalType::INTERVAL")
        # (X) -> owned C string: the text rendering a span or span set publishes (asText), as
        # shape_set's unary arm renders a set.
        if rb in ("text", "char") and rn.endswith("*"): return ("u_scalar:" + rb, "LogicalType::VARCHAR")
        # (X) -> owned geometry: the geometry a box spans (geometry(stbox))
        if rb == "GSERIALIZED" and rn.endswith("*"):  return ("u_geo", "MobilityDuckGeometryType()")
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
    # (X, by-value scalar) -> by-value scalar (area(stbox, boolean) -> float); scope-gated as u2iv
    if (contp(ins[0]) and base(ins[1]["canonical"]) in SCALAR_ARG
            and "*" not in norm(ins[1]["canonical"]) and rb in BYVAL_RET and "*" not in rn
            and rb != "bool" and C["scope"] is not None and C["scope"](f["name"]) is not None):
        return ("u2sc:%s:%s" % (base(ins[1]["canonical"]), rb), byval_ret_duck(rb))
    # (X, by-value uint64 seed) -> uint64 hash (span_hash_extended/spanset_hash_extended);
    # scope-gated like u2iv. A single-type container out of scope carves out ONLY hashExtended
    # by sqlfn below (mirrors the unary hash carve-out above) so the rest of its
    # scalar-accessor group stays out of scope.
    if (contp(ins[0]) and base(ins[1]["canonical"]) in SCALAR_ARG
            and "*" not in norm(ins[1]["canonical"]) and rb == "uint64_t" and "*" not in rn
            and C["scope"] is not None and C["scope"](f["name"]) is not None):
        return ("bsc:" + base(ins[1]["canonical"]), "LogicalType::UBIGINT")
    if (contp(ins[0]) and base(ins[1]["canonical"]) in SCALAR_ARG
            and "*" not in norm(ins[1]["canonical"]) and rb == "uint64_t" and "*" not in rn
            and C.get("single") and f.get("sqlfn") == "hashExtended"):
        return ("bsc:" + base(ins[1]["canonical"]), "LogicalType::UBIGINT")
    # (X, scalar PARAM) -> owned C string: the text renderings a value publishes (asText/asEWKT).
    # The MEOS entry always takes the maxdd int while the canonical SQL gives that argument a
    # DEFAULT, so the shorter arity is emitted beside it from the catalog's own default rather than
    # from a constant here. No scope gate: the arm is bounded by the shape itself.
    if (contp(ins[0]) and base(ins[1]["canonical"]) in SCALAR_ARG
            and "*" not in norm(ins[1]["canonical"]) and rb == "char" and rn.endswith("*")):
        two, one = text_arities(f)
        if two:
            return ("u2text:" + base(ins[1]["canonical"]), "LogicalType::VARCHAR")
        if one is not None:
            return ("u2text_d:" + str(sql_default_to_cpp(one)), "LogicalType::VARCHAR")
        return None
    # (X, scalar PARAM) -> X : a same-container return whose scalar is NOT an element
    # (floatspan_round/floatspanset_round's precision integer). Name-scoped (<elem>span_round
    # -> that container type), so the round=float-only base-value scoping falls out.
    if (contp(ins[0]) and base(ins[1]["canonical"]) in SCALAR_ARG
            and "*" not in norm(ins[1]["canonical"]) and rb == cb and rn.endswith("*")
            and C["scope"] is not None and C["scope"](f["name"]) is not None):
        return ("csc:" + base(ins[1]["canonical"]), "type")
    # (X, base value) / (base value, X) -> a container: the set operations between a container and
    # one value (spanIntersection/spanMinus/spanUnion and their span-set twins). The RESULT container
    # is named by the return type and typed by the SAME base value as the operand, so a datespan
    # minus a date answers a datespanset. MEOS publishes each operand order as its own entry, so both
    # are emitted. This sits AFTER the same-container arm above on purpose: that one is name-scoped
    # (`floatspan_round` carries a precision int, not an element) and these carry no name scope, so
    # the two are disjoint by construction rather than by ordering luck.
    if rb in RET_CONT_TOBLOB and rn.endswith("*"):
        _tok = lambda b: ELEM_NAME_TOKEN.get(b) in f["name"].split("_")
        if contp(ins[0]) and sel(ins[1]) and _tok(sel(ins[1])):
            return ("cval:%s:%s" % (sel(ins[1]), rb), "type")
        if contp(ins[1]) and sel(ins[0]) and _tok(sel(ins[0])):
            return ("valc:%s:%s" % (sel(ins[0]), rb), "type")
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
    # two collections -> a collection of another type: the union and the difference of two spans,
    # which answer a span set, and the set operations between a span and a span set in either
    # order (spanUnion(intspan, intspanset), spansetMinus(intspanset, intspan)). Detected once
    # under Span, as b_mix is, and registered over the types each declared signature names; the
    # operations whose operands and result are one container are the (X,X) -> X arm below.
    if (cb == "Span" and rb in RET_CONT_TOBLOB and rn.endswith("*")
            and "const" not in f["returnType"]["canonical"]
            and norm(ins[0]["canonical"]).endswith("*") and norm(ins[1]["canonical"]).endswith("*")):
        b0, b1 = base(ins[0]["canonical"]), base(ins[1]["canonical"])
        if {b0, b1} <= {"Span", "SpanSet"} and not b0 == b1 == rb:
            return ("b_cont:%s_%s:%s" % (b0.lower(), b1.lower(), rb), "type")
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
    if kind.startswith("b_cont:"):  # (Span|SpanSet, Span|SpanSet) -> Span|SpanSet of another type
        ops, rbase = kind.split(':')[1:]
        t0, t1 = ops.split('_')
        BT = {"span": "BlobToSpan", "spanset": "BlobToSpanSet"}
        CT = {"span": "Span", "spanset": "SpanSet"}
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            {CT[t0]} *x = {BT[t0]}(a);\n            {CT[t1]} *y = {BT[t1]}(b);\n"
                f"            {rbase} *r = {name}(x, y);\n            free(x); free(y);\n"
                f"            if (!r) {{ mask.SetInvalid(idx); return string_t(); }}\n"
                f"            return {RET_CONT_TOBLOB[rbase]}(result, r);\n        }});\n}}\n")
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
        # NULL on the guard the catalog states (volume(stbox) answers DBL_MAX for no extent),
        # through #scalar_exec as the temporal shapes read it
        cct, rett, rexpr = byval_ret3(kind.split(':')[1])
        meth, extra, ann, ret_stmt = scalar_exec(f, rett, rexpr)
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    UnaryExecutor::{meth}<string_t, {rett}>(args.data[0], result, args.size(),\n"
                f"        [&](string_t in{extra}){ann} {{\n"
                f"            {cb} *s = {bt}(in);\n            {cct} r = {name}(s);\n            free(s);\n"
                f"            {ret_stmt}\n        }});\n}}\n")
    if kind.startswith("u2sc:") or kind.startswith("u2sc_d:"):
        # (X, by-value scalar) -> by-value scalar (area(stbox, boolean)), the counterpart of the
        # u2iv arm for a by-value answer, NULL on the catalog's guard (#scalar_exec); the _d form
        # passes the SQL default of the trailing argument, as the u2text_d arm passes its digits
        _k, ab, rb_ = kind.split(':')[:3]
        cct, rett, rexpr = byval_ret3(rb_)
        meth, extra, ann, ret_stmt = scalar_exec(f, rett, rexpr)
        if _k == "u2sc_d":
            dflt = kind.split(':', 3)[3]
            return (f"static void Gen_{name}_d(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                    f"    EnsureMeosThreadInitialized();\n"
                    f"    UnaryExecutor::{meth}<string_t, {rett}>(args.data[0], result, args.size(),\n"
                    f"        [&](string_t in{extra}){ann} {{\n"
                    f"            {cb} *s = {bt}(in);\n            {cct} r = {name}(s, {dflt});\n"
                    f"            free(s);\n            {ret_stmt}\n        }});\n}}\n")
        _dt, cpp2, marsh = SCALAR_ARG[ab]
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::{meth}<string_t, {cpp2}, {rett}>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t in, {cpp2} a2{extra}){ann} {{\n"
                f"            {cb} *s = {bt}(in);\n            {cct} r = {name}(s, {marsh});\n"
                f"            free(s);\n            {ret_stmt}\n        }});\n}}\n")
    if kind == "u_geo":             # (X) -> owned geometry (geometry(stbox)), as #emit_body_unary
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &state, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(args.data[0], result, args.size(),\n"
                f"        [&](string_t in, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            {cb} *s = {bt}(in);\n            GSERIALIZED *r = {name}(s);\n            free(s);\n"
                f"            if (!r) {{ mask.SetInvalid(idx); return string_t(); }}\n"
                f"            string_t out = GSerializedToGeometry(r, state, result);\n"
                f"            free(r);\n"
                f"            return out;\n        }});\n}}\n")
    if kind.startswith("cval:") or kind.startswith("valc:"):
        # (X, base value) / (base value, X) -> a container. MEOS answers NULL where the operation
        # leaves nothing (a span minus a value it does not hold), which is SQL NULL, so this reads
        # the result through ExecuteWithNulls rather than dereferencing it.
        _k, eb, rbase = kind.split(':', 2)
        _dt, cpp2, marsh = SCALAR_ARG[eb]
        rtb = RET_CONT_TOBLOB[rbase]
        first_is_cont = _k == "cval"
        t0, t1 = ("string_t", cpp2) if first_is_cont else (cpp2, "string_t")
        a0, a1 = ("a", "a2") if first_is_cont else ("a1", "b")
        cont_arg, val_marsh = (("a", marsh) if first_is_cont
                               else ("b", marsh.replace("a2", "a1")))
        call = f"{name}(s, {val_marsh})" if first_is_cont else f"{name}({val_marsh}, s)"
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::ExecuteWithNulls<{t0}, {t1}, string_t>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&]({t0} {a0}, {t1} {a1}, ValidityMask &mask, idx_t idx) -> string_t {{\n"
                f"            {cb} *s = {bt}({cont_arg});\n"
                f"            {rbase} *r = {call};\n            free(s);\n"
                f"            if (!r) {{ mask.SetInvalid(idx); return string_t(); }}\n"
                f"            return {rtb}(result, r);\n        }});\n}}\n")
    if kind.startswith("u2text:"):  # (X, by-value scalar) -> owned C string (asText/asEWKT)
        _dt, cpp2, marsh = SCALAR_ARG[kind.split(':', 1)[1]]
        return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    BinaryExecutor::Execute<string_t, {cpp2}, string_t>(args.data[0], args.data[1], result, args.size(),\n"
                f"        [&](string_t a, {cpp2} a2) {{\n"
                f"            {cb} *s = {bt}(a);\n            char *r = {name}(s, {marsh});\n            free(s);\n"
                f"            return TakeCString(result, r);\n        }});\n}}\n")
    if kind.startswith("u2text_d:"):  # the same, with the scalar bound to its SQL DEFAULT
        dflt = kind.split(':', 1)[1]
        return (f"static void Gen_{name}_d(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    UnaryExecutor::Execute<string_t, string_t>(args.data[0], result, args.size(),\n"
                f"        [&](string_t a) {{\n"
                f"            {cb} *s = {bt}(a);\n            char *r = {name}(s, {dflt});\n            free(s);\n"
                f"            return TakeCString(result, r);\n        }});\n}}\n")
    if kind.startswith("u2iv_d:"):  # the same, with the scalar bound to its SQL DEFAULT
        dflt = kind.split(':', 2)[2]
        return (f"static void Gen_{name}_d(DataChunk &args, ExpressionState &, Vector &result) {{\n"
                f"    EnsureMeosThreadInitialized();\n"
                f"    UnaryExecutor::Execute<string_t, interval_t>(args.data[0], result, args.size(),\n"
                f"        [&](string_t in) {{\n"
                f"            {cb} *s = {bt}(in);\n            MeosInterval *r = {name}(s, {dflt});\n            free(s);\n"
                f"            return TakeInterval(r);\n        }});\n}}\n")
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

# ---------------- CONTAINER SCALARS: a number between two containers ----------------
# distance(intspan, intspan) -> integer, setDistance(tstzset, timestamptz) -> float: a by-value
# number between a set, span or span set and a second container or one base value. The C name
# carries no scope the name-keyed rules read, and the operand order the SQL surface declares is not
# always the C one -- distance(integer, intspan) and distance(intspan, integer) are one kernel,
# distance_span_int -- so the registrations come from the catalog's own sqlSignatures: each
# signature registers once, over the body whose operand order it spells, and a signature spelling
# neither order reaches no surface. The return is the declared one (scalar_ret4), so the bigint set
# distance MobilityDB declares `float` registers DOUBLE while the kernel keeps its int64.
# Admitted by @ingroup: the distances, and the comparators' `cmp`, whose eq/ne/lt/le/gt/ge
# siblings the set, span and box loops already register over every type.
CONTAINER_SCALAR_GROUPS = {"meos_setspan_dist", "meos_setspan_comp",
                           "meos_box_comp", "meos_geo_box_comp"}
# The boxes are containers of one type each: the out-parameter accessors (xMin(tbox)), the
# conversions to and from a span or a set (intspan(tbox), tbox(intset)) and the duration
# transformations (shiftTime(stbox, interval, interval)) of a retired box group reach the
# container arms, typed by the signatures MobilityDB declares.
CONT_BLOBTO = {"Set": "BlobToSet", "Span": "BlobToSpan", "SpanSet": "BlobToSpanSet",
               "TBox": "BlobToTbox", "STBox": "BlobToStbox"}
CONT_SQL_ACC = {"Set": SET_SIG_ACC, "Span": SPAN_TYPES, "SpanSet": SPANSET_TYPES,
                "TBox": {"tbox": "TboxType::tbox()"}, "STBox": {"stbox": "StboxType::stbox()"}}
def shape_container_scalar(f):
    """The C operands as [("C", Set|Span|SpanSet) | ("E", base)] for a by-value number between two
    containers or a container and one base value, else None. The distance between two time
    values is an interval, which MEOS returns owned and #scalar_ret4 marshals through
    TakeInterval, as it does for the duration of a span."""
    if f.get("group") not in CONTAINER_SCALAR_GROUPS or supported(f) is not None:
        return None
    ins, out = classify(f)
    if out is not None or len(ins) != 2:
        return None
    rb = base(f["returnType"]["canonical"])
    rn = norm(f["returnType"]["canonical"])
    interval = rb == "Interval" and rn.endswith("*") and rn.count("*") == 1
    if not interval and (rb not in NUMERIC_C_RET or rb == "bool" or "*" in rn):
        return None
    ops = []
    for p in ins:
        b, n = base(p["canonical"]), norm(p["canonical"])
        if b in CONT_BLOBTO and n.endswith("*"):
            ops.append(("C", b))
        elif b in ELEM_TO_SPAN and "*" not in n:
            ops.append(("E", b))
        else:
            return None
    return ops if any(k == "C" for k, _b in ops) else None

def container_scalar_sigs(f, ops):
    """[(duck argument types, reversed)] for each declared signature: one spelling the C operand
    order, or the reversed one a commuted SQL form declares. A position naming a type this surface
    does not register drops its signature. A base value matches where the DuckDB type of its SQL
    name is the one its C type marshals through."""
    def duck(op, sqlt):
        k, b = op
        if k == "C":
            return CONT_SQL_ACC[b].get(sqlt)
        return SCALAR_ARG[b][0] if SQL_BASE_TO_DUCK.get(sqlt) == SCALAR_ARG[b][0] else None
    out = []
    for s in f.get("sqlSignatures") or []:
        a = s.get("args") or []
        if len(a) != 2:
            continue
        fwd = [duck(ops[0], a[0]), duck(ops[1], a[1])]
        rev = [duck(ops[1], a[0]), duck(ops[0], a[1])]
        if all(fwd):
            out.append((fwd, False))
        elif all(rev):
            out.append((rev, True))
    return out

def emit_container_scalar(f, ops, rev):
    """The body for one operand order: `Gen_<fn>` reads the operands off the vectors in the C order,
    `Gen_<fn>_rev` in the commuted order its SQL form declares; both make the same call."""
    name = f["name"]
    _reg, ct, rett, rexpr = scalar_ret4(f)
    held = [1, 0] if rev else [0, 1]        # held[v] = the C operand vector v carries
    vt = lambda k, b: "string_t" if k == "C" else SCALAR_ARG[b][1]
    t0, t1 = vt(*ops[held[0]]), vt(*ops[held[1]])
    decls, call, frees = [], [], []
    for i, (k, b) in enumerate(ops):
        v = held.index(i)
        if k == "C":
            decls.append(f"            {b} *x{i} = {CONT_BLOBTO[b]}(a{v});\n")
            call.append(f"x{i}")
            frees.append(f"free(x{i});")
        else:
            call.append(SCALAR_ARG[b][2].replace("a2", f"a{v}"))
    sfx = "_rev" if rev else ""
    return (f"static void Gen_{name}{sfx}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    BinaryExecutor::Execute<{t0}, {t1}, {rett}>(args.data[0], args.data[1], result, args.size(),\n"
            f"        [&]({t0} a0, {t1} a1) {{\n"
            + "".join(decls) +
            f"            {ct} r = {name}({', '.join(call)});\n"
            f"            {' '.join(frees)}\n"
            f"            return {rexpr};\n        }});\n}}\n")

# ---- canonical names only — NO coexistence prefix, ever ----
# The North Star binding has ZERO hand-written UDFs, so there is nothing to coexist
# with: the generator OWNS every canonical name it emits, and the hand registration it
# would duplicate is DELETED in the same change (generate + delete-hand, suite-proved).
# There is no g_ transition prefix — a generated function IS the binding's function.
# RETIRED_GROUPS lists @ingroup families whose hand registrations are fully deleted, so
# the retire-safety check below verifies the generator covers every @sqlfn of each
# (dropping none). It gates SAFETY (per-family, suite-verified), not naming; naming is
# always the canonical @sqlfn.
RETIRED_GROUPS = {# The restrictions of the temporal values, the temporal points and the rigid
                  # geometries: atStbox and minusStbox reach the ternary shape with a box argument.
                  "meos_temporal_restrict", "meos_geo_restrict", "meos_rgeo_restrict",
                  # The distances of the rigid geometries: a pair of two temporal types registers
                  # its declared signatures (#sig_tt_pairs), and a pair or a geometry pair answers
                  # a distance, an instant and a geometry as well as a temporal value.
                  "meos_rgeo_dist",
                  "meos_temporal_modif", "meos_rgeo_modif",
                  "meos_geo_accessor",
                  "meos_geo_dist", "meos_pose_dist", "meos_npoint_dist", "meos_cbuffer_dist",
                  "meos_temporal_dist",
                  # The transformations of the temporal values and their families: rotate,
                  # transscale and affine reach #shape_nary_scalars, setInterp its enum read from
                  # its name, makeSimple the array return of temporal values.
                  "meos_temporal_transf", "meos_geo_base_transf", "meos_geo_transf",
                  "meos_rgeo_transf", "meos_cbuffer_transf", "meos_pose_transf",
                  "meos_posechain_transf",
                  # The bin and the tile that contain a value: getBin over the five span types,
                  # getValueTile, getTBoxTimeTile, getValueTimeTile and getStboxTimeTile, by
                  # #shape_container_make from their values and durations.
                  "meos_setspan_bin", "meos_temporal_analytics_tile", "meos_geo_tile",
                  # the tiles of a box as (index, tile) rows (#shape_box_tiles), the boxes of a
                  # spatial value and the tile of a point (#shape_box_scalars)
                  "meos_geo_bbox_split", "meos_rgeo_tile",
                  # the set and span transformations: shiftScale and the duration forms of shift,
                  # scale, expand and tprecision reach #shape_nary_scalars' container arm
                  "meos_setspan_transf",
                  # the set and span accessors: a span set's spans and dates reach the container
                  # arm of #shape_nary_scalars, the N-th value #shape_cont_outparam
                  "meos_setspan_accessor",
                  # the set and span conversions: span over a set, spanset over a set or a span
                  "meos_setspan_conversion",
                  # the set and span I/O: the typed text parsers and the WKB writers and reader of
                  # the containers the group parses (#container_from_text, #container_wkb,
                  # #container_from_wkb)
                  "meos_setspan_inout",
                  # the accessors of the rigid geometries and the quadbin cells
                  "meos_rgeo_accessor", "meos_quadbin_accessor",
                  # the boxes of a geometry: a geometry first operand reaches #shape_array
                  "meos_geo_base_bbox",
                  # the accessors of the circular buffers, poses, pose chains, point clouds and H3
                  # and S2 cells
                  "meos_cbuffer_accessor", "meos_pose_accessor", "meos_posechain_accessor",
                  "meos_pointcloud_accessor", "meos_h3_accessor", "meos_s2cell_accessor",
                  # the accessors of the sets of those families: startValue, endValue and valueN
                  # (#shape_set's u_baseval, #shape_cont_outparam's base value out-parameter)
                  "meos_cbuffer_set_accessor", "meos_json_set_accessor", "meos_npoint_set_accessor",
                  "meos_pointcloud_set_accessor", "meos_pose_set_accessor",
                  "meos_posechain_set_accessor",
                  # and of the cell sets, whose valueN hands its cell back through an out-parameter
                  # (#shape_cont_outparam), the start and end values reaching #shape_static_cell
                  "meos_quadbin_set_accessor", "meos_h3_set_accessor", "meos_s2cell_set_accessor",
                  # and their text input: <family>setFromText and FromEWKT (#container_from_text)
                  "meos_cbuffer_set_inout", "meos_json_set_inout", "meos_npoint_set_inout",
                  "meos_pointcloud_set_inout", "meos_pose_set_inout", "meos_posechain_set_inout",
                  "meos_quadbin_set_inout", "meos_s2cell_set_inout",
                  # the constructors of the sets, spans and span sets (#shape_container_make)
                  "meos_setspan_constructor",
                  # the restrictions of a temporal circular buffer, network point and pose to and
                  # outside a spatiotemporal box (atStbox, minusStbox)
                  "meos_cbuffer_restrict", "meos_npoint_restrict", "meos_pose_restrict",
                  # the simplifications: minTimeDeltaSimplify's duration reaches #shape_arg
                  "meos_temporal_analytics_simplify",
                  # the splits of a set, a span set and a temporal value into N spans or boxes
                  "meos_setspan_bbox_split", "meos_temporal_bbox_split",
                  # The JSON value accessors: valueAtTimestamp reaches the out-parameter shape,
                  # and its SQL surface answers jsonb the way MobilityDB declares it.
                  "meos_json_accessor",
                  "meos_temporal_analytics_similarity", "meos_temporal_comp_temp",
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
                  "meos_temporal_bbox_topo",
                  # Set/span/spanset distance (distance/setDistance + <->) in both operand
                  # orders, from shape_container_scalar; the hand span_distance/set_distance
                  # registrations and their executors are deleted.
                  "meos_setspan_dist",
                  # Set/span/spanset comparison (eq/ne/lt/le/gt/ge/cmp + = <> < <= > >=): the
                  # bare names and operators come from the set and span loops, cmp from
                  # shape_container_scalar; the hand set_eq/span_eq/spanset_eq... are deleted.
                  "meos_setspan_comp",
                  # The temporal accessors (getValue/getValues/valueSet/getTimestamp/instants/
                  # segmentMinDuration...), one signature per type MobilityDB declares, the set
                  # results from temporal_valueset; every hand accessor registration is deleted.
                  "meos_temporal_accessor",
                  # The box accessors, transformations, conversions, set operations and I/O of
                  # tbox and stbox
                  "meos_box_accessor", "meos_geo_box_accessor",
                  "meos_box_transf", "meos_geo_box_transf",
                  "meos_box_conversion", "meos_geo_box_conversion",
                  "meos_box_set", "meos_geo_box_set",
                  "meos_box_inout", "meos_geo_box_inout",
                  # The box constructors: tbox and stbox from a value, a span, a timestamp, a
                  # period, a geometry and their pairs
                  "meos_box_constructor", "meos_geo_box_constructor",
                  # The box comparisons (cmp, = <> < <= > >=, eq .. ge, hash, hashExtended) and
                  # the topological and position operators of tbox and stbox: the bare names and
                  # the operators from the (X,X)->bool arm of the box loops, cmp from
                  # shape_container_scalar
                  "meos_box_comp", "meos_geo_box_comp",
                  "meos_box_bbox_topo", "meos_geo_box_topo",
                  "meos_box_bbox_pos", "meos_geo_box_pos",
                  # The temporal constructors: a temporal value from a value and a timestamp, a
                  # set, a span or a span set, and the sequence constructors over an array
                  "meos_temporal_constructor", "meos_geo_constructor",
                  "meos_json_constructor", "meos_pointcloud_constructor",
                  "meos_h3_constructor", "meos_quadbin_constructor", "meos_s2cell_constructor",
                  # The functions of the temporal JSON values, each the computation of one
                  # operation (tjsonbObjectField, tjsonbPathExists and tjsonbPathExistsTz over one C
                  # function binding its tz), called as the catalog places their arguments
                  "meos_json_json",
                  # The temporal I/O: <type>FromText, FromEWKT, FromMFJSON, FromBinary, FromEWKB,
                  # FromHexEWKB, FromHexWKB and asText, asEWKT, asMFJSON, asBinary, asEWKB,
                  # asHexEWKB, asHexWKB, one signature per type MobilityDB declares
                  "meos_temporal_inout", "meos_geo_inout", "meos_json_inout",
                  "meos_pointcloud_inout",
                  # The temporal leftovers: tsample, whenTrue, the conversions of the temporal
                  # points (stbox, asMVTGeom, geoMeasure) and the native geometry operations
                  # collect(geometry[]) and distance(geometry, geometry)
                  "meos_temporal_analytics_reduction", "meos_temporal_bool",
                  "meos_geo_conversion", "meos_geo_base_spatial", "meos_geo_base_dist",
                  # The functions of the base types: round(float, integer) and the shorter form
                  # its SQL default allows
                  "meos_base_types",
                  # The geometry and geography sets: their text and WKB I/O, constructors,
                  # conversions, accessors and SRID, over the signatures declared for geomset and
                  # geogset (GEO_SET_TYPES)
                  "meos_geo_set_inout", "meos_geo_set_constructor", "meos_geo_set_conversion",
                  "meos_geo_set_accessor", "meos_geo_set_srid",
                  # The set of one base value of every other family, set(cbuffer) .. set(s2cell)
                  # (#shape_static_retired), and the set of their array (#shape_container_make)
                  "meos_cbuffer_set_conversion", "meos_h3_set_conversion",
                  "meos_json_set_conversion", "meos_npoint_set_conversion",
                  "meos_pointcloud_set_conversion", "meos_pose_set_conversion",
                  "meos_posechain_set_conversion", "meos_quadbin_set_conversion",
                  "meos_s2cell_set_conversion",
                  "meos_cbuffer_set_constructor", "meos_h3_set_constructor",
                  "meos_json_set_constructor", "meos_npoint_set_constructor",
                  "meos_pointcloud_set_constructor", "meos_pose_set_constructor",
                  "meos_posechain_set_constructor", "meos_quadbin_set_constructor",
                  "meos_s2cell_set_constructor",
                  # What a point cloud schema states: its SRID, compression and number of
                  # dimensions over the pcid that names it (#shape_static_retired)
                  "meos_pointcloud_schema_cache",
                  # The raster tile raquet: its text and WKB I/O, constructors, accessors,
                  # comparisons and box, and the sampling of a tile or a raster file along a
                  # trajectory, over the signatures MobilityDB declares for raquet
                  "meos_raster", "meos_raster_base_accessor", "meos_raster_base_constructor",
                  "meos_raster_base_inout", "meos_raster_base_comp",
                  "meos_raster_base_conversion"}
# @sqlfn names in a RETIRED group that the generator legitimately does NOT emit and that the
# hand keeps on purpose (a documented generator-shape gap, NOT a silent drop). Anything else
# uncovered in a retired group is a build-FATAL retire-safety error (see the validation below).
# The NxN *Pairs relations (ever/always int*->LIST(STRUCT(i,j)); temporal + SpanSet*** periods)
# are now GENERATED as scalar LIST(STRUCT) UDFs via shape_tgeoarr/emit_pairs_scalar (the 'pairs'
# form), over exactly the catalog sqlSignature geo types, so they are covered by construction —
# no longer listed here. Empty: every @sqlfn of a retired group is generated.
# unnest: DuckDB's own unnest cannot be overloaded, so the temporal unnest registers under the
# name the catalog gives the other engines, temporalUnnest (#shape_unnest).
# The tile and bin groups: the splits of a temporal value into its tiles (spaceSplit, timeSplit,
# valueSplit and their combinations) return arrays of temporal values that no shape takes yet;
# src/temporal/temporal.cpp keeps them.
# box2d and box3d: PostGIS's box types, which DuckDB does not have, so box2d(stbox) and
# box3d(stbox) answer no type this surface registers; no hand registration answers them either.
# stboxX: the catalog gives stbox_make, whose eleven parameters the SQL constructor fills from
# five arguments, the flags through boundArgs and the SRID read last in SQL and fourth in C; no
# shape reads that mapping, and no hand registration answers stboxX either.
# asMVTGeom: its result is the composite geom_times (a geometry and the bigint[] of its times),
# which tpoint_as_mvtgeom hands back through three out-parameters; no shape builds a STRUCT from
# out-parameters, so src/geo/tgeompoint.cpp keeps it. The clusters of a geometry array
# (clusterKMeans, clusterDBSCAN, clusterIntersecting, clusterWithin) answer an array, of integers
# or of geometries, with its count through an out-parameter, which no shape returns from an
# array of geometries; no hand registration answers them either.
# pointCloudSchemaRegister: MobilityDB declares no SQL function of the name, a schema being the
# rows of its tables pointcloud_schemas and pointcloud_dimensions, so the catalog states no
# signature for meos_pc_schema_register_dims, whose dimensions are an array of PCDimensionSpec
# with its count; src/pointcloud/pcschema.cpp keeps it.
# The functions over MobilityDB's raster type (rasterFromBinary, numBands, scaleX, summaryStats and
# the others): the raster is PostGIS's, which DuckDB does not have, so they answer or take no type
# this surface registers; no hand registration answers them either. The raquet constructor
# raquet(bytea, integer, integer, quadbin, text, float) is not among them, its name being answered
# by raquet(text): raquet_make takes `has_nodata`, which its wrapper sets from whether the nodata
# argument is NULL and which the catalog states no binding for, so src/raster/raquet.cpp keeps it.
# tjsonbSet and tjsonbSetLax register nothing: tjsonb_set reads its keys as a text ** and a count
# no marshalling fills from a text[], and tjsonbSet binds no literal for handle_null.
RETIRE_UNCOVERED_OK = {"unnest", "box2d", "box3d", "stboxX", "tjsonbSet",
                       "spaceSplit", "spaceTimeSplit", "timeSplit",
                       "valueSplit", "valueTimeSplit", "asMVTGeom",
                       "clusterKMeans", "clusterDBSCAN", "clusterIntersecting", "clusterWithin",
                       "pointCloudSchemaRegister",
                       "rasterFromBinary", "rasterFromHexWKB", "numBands", "upperLeftX",
                       "upperLeftY", "scaleX", "scaleY", "skewX", "skewY", "summaryStats",
                       "valueCount", "sameAlignment", "notSameAlignmentReason"}
def retired(f):
    return (f.get("group") or "") in RETIRED_GROUPS
TEMPORAL_CBASES = {"Temporal", "TInstant", "TSequence", "TSequenceSet"}
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
    # The portable aliases name operators over the temporal type families (the catalog's
    # portableAliases.scope.inScopeTypeFamilies), and a set, span or span set is none of them. A
    # function over no temporal operand that states its own public SQL name keeps that name:
    # <-> between two spans is `distance`, while `tDistance`, the alias <-> carries, is declared
    # only over temporal values.
    if bare and names and not any(base(p["canonical"]) in TEMPORAL_CBASES
                                  for p in f.get("params") or []):
        bare = None
    if bare and bare not in names:
        names.append(bare)
    # The operator symbol is registered only when DuckDB can parse it in an operator
    # position. Three things put it out of reach:
    #   - `::`, the cast syntax, which the parser reads as a cast to a type and never as a
    #     call; a function under that name is one no query reaches;
    #   - a name containing '#', which the lexer rejects in any operator position;
    #   - an arity past 2, since operator syntax is unary or binary. MobilityDB binds
    #     such an operator to its OWN 2-argument wrapper (`->` is `tjsonbObjectFieldOpr`,
    #     not the 3-argument `tjsonbObjectField`), so registering the symbol on the wider
    #     function would publish a name no operator syntax reaches.
    # Either way the function stays reachable through its bare name.
    wide = argsig is not None and len([x for x in argsig.strip("{}").split(",") if x.strip()]) > 2
    if op and op != "::" and "#" not in op and not wide and op not in names:
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
GENERATED_CHUNKS = 16
# The whole emitted surface, for the post-generation invariants -- which read what was WRITTEN
# rather than what was intended, and so have to see every unit.
GENERATED_TEXT = []
# The text every unit opens with, up to its anonymous namespace: the includes and the marshalling
# helpers, which #gen_aggregates opens its own unit with as well.
PREAMBLE = []
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
    "tgeompoint": "TgeompointType::tgeompoint()",
    "tgeogpoint": "TgeogpointType::tgeogpoint()",
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
# free. Without an interpolation the array makes take the default of the instants' type, linear
# where it supports one and step otherwise, the SQL default each signature declares.
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
    "    interpType interp = INTERP_NONE;\n"
    "    bool lower_inc = true, upper_inc = true;\n"
    "    if (args.ColumnCount() > 1) {{ auto &c = args.data[1]; c.Flatten(rc); Value v = c.GetValue(0); if (!v.IsNull()) interp = interptype_from_string(v.ToString().c_str()); }}\n"
    "    if (args.ColumnCount() > 2) {{ auto &c = args.data[2]; c.Flatten(rc); Value v = c.GetValue(0); if (!v.IsNull()) lower_inc = v.GetValue<bool>(); }}\n"
    "    if (args.ColumnCount() > 3) {{ auto &c = args.data[3]; c.Flatten(rc); Value v = c.GetValue(0); if (!v.IsNull()) upper_inc = v.GetValue<bool>(); }}\n"
    "    UnaryExecutor::ExecuteWithNulls<list_entry_t, string_t>(arr, result, rc,\n"
    "        [&](list_entry_t le, ValidityMask &mask, idx_t idx) -> string_t {{\n"
    "            int n = 0;\n"
    "            const Temporal **a = ListToTemporalArr(child, le, &n);\n"
    "            interpType ip = interp != INTERP_NONE ? interp :\n"
    "                (n > 0 && temptype_supports_linear((MeosType) a[0]->temptype) ? LINEAR : STEP);\n"
    "            TSequence *seq = tsequence_make((TInstant **) a, n, lower_inc, upper_inc, ip, true);\n"
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
  # from the instants' type); the per-type arg subset (bool/text: only maxt; float: adds a text interp) comes
  # from the catalog signatures, so the same body serves every callable arity.
  "make_gaps":
    "static void Gen_{fn}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
    "    EnsureMeosThreadInitialized();\n"
    "    auto rc = args.size();\n"
    "    auto &arr = args.data[0]; arr.Flatten(rc);\n"
    "    auto &child = ListVector::GetEntry(arr);\n"
    "    child.Flatten(ListVector::GetListSize(arr));\n"
    "    interpType interp = INTERP_NONE;\n"
    "    MeosInterval maxt_val; bool has_maxt = false; double maxdist = -1.0;\n"
    "    if (args.ColumnCount() > 1) {{ auto &c = args.data[1]; c.Flatten(rc); Value v = c.GetValue(0); if (!v.IsNull()) {{ maxt_val = IntervaltToInterval(v.GetValue<interval_t>()); has_maxt = true; }} }}\n"
    "    if (args.ColumnCount() > 2) {{ auto &c = args.data[2]; c.Flatten(rc); Value v = c.GetValue(0); if (!v.IsNull()) maxdist = v.GetValue<double>(); }}\n"
    "    if (args.ColumnCount() > 3) {{ auto &c = args.data[3]; c.Flatten(rc); Value v = c.GetValue(0); if (!v.IsNull()) interp = interptype_from_string(v.ToString().c_str()); }}\n"
    "    const MeosInterval *maxt = has_maxt ? &maxt_val : NULL;\n"
    "    UnaryExecutor::ExecuteWithNulls<list_entry_t, string_t>(arr, result, rc,\n"
    "        [&](list_entry_t le, ValidityMask &mask, idx_t idx) -> string_t {{\n"
    "            int n = 0;\n"
    "            const Temporal **a = ListToTemporalArr(child, le, &n);\n"
    "            interpType ip = interp != INTERP_NONE ? interp :\n"
    "                (n > 0 && temptype_supports_linear((MeosType) a[0]->temptype) ? LINEAR : STEP);\n"
    "            TSequenceSet *ss = tsequenceset_make_gaps((TInstant **) a, n, ip, maxt, maxdist);\n"
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

# ---- Temporal constructors from a base value and a time value ----
# tint(integer, timestamptz), tfloat(float, tstzspan, text), tgeompoint(geometry, tstzspanset, text),
# tpcpoint(pcpoint, tstzset), ...: a retired group's public function over a base value and a
# timestamp, a set, a span or a span set, with an interpolation past them, answering a temporal
# value. Each signature MobilityDB declares registers under its own name, argument types and
# result, with the shorter form its trailing default allows.
TIME_OPERAND = {  # C base of the time operand -> (C++ local of its vector, marshal of `a2`, freed)
    "TimestampTz": ("timestamp_tz_t", "DuckDBToMeosTimestamp(a2).value", False),
    "Set":         ("string_t", "BlobToSet(a2)", True),
    "Span":        ("string_t", "BlobToSpan(a2)", True),
    "SpanSet":     ("string_t", "BlobToSpanSet(a2)", True),
}
def base_operand(p, sqlt):
    """(DuckDB type, C++ local of its vector, marshal of `a2`, freed) of the base value of a
    temporal constructor, `sqlt` the SQL type the signature declares for it; None otherwise. A
    geography is the DuckDB GEOMETRY read as geodetic, as MobilityDB's geography input is."""
    b, n = base(p["canonical"]), norm(p["canonical"])
    if "*" not in n and b in SCALAR_ARG and b != "TimestampTz":
        d = SQL_BASE_TO_DUCK.get(sqlt)
        return (d, SCALAR_ARG[b][1], "a2", False) if d else None
    if n.count("*") != 1:
        return None
    if b == "text" and sqlt == "text":
        return ("LogicalType::VARCHAR", "string_t", "MakeText(a2)", True)
    if b == "GSERIALIZED" and sqlt.startswith(("geometry", "geography")):
        geog = sqlt.startswith("geography")
        return ("MobilityDuckGeometryType()", "string_t",
                "GeogFromGeometry(a2)" if geog else "GeometryToGSerialized(a2, 0)", True)
    if b in PTR_IN and PTR_IN[b][0] != "MD_TEMPORAL":
        return (PTR_IN[b][0], "string_t", PTR_IN[b][1] % "a2", True)
    return None

def shape_temporal_from_base(f):
    """A retired group's public constructor of a temporal value from a base value and a time
    value: [(the registered name, [DuckDB argument types], the result accessor, the marshal of the
    base value)] per callable form, with the marshal of the time value and whether the C function
    reads the interpolation; None for any other function. A geometry and a geography reach one C
    function (tgeoinst_make backs tgeometry and tgeography) and differ in their marshal. A signature
    whose interpolation the C function does not read (tpcpoint(pcpoint, tstzspan, text), over
    tpcpointseq_from_base_tstzspan, which constructs the step sequence) reads the text and refuses
    any other interpolation than its default."""
    if not retired(f) or f.get("api") != "public" or not f.get("sqlfn"):
        return None
    rb, rn = base(f["returnType"]["canonical"]), norm(f["returnType"]["canonical"])
    if rb not in ("TInstant", "TSequence", "TSequenceSet") or rn.count("*") != 1:
        return None
    ps = f.get("params") or []
    if len(ps) not in (2, 3):
        return None
    tb, tn = base(ps[1]["canonical"]), norm(ps[1]["canonical"])
    if tb not in TIME_OPERAND or tn.count("*") != (0 if tb == "TimestampTz" else 1):
        return None
    has_interp = len(ps) == 3
    if has_interp and (base(ps[2]["canonical"]) != "interpType" or "*" in norm(ps[2]["canonical"])):
        return None
    sigs = f.get("sqlSignatures") or []
    if not sigs:
        return None
    forms = []
    for s in sigs:
        args, defs = s.get("args") or [], s.get("argDefaults") or []
        if len(args) not in (2, 3) or s.get("ret") not in SIG_TEMPORAL_ACC:
            return None
        if len(args) == 3 and args[2] != "text":
            return None
        if len(args) == 2 and has_interp:
            return None
        v = base_operand(ps[0], args[0])
        t = ctor_duck_type(args[1])
        if v is None or t is None:
            return None
        dflt = defs[2] if len(args) == 3 and len(defs) == 3 else None
        if len(args) == 3 and dflt is None and not has_interp:
            return None
        name = s.get("sqlName") or f["sqlfn"]
        acc = SIG_TEMPORAL_ACC[s["ret"]]
        forms.append((name, [v[0], t], acc, v))
        if len(args) == 3:
            forms.append((name, [v[0], t, "LogicalType::VARCHAR"], acc, v))
    return (forms, TIME_OPERAND[tb], has_interp)

def from_base_suffix(value):
    """The suffix of the body #emit_temporal_from_base writes for a base value marshal: none for
    the plain one, `_geog` for the geography read from a DuckDB GEOMETRY."""
    return "_geog" if value[2].startswith("GeogFromGeometry") else ""

def emit_temporal_from_base(f, value, time_, has_interp):
    """The body of #shape_temporal_from_base: each argument read per row, the operands decoded
    and freed after the call, the interpolation read from its text where the call states it and
    from its SQL default otherwise. One body serves every form, as #emit_temporal_ctor's do: a
    signature's default reaches the body as the third column's absence."""
    name = f["name"]
    sigs = f.get("sqlSignatures") or []
    dflt = next((s["argDefaults"][2] for s in sigs
                 if len(s.get("args") or []) == 3 and len(s.get("argDefaults") or []) == 3), None)
    dlit = str(dflt).strip().strip("'") if dflt is not None else None
    L = [f"static void Gen_{name}{from_base_suffix(value)}(DataChunk &args, ExpressionState &, "
         "Vector &result) {",
         "    EnsureMeosThreadInitialized();",
         "    const idx_t rows = args.size();",
         "    const idx_t ncol = args.ColumnCount();",
         "    for (idx_t c = 0; c < ncol; c++) args.data[c].Flatten(rows);",
         f"    auto in0 = FlatVector::GetData<{value[1]}>(args.data[0]);",
         f"    auto in1 = FlatVector::GetData<{time_[0]}>(args.data[1]);",
         "    auto in2 = ncol > 2 ? FlatVector::GetData<string_t>(args.data[2]) : nullptr;",
         "    auto out = FlatVector::GetData<string_t>(result);",
         "    auto &outv = FlatVector::Validity(result);",
         "    for (idx_t i = 0; i < rows; i++) {",
         "        bool valid = true;",
         "        for (idx_t c = 0; c < ncol; c++)",
         "            if (!FlatVector::Validity(args.data[c]).RowIsValid(i)) { valid = false; break; }",
         "        if (!valid) { outv.SetInvalid(i); continue; }"]
    call = []
    if dlit is not None:
        L.append(f'        interpType interp = in2 ? interptype_from_string(in2[i].GetString().c_str())'
                 f' : interptype_from_string("{dlit}");')
        if not has_interp:
            L += [f'        if (interp != interptype_from_string("{dlit}"))',
                  f'            throw NotImplementedException("{f["sqlfn"]} constructs the {dlit} '
                  f'interpolation only");']
    L.append(f"        auto v0 = {value[2].replace('a2', 'in0[i]')};")
    L.append(f"        auto v1 = {time_[1].replace('a2', 'in1[i]')};")
    call = ["v0", "v1"] + (["interp"] if has_interp else [])
    L.append(f"        Temporal *r = (Temporal *) {name}({', '.join(call)});")
    if value[3]:
        L.append("        free(v0);")
    if time_[2]:
        L.append("        free(v1);")
    L += ["        if (!r) { outv.SetInvalid(i); continue; }",
          "        out[i] = TemporalToBlob(result, r);",
          "    }",
          "}"]
    return "\n".join(L) + "\n"

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
CONTAINER_FROMHEX_ACC = {**SET_TYPES, **GEO_SET_TYPES, **SQL_CONTAINER_ACC}
_CFH_SERIALIZE = {
    "Set":     "            return SetToBlobN(result, r, mask, idx);\n",
    "Span":    "            if (!r) { mask.SetInvalid(idx); return string_t(); }\n"
               "            return SpanToBlob(result, r);\n",
    "SpanSet": "            if (!r) { mask.SetInvalid(idx); return string_t(); }\n"
               "            return SpanSetToBlob(result, r);\n",
    "TBox":    "            if (!r) { mask.SetInvalid(idx); return string_t(); }\n"
               "            return TboxToBlob(result, r);\n",
    "STBox":   "            if (!r) { mask.SetInvalid(idx); return string_t(); }\n"
               "            return StboxToBlob(result, r);\n",
    "Temporal": "            return TemporalToBlobN(result, r, mask, idx);\n",
}
# The readers and writers of a temporal value take the arms of the containers: its text, MF-JSON
# and WKB readers (tint_in, tgeompoint_from_mfjson, temporal_from_wkb) answer it from a C string
# or from bytes, its WKB writers (temporal_as_wkb, tspatial_as_ewkb, tspatial_as_hexewkb) write it
# with the variant its endian names, registered over the temporal types each signature declares.
# The decoder extends #CONT_BLOBTO and the accessors #CONTAINER_FROMHEX_ACC with the temporal
# entries of #SIG_TEMPORAL_ACC.
IO_BLOBTO = {**CONT_BLOBTO, "Temporal": "BlobToTemporal"}
def io_acc(cont):
    """The SQL type -> DuckDB accessor map a reader or writer of `cont` registers over: the one of
    #CONTAINER_FROMHEX_ACC, or #SIG_TEMPORAL_ACC for a temporal value."""
    return SIG_TEMPORAL_ACC if cont == "Temporal" else CONTAINER_FROMHEX_ACC
def type_input(f):
    """(type, DuckDB accessor) when f is the input function of a type this surface registers, the
    public `<type>_in` reading it from one C string (intspan_in, tgeompoint_in, cbuffer_in), else
    None. A container or a temporal value is typed through #io_acc, a base value through the
    accessor its #PTR_RET entry names, which names the type itself (CbufferTypes::cbuffer())."""
    fn = f["name"]
    if f.get("api") != "public" or not fn.endswith("_in"):
        return None
    ps = f.get("params") or []
    if len(ps) != 1 or norm(ps[0]["canonical"]) != "char *":
        return None
    rb, rn = base(f["returnType"]["canonical"]), norm(f["returnType"]["canonical"])
    if not rn.endswith("*") or "const" in f["returnType"]["canonical"]:
        return None
    t = fn[:-len("_in")]
    if rb in IO_BLOBTO:
        acc = io_acc(rb).get(t)
    else:
        acc = (PTR_RET.get(rb) or (None,))[0]
        if not acc or not re.search(r'::%s\(\)$' % re.escape(t), acc):
            acc = None
    return (t, acc) if acc else None

def container_from_text(f):
    """The container a retired group's public parser answers from one C string, the shape the
    names of #CONTAINER_FROM_HEXWKB share (set_from_hexwkb, intsetFromHexWKB), derived instead
    of named, so the typed parsers (intset_in, intsetFromText) take #emit_container_from_hexwkb
    too: Set, Span, SpanSet, a box or a temporal value; and the input function of every type
    (#type_input), retired or not, which answers the function form of its cast; None
    otherwise."""
    if f.get("api") != "public" or not f.get("sqlfn"):
        return None
    ps = f.get("params") or []
    if len(ps) != 1 or norm(ps[0]["canonical"]) != "char *":
        return None
    rb, rn = base(f["returnType"]["canonical"]), norm(f["returnType"]["canonical"])
    if type_input(f):
        return rb
    if not retired(f) or rb not in IO_BLOBTO or not rn.endswith("*") \
            or "const" in f["returnType"]["canonical"]:
        return None
    return rb

def emit_container_from_hexwkb(f, cont):
    fn = f["name"]
    # a base value (cbuffer_in) serialized as #PTR_RET states it, NULL for no value
    ser = _CFH_SERIALIZE.get(cont) or (
        "            if (!r) { mask.SetInvalid(idx); return string_t(); }\n"
        "            return %s;\n" % (PTR_RET[cont][1] % "r"))
    return ("static void Gen_{fn}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            "    EnsureMeosThreadInitialized();\n"
            "    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(args.data[0], result, args.size(),\n"
            "        [&](string_t in, ValidityMask &mask, idx_t idx) -> string_t {{\n"
            "            {cont} *r = {fn}(in.GetString().c_str());\n"
            "{ser}"
            "        }});\n"
            "}}\n").format(fn=fn, cont=cont, ser=ser)

def container_wkb(f):
    """(kind, container) for a retired group's container writer, (const <C> *, uint8_t variant,
    size_t *size) answering WKB bytes (set_as_wkb, asBinary) or hex (set_as_hexwkb, asHexWKB),
    kind "bin" or "hex"; None otherwise."""
    if not retired(f) or f.get("api") != "public":
        return None
    ps = [p for p in f.get("params") or [] if norm(p["canonical"]) != "size_t *"]
    if len(ps) != 2 or base(ps[0]["canonical"]) not in IO_BLOBTO or norm(ps[1]["canonical"]) != "uint8_t":
        return None
    rn = norm(f["returnType"]["canonical"])
    kind = {"uint8_t *": "bin", "char *": "hex"}.get(rn)
    return (kind, base(ps[0]["canonical"])) if kind else None

def emit_container_wkb(f, kind, cont):
    """The writer of #container_wkb: one body serves the call without
    the endian and the call with it, the endian read by wkb_variant_from_endian (the reader of
    the variant, ENUM_READER), the bytes or the hex copied out and freed."""
    fn = f["name"]
    blobto = IO_BLOBTO[cont]
    if kind == "bin":
        rty, give = "uint8_t", "StringVector::AddStringOrBlob(result, (const char *) w, sz)"
    else:
        rty, give = "char", "StringVector::AddString(result, w)"
    body = ("                {cont} *x = {blobto}(in);\n"
            "                size_t sz = 0;\n"
            "                {rty} *w = {fn}(x, {variant}, &sz);\n"
            "                free(x);\n"
            "                if (!w) {{ mask.SetInvalid(idx); return string_t(); }}\n"
            "                string_t out = {give};\n"
            "                free(w);\n"
            "                return out;\n")
    two = body.format(cont=cont, blobto=blobto, rty=rty, fn=fn, give=give,
                      variant="wkb_variant_from_endian(endian.GetString().c_str())")
    one = body.format(cont=cont, blobto=blobto, rty=rty, fn=fn, give=give,
                      variant="wkb_variant_from_endian(\"\")")
    return ("static void Gen_{fn}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            "    EnsureMeosThreadInitialized();\n"
            "    if (args.ColumnCount() > 1) {{\n"
            "        BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(args.data[0], args.data[1], result, args.size(),\n"
            "            [&](string_t in, string_t endian, ValidityMask &mask, idx_t idx) -> string_t {{\n"
            "{two}"
            "            }});\n"
            "    }} else {{\n"
            "        UnaryExecutor::ExecuteWithNulls<string_t, string_t>(args.data[0], result, args.size(),\n"
            "            [&](string_t in, ValidityMask &mask, idx_t idx) -> string_t {{\n"
            "{one}"
            "            }});\n"
            "    }}\n"
            "}}\n").format(fn=fn, two=two, one=one)

def container_from_wkb(f):
    """The container a retired group's reader answers from WKB bytes, (const uint8_t *, size_t)
    (set_from_wkb, intsetFromBinary), as #container_from_text answers one from a C string;
    None otherwise."""
    if not retired(f) or f.get("api") != "public":
        return None
    ps = f.get("params") or []
    if len(ps) != 2 or norm(ps[0]["canonical"]) != "uint8_t *" or norm(ps[1]["canonical"]) != "size_t":
        return None
    rb, rn = base(f["returnType"]["canonical"]), norm(f["returnType"]["canonical"])
    if rb not in IO_BLOBTO or not rn.endswith("*") or "const" in f["returnType"]["canonical"]:
        return None
    return rb

def emit_container_from_wkb(f, cont):
    """The reader of #container_from_wkb, the bytes read as #emit_container_from_hexwkb reads its
    string, the result serialized as it serializes one."""
    fn = f["name"]
    return ("static void Gen_{fn}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            "    EnsureMeosThreadInitialized();\n"
            "    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(args.data[0], result, args.size(),\n"
            "        [&](string_t in, ValidityMask &mask, idx_t idx) -> string_t {{\n"
            "            {cont} *r = {fn}((const uint8_t *) in.GetData(), (size_t) in.GetSize());\n"
            "{ser}"
            "        }});\n"
            "}}\n").format(fn=fn, cont=cont, ser=_CFH_SERIALIZE[cont])

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
    # The resolution is a 32-bit integer however MEOS spells it (`int` or the `int32` of the
    # SQL-exposed integer convention), as the integer return check of this file reads it.
    if (b0 == "GSERIALIZED" and ptr(ins[0]) and b1 in ("int", "int32_t")
            and "*" not in norm(ins[1]["canonical"])):
        if rb == "Set" and rn.endswith("*"):
            return "geo2set"
        if rb == "uint64_t" and "*" not in rn:
            return "geo2scalar"
    if (b0 == "Set" and ptr(ins[0]) and b1 == "Temporal" and ptr(ins[1])
            and rb in ("int", "int32_t") and "*" not in rn):
        return "settemp_ebool"
    # The cell-vs-temporal comparisons belong to the GENERIC binary shape, which reaches
    # them now that `arg_type` resolves a by-value cell id from the function's own scope.
    # Claiming them here too emits a second body under the same name, and the compiler is
    # what says so: `redefinition of Gen_ever_eq_h3index_th3index`. Yielding them also
    # widens the surface, since this pass fires for H3 alone while the generic shape serves
    # quadbin and s2cell on the same rule.
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
    # a cell id the signature declares (quadbin(raquet)) is its cell type, which #shape_sig_call
    # reads from the signature, not the unsigned integer of its C type
    if rb in CELL_UINT and f.get("sqlReturnType") in CELL_SQL:
        return None
    if rb in BYVAL_RET and "*" not in rn:
        return (bb, rb)
    return None

def emit_baseval_scalar(f, bb, rb):
    name = f["name"]; marshal = PTR_IN[bb][1] % "in"
    cct, rett, rexpr = byval_ret3(rb)
    ex, prm, ann, ret = scalar_exec(f, rett, rexpr)
    return (f"static void Gen_{name}(DataChunk &args, ExpressionState &, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n"
            f"    UnaryExecutor::{ex}<string_t, {rett}>(args.data[0], result, args.size(),\n"
            f"        [&](string_t in{prm}){ann} {{\n"
            f"            {bb} *v = {marshal};\n            {cct} r = {name}(v);\n            free(v);\n"
            f"            {ret}\n        }});\n}}\n")

# ---------------- STATIC cell surface: h3index / quadbin / s2cell with no temporal operand ----------------
# A cell index is carried BY VALUE, the eight bytes of an H3Index / Quadbin / S2CellId, which C spells
# `uint64_t` or `unsigned long` and which is equally an ordinary hash seed. The temporal cell surface
# resolves which one a parameter is from the function's registration scope (arg_type), but a static
# function has no temporal operand and so no scope: reg_scope answers None for every `h3_*`,
# `quadbin_*` and `s2cell_*` kernel, and supported() rejects its cell argument. What does state the
# type is the catalog's own sqlSignatures, which spell each position in the SQL type MobilityDB
# declares -- `cellToParent(quadbin, integer)` -- so this shape reads every argument and the result
# from the signature and the C parameter list together, and registers one overload per signature.
# The value conversions are the hand executor's (src/quadbin/tquadbin.cpp Quadbin_tile_to_cell):
# a cell type is a BIGINT alias, so its vector is int64_t, read into the MEOS call as uint64_t and
# written back as int64_t, the id keeping its bits both ways.
CELL_SQL = {_acc_sqlname(v): v for v in CELL_BASEVAL.values()}         # h3index / quadbin / s2cell

def static_cell_arg(p, sqlt, i, wide=False):
    """(DuckDB type, executor C++ type, MEOS-call expression, pre-call lines, post-call lines) for
    argument `i`, whose C parameter is `p` and whose declared SQL type is `sqlt`; None if unmappable.
    With `wide`, as #shape_static_retired reads them, a text, a geometry or geography and a base
    value behind a pointer are read as #base_operand reads the base value of a constructor, into a
    local freed after the call."""
    b, n, v = base(p["canonical"]), norm(p["canonical"]), "a%d" % i
    if wide and n.count("*") == 1 and b not in ("Set", "Span", "SpanSet", "char"):
        bo = base_operand(p, sqlt)
        if bo is None:
            return None
        dt, _cpp, marsh, _freed = bo
        loc = "p%d" % i
        return (dt, "string_t", loc, "%s *%s = %s;\n            " % (b, loc, marsh.replace("a2", v)),
                "free(%s); " % loc)
    if b in CELL_UINT and "*" not in n:
        if sqlt in CELL_SQL:
            return (CELL_SQL[sqlt], "int64_t", "static_cast<uint64_t>(%s)" % v, "", "")
        if b in SCALAR_ARG:                  # the *_hash_extended seed, a native UBIGINT
            dt, cpp, _ = SCALAR_ARG[b]
            return (dt, cpp, v, "", "")
        return None
    if "*" not in n and b in SCALAR_ARG:
        dt, cpp, ex = SCALAR_ARG[b]
        return (dt, cpp, ex.replace("a2", v), "", "")
    if "*" not in n and b == "uint32_t" and sqlt in SQL_SCALAR_RET:
        # A resolution or tile coordinate: MEOS takes it unsigned, MobilityDB declares it `integer`.
        return (SQL_SCALAR_RET[sqlt], "int32_t", "static_cast<uint32_t>(%s)" % v, "", "")
    if b == "char" and n.count("*") == 1 and sqlt == "text":
        return ("LogicalType::VARCHAR", "string_t", "%s.GetString().c_str()" % v, "", "")
    if b in ("Set", "Span") and n.endswith("*"):
        acc = SET_SIG_ACC.get(sqlt) if b == "Set" else SQL_BASE_TO_DUCK.get(sqlt)
        if not acc:
            return None
        loc = "p%d" % i
        return (acc, "string_t", loc,
                "%s *%s = %s;\n            " % (b, loc, PTR_IN[b][1] % v), "free(%s); " % loc)
    return None

def static_cell_ret(f, sqlr):
    """(DuckDB type, executor C++ type, C type of the MEOS result, return lines, NULL-able) for the
    result of `f` declared as SQL type `sqlr`; None if unmappable."""
    rc = f["returnType"]["canonical"]; b, n = base(rc), norm(rc)
    if b in CELL_UINT and "*" not in n:
        if sqlr in CELL_SQL:
            return (CELL_SQL[sqlr], "int64_t", "uint64_t", "return static_cast<int64_t>(r);", False)
        # A bare uint64 that is not a cell is the *_hash_extended value (native unsigned).
        return ("LogicalType::UBIGINT", "uint64_t", "uint64_t", "return r;", False)
    if b == "Set" and n.endswith("*"):
        acc = SET_SIG_ACC.get(sqlr)
        if not acc:
            return None
        return (acc, "string_t", "Set *", "return SetToBlobN(result, r, mask, idx);", True)
    if b == "STBox" and n.endswith("*"):
        return (SQL_BASE_TO_DUCK["stbox"], "string_t", "STBox *",
                "if (!r) { mask.SetInvalid(idx); return string_t(); }\n"
                "            return StboxToBlob(result, r);", True)
    if b == "GSERIALIZED" and n.endswith("*"):
        return ("MobilityDuckGeometryType()", "string_t", "GSERIALIZED *",
                "if (!r) { mask.SetInvalid(idx); return string_t(); }\n"
                "            string_t out = GSerializedToGeometry(r, state, result);\n"
                "            free(r);\n            return out;", True)
    if b == "char" and n.endswith("*"):
        dt, cct, rett, rexpr = scalar_ret4(f)
        return (dt, rett, cct, "if (!r) { mask.SetInvalid(idx); return string_t(); }\n"
                "            return %s;" % rexpr, True)
    if b in BYVAL_RET and "*" not in n:
        dt, cct, rett, rexpr = scalar_ret4(f)
        guard = static_null_guard(f)
        if guard is not None:
            return (dt, rett, cct, "if (%s) { mask.SetInvalid(idx); return %s(); }\n"
                    "            return %s;" % (guard, rett, rexpr), True)
        return (dt, rett, cct, "return %s;" % rexpr, False)
    return None

def static_null_guard(f):
    """The test on `r` under which the wrapper of `f` answers NULL for its by-value result, as the
    catalog states it (`shape.nullableResult`), else None. A guard SCALAR_NULL_GUARD names maps as
    it does there. Any other guard over the result, the parameters of `f` and public MEOS functions
    reads `r` for the result and @A<i>@ for the i-th parameter, which #emit_static_cell replaces
    with the operand it passes: pointCloudSchemaSRID answers NULL where meos_pc_schema_ndims(pcid)
    < 0, as MobilityDB's Pointcloud_schema_srid does."""
    nr = (f.get("shape") or {}).get("nullableResult")
    if nr in SCALAR_NULL_GUARD:
        return SCALAR_NULL_GUARD[nr]
    m = re.fullmatch(r'if \((.+)\)', nr or "")
    if not m:
        return None
    params = [p["name"] for p in classify(f)[0]]
    for tok in re.findall(r'[A-Za-z_]\w*', m.group(1)):
        if tok != "result" and tok not in params \
                and (FN_BY_NAME.get(tok) or {}).get("api") != "public":
            return None
    expr = re.sub(r'\bresult\b', 'r', m.group(1))
    for i, nm in enumerate(params):
        expr = re.sub(r'\b%s\b' % re.escape(nm), '@A%d@' % i, expr)
    return expr

def shape_static_cell(f):
    """[(sqlSignature, [arg tuples], ret tuple)] for a static cell function, else None. One entry per
    declared signature the marshalling reaches; the set constructor over a registered set type is
    left to the hand `Value_to_set` loop, which registers `set(<base>)` for every SetTypes type."""
    if f.get("api") != "public" or not f.get("sqlfn"):
        return None
    if re.search(r'_(out|in|send|recv)$', f["sqlfn"]):
        return None
    if reg_scope(f["name"]) is not None or unregistered_family_ref(f["name"]) is not None:
        return None
    ins, out = classify(f)
    if out is not None or not 1 <= len(ins) <= 3:
        return None
    # What makes a function part of the static cell surface is a by-value cell id among its C
    # operands or as its C result. A function over cell SETS alone (set_eq, union_set_set over
    # quadbinset) is a set-family kernel whose body the set loop already emits; claiming it here
    # would emit that body twice.
    byval_cell = lambda c: base(c) in CELL_UINT and "*" not in norm(c)
    if not (any(byval_cell(p["canonical"]) for p in ins) or byval_cell(f["returnType"]["canonical"])):
        return None
    sigs = []
    for s in f.get("sqlSignatures") or []:
        if len(s["args"]) != len(ins):
            continue
        # The C type alone cannot say a uint64 is a cell -- set_hash_extended answers a uint64 hash
        # over a set -- so a position counts only where the signature ALSO declares a cell type.
        if not (any(byval_cell(p["canonical"]) and t in CELL_SQL for p, t in zip(ins, s["args"]))
                or (byval_cell(f["returnType"]["canonical"]) and s["ret"] in CELL_SQL)):
            continue
        if s.get("sqlName", f["sqlfn"]) == "set" and s["ret"] in SET_TYPES:
            continue
        args = [static_cell_arg(p, t, i) for i, (p, t) in enumerate(zip(ins, s["args"]))]
        ret = static_cell_ret(f, s["ret"])
        if ret is None or any(a is None for a in args):
            continue
        sigs.append((s, args, ret))
    return sigs or None

def shape_static_retired(f):
    """[(sqlSignature, [arg tuples], ret tuple)] for a public function of a retired group over no
    temporal value that no other shape emits, one entry per declared signature #static_cell_arg
    (wide) and #static_cell_ret marshal: round(float, integer), set(text), set(cbuffer),
    set(h3index), SRID(geomset), startValue(geomset). #gen_cpp asks it last, of the functions with
    no body yet, so it never takes a function from another shape."""
    if not retired(f) or f.get("api") != "public" or not f.get("sqlfn"):
        return None
    if re.search(r'_(out|in|send|recv)$', f["sqlfn"]):
        return None
    if unregistered_family_ref(f["name"]) is not None:
        return None
    ins, out = classify(f)
    if out is not None or not 1 <= len(ins) <= 3:
        return None
    if any(base(p["canonical"]) in TEMPORAL_CBASES for p in ins):
        return None
    # a set read or answered (the sets of the families and of geometries, set(text)), or a
    # function of by-value numbers alone answering a number or a text (round(float, integer),
    # pointCloudSchemaCompression(integer))
    is_set = lambda c: base(c) == "Set" and norm(c).endswith("*")
    byval = lambda c: "*" not in norm(c) and (base(c) in SCALAR_ARG or base(c) in BYVAL_RET)
    is_text = lambda c: base(c) == "char" and norm(c).endswith("*")
    rc = f["returnType"]["canonical"]
    if not (any(is_set(p["canonical"]) for p in ins) or is_set(rc)
            or (all(byval(p["canonical"]) for p in ins) and (byval(rc) or is_text(rc)))):
        return None
    sigs = []
    for s in f.get("sqlSignatures") or []:
        if len(s["args"]) != len(ins) or re.search(r'_(out|in|send|recv)$', s.get("sqlName") or ""):
            continue
        args = [static_cell_arg(p, t, i, wide=True) for i, (p, t) in enumerate(zip(ins, s["args"]))]
        ret = static_cell_ret(f, s["ret"])
        if ret is None or any(a is None for a in args):
            continue
        sigs.append((s, args, ret))
    return sigs or None

def emit_static_cell(f, args, ret, sfx="", dval=None):
    """The body of a static function, Gen_<fn><sfx>: `sfx` names a second body of the same function
    whose operands marshal otherwise (a geography where the first reads a geometry). With `dval`,
    the shorter body Gen_<fn><sfx>_d its trailing SQL default answers, that literal in the last
    argument's place, as #emit_nary_scalars passes one."""
    if dval is not None:
        args, sfx = args[:-1], sfx + "_d"
    name, n = f["name"], len(args)
    ex = {1: "UnaryExecutor", 2: "BinaryExecutor", 3: "TernaryExecutor"}[n]
    _dt, rett, cct, rlines, nulls = ret
    for i, a in enumerate(args):        # the operands a guard of #static_null_guard reads
        rlines = rlines.replace("@A%d@" % i, a[2])
    tps = ", ".join(a[1] for a in args)
    vecs = ", ".join("args.data[%d]" % i for i in range(n))
    params = ", ".join("%s a%d" % (a[1], i) for i, a in enumerate(args))
    pre, post = "".join(a[3] for a in args), "".join(a[4] for a in args)
    call = ", ".join([a[2] for a in args] + ([dval] if dval is not None else []))
    state = "state" if "GSerializedToGeometry" in rlines else ""
    if nulls:
        head = (f"    {ex}::ExecuteWithNulls<{tps}, {rett}>({vecs}, result, args.size(),\n"
                f"        [&]({params}, ValidityMask &mask, idx_t idx) -> {rett} {{\n")
    else:
        head = (f"    {ex}::Execute<{tps}, {rett}>({vecs}, result, args.size(),\n"
                f"        [&]({params}) -> {rett} {{\n")
    return (f"static void Gen_{name}{sfx}(DataChunk &args, ExpressionState &{state}, Vector &result) {{\n"
            f"    EnsureMeosThreadInitialized();\n{head}"
            f"            {pre}{cct} r = {name}({call});\n"
            + (f"            {post}\n" if post else "")
            + f"            {rlines}\n        }});\n}}\n")

# ---------------- Calls placed by the catalog: one body per declared signature ----------------
# A public function of a retired group that no shape above emits is called the way its PostgreSQL
# wrapper calls it, which the catalog states: the C parameter each SQL argument fills (`sqlArgParams`,
# else the C parameters in order), the pointer and length pairs one SQL argument fills (a bytea
# fills a byte string and its size, an array of a base value fills the array and its count:
# `inputArrays`), the parameters MEOS writes back (`outParams`), the literal a parameter no
# argument fills takes (`boundArgs`), and the value a NULL argument binds (`nullDefaultBinds`). A
# signature whose parameters these leave unfilled registers nothing.

def sig_call_arg(f, p, t, k, arrays, lenp):
    """The marshalling of SQL argument `k`, declared as SQL type `t`, into C parameter `p` (and, for
    a pointer and length pair, into the length parameter `lenp`): a dict with the DuckDB type `dt`,
    the C++ type of its vector `cpp`, the lines decoding it `pre`, the C expressions of the
    parameters it fills `cexpr` (name -> expression), the lines freeing what it decoded `post`, and
    its `kind`; None if no marshalling reaches it."""
    b, n, v = base(p["canonical"]), norm(p["canonical"]), "in%d[i]" % k
    a = arrays.get(p["name"])
    if a is not None:
        elem = norm((a.get("element") or {}).get("canonical") or "")
        ln = lenp["cType"] if lenp else None
        if lenp is None:
            return None
        if elem == "uint8_t" and t == "bytea":
            return dict(kind="bytes", dt="LogicalType::BLOB", cpp="string_t", pre=[], post=[],
                        cexpr={p["name"]: "(const uint8_t *) %s.GetData()" % v,
                               lenp["name"]: "(%s) %s.GetSize()" % (ln, v)})
        eb = base(elem)
        acc = BASE_LOGICAL.get(t[:-2]) if t.endswith("[]") else None
        if eb in BASEVAL_PTR_IN and elem.endswith("*") and acc and PTR_IN[eb][0] == acc:
            # the elements of the list, its NULL elements left out as #emit_nary_scalars leaves
            # them out of an array of geometries; an empty list still hands MEOS an allocated
            # array, as PostgreSQL does, so MEOS answers for its count, not for a NULL pointer
            pre = ["const list_entry_t le%d = %s;" % (k, v),
                   "vector<%s *> v%d;" % (eb, k),
                   "v%d.reserve(le%d.length + 1);" % (k, k),
                   "for (idx_t j = 0; j < le%d.length; j++)" % k,
                   "    if (ch%dv.RowIsValid(le%d.offset + j))" % (k, k),
                   "        v%d.push_back(%s);" % (k, PTR_IN[eb][1] % ("ch%dd[le%d.offset + j]" % (k, k)))]
            return dict(kind="list", dt="LogicalType::LIST(%s)" % acc, cpp="list_entry_t", pre=pre,
                        post=["for (auto *e : v%d) free(e);" % k],
                        cexpr={p["name"]: "(%s) v%d.data()" % (p["cType"], k),
                               lenp["name"]: "(%s) v%d.size()" % (ln, k)})
        return None
    if n.count("*") == 1 and b in TEMPORAL_CBASES:
        acc = SIG_TEMPORAL_ACC.get(t)
        if not acc:
            return None
        return dict(kind="ptr", dt=acc, cpp="string_t",
                    pre=["Temporal *p%d = BlobToTemporal(%s);" % (k, v)], post=["free(p%d);" % k],
                    cexpr={p["name"]: "p%d" % k})
    if n.count("*") == 1 and b in PTR_IN:
        # or the SQL type the pointer's own DuckDB type is named after (jsonpath for JsonPath,
        # TJsonbTypes::jsonpath()), a type no base, span or set map carries
        acc = (BASE_LOGICAL.get(t) or SQL_BASE_TO_DUCK.get(t) or SET_SIG_ACC.get(t)
               or SPANSET_TYPES.get(t)
               or (PTR_IN[b][0] if PTR_IN[b][0].endswith("::%s()" % t) else None))
        if not acc or (b in BASEVAL_PTR_IN and PTR_IN[b][0] != acc):
            return None
        return dict(kind="ptr", dt=acc, cpp="string_t",
                    pre=["%s *p%d = %s;" % (b, k, PTR_IN[b][1] % v)], post=["free(p%d);" % k],
                    cexpr={p["name"]: "p%d" % k})
    if b == "char" and n.count("*") == 1 and t == "text":
        return dict(kind="cstr", dt="LogicalType::VARCHAR", cpp="string_t",
                    pre=["std::string s%d = %s.GetString();" % (k, v)], post=[],
                    cexpr={p["name"]: "s%d.c_str()" % k})
    if b == "text" and n.count("*") == 1 and t == "text":
        return dict(kind="ptr", dt="LogicalType::VARCHAR", cpp="string_t",
                    pre=["text *p%d = MakeText(%s);" % (k, v)], post=["free(p%d);" % k],
                    cexpr={p["name"]: "p%d" % k})
    if "*" in n:
        return None
    if b in CELL_UINT and t in CELL_SQL:
        return dict(kind="byval", dt=CELL_SQL[t], cpp="int64_t", pre=[], post=[],
                    cexpr={p["name"]: "static_cast<uint64_t>(%s)" % v})
    if b in ENUM_READER and t == "text":
        return dict(kind="byval", dt="LogicalType::VARCHAR", cpp="string_t", pre=[], post=[],
                    cexpr={p["name"]: "%s(%s.GetString().c_str())" % (ENUM_READER[b], v)})
    if b in SCALAR_ARG and SQL_SCALAR_RET.get(t) == SCALAR_ARG[b][0]:
        dt, cpp, ex = SCALAR_ARG[b]
        return dict(kind="byval", dt=dt, cpp=cpp, pre=[], post=[],
                    cexpr={p["name"]: ex.replace("a2", v)})
    return None

def sig_call_literal(p, raw):
    """The C argument SQL literal `raw` stands for in C parameter `p` (#forward_literal, and a C
    string for a text), else None."""
    b, n = base(p["canonical"]), norm(p["canonical"])
    if b == "char" and n.count("*") == 1:
        text = str(raw).strip()
        if text.upper() == "NULL":
            return None
        return '"%s"' % text.strip("'").replace("\\", "\\\\").replace('"', '\\"')
    if b in CELL_UINT and "*" not in n:
        lit = str(raw).strip()
        return None if lit.upper() == "NULL" else "static_cast<uint64_t>(%s)" % lit
    return forward_literal(p, raw)

def sig_call_ret(f, s, outs):
    """(DuckDB type, C++ type of the result vector, the call and result lines, where `CALL` stands
    for the call) for the result of `f` declared as `s["ret"]`, its out-parameters `outs` read back;
    None if no marshalling reaches it."""
    rc = f["returnType"]["canonical"]
    rb, rn, sqlr = base(rc), norm(rc), s.get("ret")
    sizes = [p for p in outs if norm(p["canonical"]) == "size_t *"]
    vals = [p for p in outs if norm(p["canonical"]) != "size_t *"]
    null = "outv.SetInvalid(i); continue;"
    if vals:
        # a value handed back beside a flag stating there is one (bandNoDataValue)
        if len(vals) != 1 or sizes or rn != "bool":
            return None
        on = norm(vals[0]["canonical"])
        dt = SQL_SCALAR_RET.get(sqlr)
        if on not in OUTPARAM_LOCAL or dt is None or OUTPRIM.get(on) != dt:
            return None
        cloc, cpp = OUTPARAM_LOCAL[on]
        return (dt, cpp, ["%s v{};" % cloc,
                          "bool found = CALL;"], ["if (!found) { %s }" % null,
                                                  "out[i] = (%s) v;" % cpp], {vals[0]["name"]: "&v"})
    if len(sizes) > 1:
        return None
    if sizes:
        # a byte string or a text with its length (pixels, asBinary, asHexWKB)
        size = {sizes[0]["name"]: "&sz"}
        if rn == "uint8_t *" and sqlr == "bytea":
            return ("LogicalType::BLOB", "string_t", ["size_t sz = 0;", "uint8_t *r = CALL;"],
                    ["if (!r) { %s }" % null,
                     "out[i] = StringVector::AddStringOrBlob(result, (const char *) r, sz);",
                     "free(r);"], size)
        if rn == "char *" and sqlr == "text":
            return ("LogicalType::VARCHAR", "string_t", ["size_t sz = 0;", "char *r = CALL;"],
                    ["if (!r) { %s }" % null, "out[i] = StringVector::AddString(result, r);",
                     "free(r);"], size)
        return None
    if rn.endswith("*") and "const" not in rc:
        if rb in TEMPORAL_PTR_RET:
            acc = SIG_TEMPORAL_ACC.get(sqlr)
            if not acc:
                return None
            return (acc, "string_t", ["Temporal *r = (Temporal *) CALL;"],
                    ["if (!r) { %s }" % null, "out[i] = TemporalToBlob(result, r);"], {})
        if rb in PTR_RET and rb not in TEMPORAL_PTR_RET:
            acc = BASE_LOGICAL.get(sqlr) or SQL_BASE_TO_DUCK.get(sqlr) or SET_SIG_ACC.get(sqlr)
            if not acc or (rb in BASEVAL_PTR_RET and PTR_RET[rb][0] != acc):
                return None
            return (acc, "string_t", ["%s *r = CALL;" % rb],
                    ["if (!r) { %s }" % null, "out[i] = %s;" % (PTR_RET[rb][1] % "r")], {})
        if rb == "char" and sqlr == "text":
            return ("LogicalType::VARCHAR", "string_t", ["char *r = CALL;"],
                    ["if (!r) { %s }" % null, "out[i] = TakeCString(result, r);"], {})
        return None
    if "*" in rn:
        return None
    if rb in CELL_UINT and sqlr in CELL_SQL:
        return (CELL_SQL[sqlr], "int64_t", ["uint64_t r = CALL;"],
                ["out[i] = static_cast<int64_t>(r);"], {})
    # a fallible predicate, -1 on error, 1 or 0 otherwise (eRasterValue)
    if rb in ("int", "int32_t") and sqlr == "boolean":
        return ("LogicalType::BOOLEAN", "bool", ["int r = CALL;"],
                ["if (r < 0) { %s }" % null, "out[i] = (r != 0);"], {})
    if rb in BYVAL_RET:
        dt, cct, rett, rexpr = scalar_ret4(f)
        guard = SCALAR_NULL_GUARD.get((f.get("shape") or {}).get("nullableResult"))
        return (dt, rett, ["%s r = CALL;" % cct],
                (["if (%s) { %s }" % (guard, null)] if guard else []) + ["out[i] = %s;" % rexpr], {})
    return None

# The public functions declaring each SQL signature, (name, argument types) -> count, filled on
# first use from FN_BY_NAME.
SIG_CLAIMS = Counter()

def sig_claims(s, f):
    """How many public functions declare signature `s` of `f`: more than one when the PostgreSQL
    wrapper picks the kernel from the value (nearestApproachDistance(tbox, tbox) over
    nad_tboxint_tboxint, nad_tboxbigint_tboxbigint and nad_tboxfloat_tboxfloat), so the signature
    names no single function to call."""
    if not SIG_CLAIMS:
        for g in FN_BY_NAME.values():
            if g.get("api") == "public" and g.get("sqlfn"):
                for t in g.get("sqlSignatures") or []:
                    SIG_CLAIMS[((t.get("sqlName") or g["sqlfn"]).lower(), tuple(t["args"]))] += 1
    return SIG_CLAIMS[((s.get("sqlName") or f["sqlfn"]).lower(), tuple(s["args"]))]

def shape_sig_call(f):
    """[(signature, [argument marshallings], parameter literals, NULL binds, result)] for a public
    function of a retired group, one entry per declared signature the catalog places on its C
    parameters (see the section comment) and no other public function declares; None if none.
    #gen_cpp asks it last, of the functions with no body yet, after #shape_static_retired."""
    if not retired(f) or f.get("api") != "public" or not f.get("sqlfn"):
        return None
    if re.search(r'_(out|in|send|recv)$', f["sqlfn"]) or unregistered_family_ref(f["name"]):
        return None
    shape = f.get("shape") or {}
    ps = f.get("params") or []
    byname = {p["name"]: p for p in ps}
    outn = set(shape.get("outParams") or [])
    if ps and norm(ps[-1]["canonical"]) == "size_t *" and "const" not in ps[-1]["canonical"]:
        outn.add(ps[-1]["name"])
    arrays = {a["param"]: a for a in shape.get("inputArrays") or []
              if (a.get("lengthFrom") or {}).get("kind") == "param"}
    lenof = {a["lengthFrom"]["name"]: a["param"] for a in arrays.values()}
    outs = [p for p in ps if p["name"] in outn]
    facing = [p for p in ps if p["name"] not in outn and p["name"] not in lenof]
    plans = []
    for s in f.get("sqlSignatures") or []:
        if re.search(r'_(out|in|send|recv)$', s.get("sqlName") or "") or sig_claims(s, f) > 1:
            continue
        # the C parameters in order, but for those the signature binds to a literal (tz of
        # tjsonbPathExists and tjsonbPathExistsTz, one C function under two SQL names)
        sbound = s.get("boundArgs") or {}
        order = (s.get("sqlArgParams") or shape.get("sqlArgParams")
                 or [p["name"] for p in facing if p["name"] not in sbound])
        if len(order) != len(s["args"]) or any(nm not in byname for nm in order):
            continue
        bound = {}
        for p in facing:
            if p["name"] in order:
                continue
            lit = (s.get("boundArgs") or {}).get(p["name"])
            lit = sig_call_literal(p, lit) if lit is not None else None
            if lit is None:
                bound = None        # a parameter no argument fills and no literal binds
                break
            bound[p["name"]] = lit
        if bound is None:
            continue
        args = []
        for k, (nm, t) in enumerate(zip(order, s["args"])):
            lenp = byname.get((arrays.get(nm) or {}).get("lengthFrom", {}).get("name", ""))
            args.append(sig_call_arg(f, byname[nm], t, k, arrays, lenp))
        if any(a is None for a in args):
            continue
        # the value a NULL argument binds, for a by-value argument
        nb = {}
        for kk, binds in (s.get("nullDefaultBinds") or {}).items():
            k = int(kk)
            if k >= len(args) or args[k]["kind"] != "byval":
                nb = None
                break
            lits = {cn: sig_call_literal(byname[cn], lit) for cn, lit in binds.items() if cn in byname}
            if len(lits) != len(binds) or None in lits.values() or set(lits) != set(args[k]["cexpr"]):
                nb = None
                break
            nb[k] = lits
        if nb is None:
            continue
        ret = sig_call_ret(f, s, outs)
        if ret is None:
            continue
        plans.append((s, order, args, bound, nb, ret))
    return plans or None

def sig_call_forms(f, plan):
    """[(number of arguments, {C parameter: literal} of the omitted ones)] a signature registers:
    the full form, and each shorter one its trailing SQL defaults allow, an omitted argument taking
    the value its NULL binds or the literal its default names."""
    s, order, args, _bound, nb, _ret = plan
    n = len(args)
    byname = {p["name"]: p for p in f.get("params") or []}
    defs = s.get("argDefaults") or [None] * n
    forms, lits = [(n, {})], {}
    for k in range(n - 1, -1, -1):
        d = defs[k] if k < len(defs) else None
        if d is None:
            break
        if k in nb and str(d).strip().upper() == "NULL":
            add = dict(nb[k])
        elif len(args[k]["cexpr"]) == 1:
            cn = next(iter(args[k]["cexpr"]))
            lit = sig_call_literal(byname[cn], d)
            if lit is None:
                break
            add = {cn: lit}
        else:
            break
        lits = {**lits, **add}
        forms.append((k, dict(lits)))
    return forms

def emit_sig_call(f, plan, sfx, nargs, omitted):
    """The per-row body of a #shape_sig_call form, Gen_<fn><sfx>, reading its first `nargs`
    arguments, the omitted ones taking their literals, as #emit_nary_scalars reads each operand."""
    s, order, args, bound, nb, ret = plan
    name = f["name"]
    args = args[:nargs]
    dt, rcpp, rcall, rwrite, outx = ret
    L = [f"static void Gen_{name}{sfx}(DataChunk &args, ExpressionState &, Vector &result) {{",
         "    EnsureMeosThreadInitialized();",
         "    const idx_t rows = args.size();",
         f"    for (idx_t c = 0; c < {nargs}; c++) args.data[c].Flatten(rows);"]
    for k, a in enumerate(args):
        if a["kind"] == "list":
            L += [f"    auto in{k} = ListVector::GetData(args.data[{k}]);",
                  f"    auto &ch{k} = ListVector::GetEntry(args.data[{k}]);",
                  f"    ch{k}.Flatten(ListVector::GetListSize(args.data[{k}]));",
                  f"    auto ch{k}d = FlatVector::GetData<string_t>(ch{k});",
                  f"    auto &ch{k}v = FlatVector::Validity(ch{k});"]
        else:
            L.append(f"    auto in{k} = FlatVector::GetData<{a['cpp']}>(args.data[{k}]);")
    L += [f"    auto out = FlatVector::GetData<{rcpp}>(result);",
          "    auto &outv = FlatVector::Validity(result);",
          "    for (idx_t i = 0; i < rows; i++) {"]
    for k in range(nargs):
        if k in nb:
            L.append(f"        const bool n{k} = !FlatVector::Validity(args.data[{k}]).RowIsValid(i);")
        else:
            L.append(f"        if (!FlatVector::Validity(args.data[{k}]).RowIsValid(i)) {{ outv.SetInvalid(i); continue; }}")
    cexpr = dict(bound)
    cexpr.update(omitted)
    pre, post = [], []
    for k, a in enumerate(args):
        pre += a["pre"]
        post += a["post"]
        for cn, ex in a["cexpr"].items():
            cexpr[cn] = ("(n%d ? %s : %s)" % (k, nb[k][cn], ex)) if k in nb else ex
    cexpr.update(outx)
    call = "%s(%s)" % (name, ", ".join(cexpr[p["name"]] for p in f.get("params") or []))
    L += ["        " + x for x in pre]
    L += ["        " + x.replace("CALL", call) for x in rcall]
    L += ["        " + x for x in post]
    L += ["        " + x for x in rwrite]
    L += ["    }",
          "    if (rows == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);",
          "}", ""]
    return "\n".join(L)

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
        tfb = shape_temporal_from_base(f)
        if tfb:
            # A temporal value from a base value and a time value, registered per declared
            # signature with the shorter form of its trailing default (#shape_temporal_from_base)
            STATE["grp"] = f.get("group") or "meos_ungrouped"
            forms, time_, has_interp = tfb
            emitted_values = []
            for nm, slots, acc, value in forms:
                if value not in emitted_values:
                    emitted_values.append(value)
                    bodies.append(emit_temporal_from_base(f, value, time_, has_interp))
                specific_regs.append(
                    f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                    f'"{reg_name(nm, f)}", {{{", ".join(slots)}}}, {acc}, '
                    f'Gen_{f["name"]}{from_base_suffix(value)}));')
            continue
        tc = shape_temporal_ctor(f)
        if tc:
            # Temporal constructor / transform NAME FAMILY: one Gen_<fn> body, registered per
            # core-type sqlName over each callable arity (the catalog argDefaults give the shorter
            # overloads). Self-contained (own body + regs + continue), so the generic shapes below
            # never also emit these — the bare names are owned here and by the retired hand loop.
            STATE["grp"] = f.get("group") or "meos_ungrouped"
            fn = f["name"]
            bodies.append(emit_temporal_ctor(f, tc))
            # a reader of a retired I/O group (tcbufferFromHexWKB) reads every temporal type its
            # signatures declare, the transforms and constructors the core types
            # and the transforms of a retired group every type theirs declare (tgeometryInst,
            # tpcpatchSeq, tjsonbSeqSet)
            ctor_acc = (SIG_TEMPORAL_ACC if tc in ("from_hexwkb", "from_mfjson", "to_inst",
                                                   "to_seq", "to_seqset") and retired(f)
                        else CORE_CTOR_ACC)
            for s in f.get("sqlSignatures", []):
                # The registered SQL/return type is the RESULT temporal type (the parsers
                # take a text/blob arg, the constructors an operand of the same type).
                # The constructors over an array of a retired group register for every temporal
                # type the signatures declare (tgeometrySeq, tpcpointSeq, tcbufferSeqSet); the
                # transforms and parsers stay on the types ctor_acc names.
                arr = (s.get("args") or [""])[0].endswith("[]")
                acc = (SIG_TEMPORAL_ACC if retired(f) and arr else ctor_acc).get(s["ret"])
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
        cont = CONTAINER_FROM_HEXWKB.get(f["name"]) or container_from_text(f)
        if cont:
            # Set/Span/SpanSet FromHexWKB NAME FAMILY: one Gen_<fn> body, registered per core
            # element-type sqlName over the single VARCHAR (hex) argument. Self-contained
            # (own body + regs + continue) like the temporal constructor branch above.
            STATE["grp"] = f.get("group") or "meos_ungrouped"
            fn = f["name"]
            bodies.append(emit_container_from_hexwkb(f, cont))
            # the names the signatures declare (intsetFromText), where the group is retired
            for s in (f.get("sqlSignatures", []) if retired(f) else []):
                acc = io_acc(cont).get(s["ret"]) if cont in IO_BLOBTO else None
                if not acc:
                    continue    # spatial element (geo/cbuffer/npoint/...) -> its own family file
                nm = s.get("sqlName") or f["sqlfn"]
                # a type I/O name (cbufferset_in over cstring) is PostgreSQL's type support and is
                # not registered, as the WKB family below skips intset_send
                if re.search(r'_(out|in|send|recv)$', nm):
                    continue
                specific_regs.append(
                    f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                    f'"{reg_name(nm, f)}", {{LogicalType::VARCHAR}}, {acc}, Gen_{fn}));')
            # The input function of a type (#type_input) also answers the function form of its
            # cast, T(text), which MobilityDB answers for every type as PostgreSQL does
            # (stbox('STBOX X((1,2),(3,4))') beside 'STBOX X((1,2),(3,4))'::stbox), named by the
            # type it reads
            tin = type_input(f)
            if tin:
                specific_regs.append(
                    f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                    f'"{tin[0]}", {{LogicalType::VARCHAR}}, {tin[1]}, Gen_{fn}));')
            continue
        # the WKB writers and reader of a set, span or span set (asBinary, asHexWKB,
        # intsetFromBinary), registered per declared signature under its own name over the core
        # container types, as the FromHexWKB family above; a type I/O name (intset_send) is
        # PostgreSQL's type support and is not registered
        cw, cr = container_wkb(f), container_from_wkb(f)
        if cw or cr:
            STATE["grp"] = f.get("group") or "meos_ungrouped"
            fn = f["name"]
            bodies.append(emit_container_wkb(f, *cw) if cw else emit_container_from_wkb(f, cr))
            wseen = set()
            for s in f.get("sqlSignatures", []):
                nm = s.get("sqlName") or f["sqlfn"]
                if re.search(r'_(out|in|send|recv)$', nm):
                    continue
                # the spans and span sets this group parses itself (a typed <type>_in beside the
                # writer: intspan_in .. tstzspanset_in), every set, the sets of the other families
                # and of geometries among them (asBinary(cbufferset), geomsetFromBinary), and
                # every temporal value, whose readers and writers the temporal, spatial, JSON and
                # point cloud I/O groups retire together, over each type declared
                cio = cw[1] if cw else cr
                tname = (s.get("args") or [None])[0] if cw else s.get("ret")
                own = FN_BY_NAME.get(f"{tname}_in")
                if cio not in ("Temporal", "Set") and (not own or own.get("group") != f.get("group")):
                    continue
                if cw:
                    acc = io_acc(cio).get((s.get("args") or [None])[0])
                    rty = "LogicalType::BLOB" if cw[0] == "bin" else "LogicalType::VARCHAR"
                    defs = s.get("argDefaults") or []
                    forms = [[acc]] + ([[acc, "LogicalType::VARCHAR"]]
                                       if len(s.get("args") or []) == 2 else [])
                    if len(s.get("args") or []) == 2 and not (defs and defs[-1] is not None):
                        forms = forms[1:]
                else:
                    acc = io_acc(cio).get(s.get("ret"))
                    rty, forms = acc, [["LogicalType::BLOB"]]
                if not acc:
                    continue    # spatial element (geo/cbuffer/npoint/...) -> its own family file
                for args_ in forms:
                    reg = (f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                           f'"{reg_name(nm, f)}", {{{", ".join(args_)}}}, {rty}, Gen_{fn}));')
                    if reg not in wseen:
                        wseen.add(reg)
                        specific_regs.append(reg)
            continue
        # STATIC cell functions (h3index/quadbin/s2cell with no temporal operand): one Gen_<fn>
        # body, registered over every declared signature whose marshalling matches the body's.
        # Self-contained (own body + regs + continue): supported() rejects the by-value cell
        # argument, so none of the shapes below claims these.
        stc = shape_static_cell(f)
        if stc:
            STATE["grp"] = f.get("group") or "meos_ungrouped"
            fn = f["name"]
            _s0, args0, ret0 = stc[0]
            bodies.append(emit_static_cell(f, args0, ret0))
            body_key = ([a[1] for a in args0], ret0[1])
            for s, sargs, sret in stc:
                if ([a[1] for a in sargs], sret[1]) != body_key:
                    continue            # a signature the one body does not marshal
                slots = ", ".join(a[0] for a in sargs)
                for nm in reg_names(f, s.get("sqlName", f["sqlfn"]), aliases, "{%s}" % slots):
                    specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                         f'"{reg_name(nm, f)}", {{{slots}}}, {sret[0]}, Gen_{fn}));')
                # the input function of a cell (quadbin_in) answers the function form of its
                # cast, quadbin(text), as #type_input's types do
                if fn == s.get("ret", "") + "_in" and slots == "LogicalType::VARCHAR":
                    specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                         f'"{s["ret"]}", {{{slots}}}, {sret[0]}, Gen_{fn}));')
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
        # an array of temporal values (merge(tint[])), registered over the element types its
        # signatures declare, as the NxN array shapes below register theirs
        _arr = shape_temporal_array(f)
        if _arr:
            STATE["grp"] = f.get("group") or "meos_ungrouped"
            bodies.append(emit_temporal_array(f)); n_un += 1
            for t_ in _arr:
                a_ = SIG_TEMPORAL_ACC[t_]
                for nm in reg_names(f, f["sqlfn"], aliases):
                    specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                         f'"{reg_name(nm, f)}", {{LogicalType::LIST({a_})}}, {a_}, '
                                         f'Gen_{f["name"]}));')
            continue
        # a set, span or span set built from its values: one registration per declared signature,
        # and per shorter form its trailing SQL defaults allow (#ctor_forms)
        _mk = shape_container_make(f)
        if _mk:
            STATE["grp"] = f.get("group") or "meos_ungrouped"
            kind, el, conv, rb_ = _mk
            info = el if kind == "scalars" else (el, conv)
            emitted, taken = set(), set()
            for s in f.get("sqlSignatures") or []:
                ret_ = ctor_duck_type(s.get("ret") or "")
                argt = [ctor_duck_type(a) for a in s.get("args") or []]
                if ret_ is None or None in argt:
                    continue
                for k, sfx in ctor_forms(f, s, kind):
                    # a signature whose DuckDB arguments an earlier one takes registers nothing:
                    # set(geography[]) reads the GEOMETRY list set(geometry[]) reads
                    nms = [reg_name(nm, f) for nm in reg_names(f, s.get("sqlName") or f["sqlfn"], aliases)]
                    if all((nm.lower(), tuple(argt[:k])) in taken for nm in nms):
                        continue
                    if sfx not in emitted:
                        emitted.add(sfx)
                        bodies.append(emit_container_make(f, kind, info, rb_, k)); n_un += 1
                    for nm in nms:
                        if (nm.lower(), tuple(argt[:k])) in taken:
                            continue
                        taken.add((nm.lower(), tuple(argt[:k])))
                        specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                             f'"{nm}", {{{", ".join(argt[:k])}}}, {ret_}, '
                                             f'Gen_{f["name"]}{sfx}));')
            continue
        # a set-returning function the other engines call by its `altSqlName` (temporalUnnest):
        # a table function over the types its signatures declare, as the path tables are
        _un = shape_unnest(f)
        if _un:
            STATE["grp"] = f.get("group") or "meos_ungrouped"
            un_name, un_accs, un_dt, un_write, un_ctype = _un
            bodies.append(emit_unnest(f, un_name, un_dt, un_write, un_ctype))
            S_ = f"Unnest_{f['name']}"
            for a in un_accs:
                specific_regs.append(f'    loader.RegisterFunction({S_}Table({a}));')
            continue
        # the tiles of a box, one (index, tile) row each (timeTiles, spaceTimeTiles): a table function
        # per declared form, its shorter forms the trailing SQL defaults allow
        _bt = shape_box_tiles(f)
        if _bt:
            STATE["grp"] = f.get("group") or "meos_ungrouped"
            for j, (s_, ins_, bound_) in enumerate(_bt):
                for k_ in box_tile_forms(s_, ins_):
                    tag = f"s{j}_a{k_}"
                    S_ = f"Tiles_{f['name']}_{tag}"
                    types_ = [TILE_BOX_IN[base(ins_[0]["canonical"])][1]] + \
                             [_tile_arg(p)[0] for p in ins_[1:k_]]
                    nms = [reg_name(nm, f) for nm in reg_names(f, s_.get("sqlName") or f["sqlfn"], aliases)
                           if (reg_name(nm, f), tuple(types_)) not in TILE_REGISTERED]
                    if not nms:
                        continue
                    bodies.append(emit_box_tiles(f, tag, s_, ins_, bound_, k_))
                    for nm in nms:
                        TILE_REGISTERED.add((nm, tuple(types_)))
                        specific_regs.append(f'    loader.RegisterFunction(TableFunction("{nm}", '
                                             f'{{{", ".join(types_)}}}, {S_}ExecFn, {S_}BindFn, {S_}InitFn));')
            continue
        # the boxes of a spatial value and the tile of a point (spaceBoxes, getSpaceTile): a scalar
        # per declared form over the first operand types its signatures name
        _bs = shape_box_scalars(f)
        if _bs:
            STATE["grp"] = f.get("group") or "meos_ungrouped"
            first_, groups_, ret_ = _bs
            rt_ = "LogicalType::LIST(StboxType::stbox())" if ret_ == "list" else "StboxType::stbox()"
            for j, (s_, ins_, bound_, firsts_) in enumerate(groups_):
                accs_ = ([SIG_TEMPORAL_ACC[a] for a in firsts_] if first_ == "temporal"
                         else ["MobilityDuckGeometryType()"])
                for k_ in box_tile_forms(s_, ins_):
                    tag = f"s{j}_a{k_}"
                    tail_ = [_tile_arg(p)[0] for p in ins_[1:k_]]
                    sql_ = s_.get("sqlName") or f["sqlfn"]
                    regs_ = []
                    for acc in accs_:
                        types_ = tuple([acc] + tail_)
                        for nm in reg_names(f, sql_, aliases):
                            if (reg_name(nm, f), types_) not in TILE_REGISTERED:
                                regs_.append((reg_name(nm, f), types_))
                    if not regs_:
                        continue
                    bodies.append(emit_box_scalars(f, first_, tag, s_, ins_, bound_, k_, ret_))
                    for nm, types_ in regs_:
                        TILE_REGISTERED.add((nm, types_))
                        specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                             f'"{nm}", {{{", ".join(types_)}}}, {rt_}, Gen_{f["name"]}_{tag}));')
            continue
        # a temporal value or a geometry followed by three or more scalars (rotate, affine),
        # registered over the types its signatures declare, as the restriction groups' are
        _nary = shape_nary_scalars(f)
        if _nary:
            STATE["grp"] = f.get("group") or "meos_ungrouped"
            first, tail, ret = _nary
            bodies.append(emit_nary_scalars(f, first, tail, ret)); n_ter += 1
            rest = "".join(", " + t[0] for t in tail)
            if first == "geo" and ret.startswith("cont:"):
                # one GEOMETRY stands for the geometry and geography signatures alike, as
                # #shape_array reads a geometry first; the container is the one the first
                # declared signature answers (the box both declare, the geomset of set(geometry)
                # where set(geography) answers a geogset), one registration per argument list
                rcb = ret.split(":", 1)[1]
                rs = [CONT_SQL_ACC[rcb].get(s.get("ret")) for s in f.get("sqlSignatures") or []
                      if len(s.get("args") or []) == 1 + len(tail)]
                pairs = [("MobilityDuckGeometryType()", r, [f["sqlfn"]])
                         for r in [x for x in rs if x][:1]]
            elif first in ("geo", "geoarr") and ret in ("geo", "geo:out"):
                pairs = [("MobilityDuckGeometryType()" if first == "geo"
                          else "LogicalType::LIST(MobilityDuckGeometryType())",
                          "MobilityDuckGeometryType()", [f["sqlfn"]])]
            elif first in ("geo", "geoarr"):
                # the signatures over a geometry, under their own names and results: a
                # temporal value typed by the signature (tgeompoint(geometry)), a by-value result
                # as #scalar_ret4 registers it (numPoints(geometry), distance(geometry, geometry));
                # the geography signatures read a geodetic value DuckDB's GEOMETRY does not carry
                a0 = ("MobilityDuckGeometryType()" if first == "geo"
                      else "LogicalType::LIST(MobilityDuckGeometryType())")
                pairs, seen_ = [], set()
                for s in f.get("sqlSignatures") or []:
                    if (s.get("args") or [None])[0] != ("geometry" if first == "geo" else "geometry[]") \
                            or len(s["args"]) != 1 + len(tail):
                        continue
                    r = (scalar_ret4(f)[0] if ret == "scalar" else SIG_TEMPORAL_ACC.get(s.get("ret")))
                    nm0 = s.get("sqlName") or f["sqlfn"]
                    if r and (nm0, r) not in seen_:
                        seen_.add((nm0, r))
                        pairs.append((a0, r, [nm0]))
            elif first.startswith("cont:"):
                # the operand's and the return's container types per declared signature, as
                # #container_scalar_sigs reads one (CONT_SQL_ACC), under the signature's own name
                # (shiftScale(tstzspan, interval, interval) beside the shift and scale its
                # siblings bind; span(intspanset) answering intspan); a trailing SQL default also
                # registers the shorter form through Gen_<fn>_d (tprecision(tstzset, interval))
                acc_ = CONT_SQL_ACC[first.split(":", 1)[1]]
                racc_ = CONT_SQL_ACC[ret.split(":", 1)[1]]
                short = "".join(", " + t[0] for t in tail[:-1])
                dflt, seen = None, set()
                for s in f.get("sqlSignatures") or []:
                    if len(s.get("args") or []) != 1 + len(tail) or s["args"][0] not in acc_ \
                            or s.get("ret") not in racc_:
                        continue
                    a, r = acc_[s["args"][0]], racc_[s["ret"]]
                    nm0 = s.get("sqlName") or f["sqlfn"]
                    defs = s.get("argDefaults") or []
                    forms = [(rest, "")]
                    if defs and defs[-1] is not None and not any(d is not None for d in defs[:-1]):
                        lit = forward_literal(classify(f)[0][-1], defs[-1])
                        if lit is not None:
                            dflt = lit
                            forms.append((short, "_d"))
                    for rr, sfx in forms:
                        for nm in reg_names(f, nm0, aliases, "{x%s}" % rr):
                            reg = (f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                   f'"{reg_name(nm, f)}", {{{a}{rr}}}, {r}, Gen_{f["name"]}{sfx}));')
                            if reg not in seen:
                                seen.add(reg)
                                specific_regs.append(reg)
                if dflt is not None:
                    bodies.append(emit_nary_scalars(f, first, tail, ret, dflt))
                continue
            else:
                _sc = reg_scope(f["name"])
                accs_ = sig_declared_accs(f) or (_sc[1] if _sc and _sc[0] == "types" else None) or []
                pairs = []
                for a in accs_:
                    r = ("MobilityDuckGeometryType()" if ret in ("geo", "geo:out") else
                         "LogicalType::VARCHAR" if ret == "text" else
                         (sig_declared_ret(f, a, 1 + len(tail))
                          or ret_temporal_type(f["name"], a, f.get("group"), f.get("sqlReturnType"))))
                    # under the name its signatures at this arity declare (geoMeasure, where the
                    # function's other signatures are geometry(tgeompoint))
                    at = [s for s in f.get("sqlSignatures") or []
                          if len(s.get("args") or []) == 1 + len(tail)]
                    nms = sorted({s.get("sqlName") or f["sqlfn"] for s in at
                                  if s["args"][0] == _acc_sqlname(a)})
                    if at and not nms:
                        continue    # a type no signature at this arity takes first
                    pairs.append((a, r, nms or [f["sqlfn"]]))
            # a trailing SQL default also registers the shorter form through Gen_<fn>_d, as the
            # container arm above does (extendedKalmanFilter(tfloat, float, float, float)); the
            # defaults before it are the shorter records #project_signature_forms forwards
            # (asMFJSON(tfloat, integer, integer) here, asMFJSON(tfloat, integer) there)
            forms, dflt = [(rest, "")], None
            for s in f.get("sqlSignatures") or []:
                defs = s.get("argDefaults") or []
                if len(s.get("args") or []) == 1 + len(tail) and defs and defs[-1] is not None:
                    dflt = forward_literal(classify(f)[0][-1], defs[-1])
                    break
            if dflt is not None:
                forms.append(("".join(", " + t[0] for t in tail[:-1]), "_d"))
                bodies.append(emit_nary_scalars(f, first, tail, ret, dflt))
            for a, r, nms in pairs:
                for rr, sfx in forms:
                    for nm in [x for nm0 in nms for x in reg_names(f, nm0, aliases, "{x%s}" % rr)]:
                        specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                             f'"{reg_name(nm, f)}", {{{a}{rr}}}, {r}, Gen_{f["name"]}{sfx}));')
            continue
        # a set, span or span set handing its value back through an out-parameter
        # (valueN(intset, integer)), registered over the container type each signature declares
        _cop = shape_cont_outparam(f)
        if _cop:
            STATE["grp"] = f.get("group") or "meos_ungrouped"
            ctail, cdt, ccloc, ccpp, cout = _cop
            bodies.append(emit_cont_outparam(f, ctail, ccloc, ccpp, cout)); n_bin += 1
            cacc = CONT_SQL_ACC[base(classify(f)[0][0]["canonical"])]
            crest = "".join(", " + t[0] for t in ctail)
            cseen = set()
            for s in f.get("sqlSignatures") or []:
                if len(s.get("args") or []) != 1 + len(ctail) or s["args"][0] not in cacc:
                    continue
                for nm in reg_names(f, s.get("sqlName") or f["sqlfn"], aliases, "{x%s}" % crest):
                    reg = (f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                           f'"{reg_name(nm, f)}", {{{cacc[s["args"][0]]}{crest}}}, {cdt}, Gen_{f["name"]}));')
                    if reg not in cseen:
                        cseen.add(reg)
                        specific_regs.append(reg)
            continue
        # two geometries handing back a by-value result (bearing(geometry, geometry))
        _gpo = shape_geo_pair_out(f)
        if _gpo:
            STATE["grp"] = f.get("group") or "meos_ungrouped"
            bodies.append(emit_geo_pair_out(f, _gpo[1])); n_bin += 1
            for nm in reg_names(f, f["sqlfn"], aliases):
                specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                     f'"{reg_name(nm, f)}", {{MobilityDuckGeometryType(), '
                                     f'MobilityDuckGeometryType()}}, {_gpo[0]}, Gen_{f["name"]}));')
            continue
        u = shape_emittable(f); b = None if u else shape_binary(f); t = None if (u or b) else shape_ternary(f)
        tt = None if (u or b or t) else shape_binary_tt(f)
        tts = None if (u or b or t or tt) else shape_binary_tt_scalar(f)
        sf = None if (u or b or t or tt or tts) else shape_scalar_first(f)
        gt = None if (u or b or t or tt or tts or sf) else shape_geo_temporal(f)
        ga = None if (u or b or t or tt or tts or sf or gt) else shape_tgeoarr(f)
        pp = None if (u or b or t or tt or tts or sf or gt or ga) else shape_path(f)
        op = None if (u or b or t or tt or tts or sf or gt or ga or pp) else shape_outparam(f)
        bt = None if (u or b or t or tt or tts or sf or gt or ga or pp or op) else shape_bound_tail(f)
        ta = None if (u or b or t or tt or tts or sf or gt or ga or pp or bt) else shape_text_array(f)
        if not u and not b and not t and not tt and not tts and not sf and not gt and not ga and not pp and not op and not bt and not ta:
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
        # geodetic/cross-type overloads MEOS never declares. The restrictions read the same SoT: tgeo_at_geom
        # and tpoint_at_geom both carry atGeometry, and only their declared types keep one of them per
        # signature; atStbox is declared over neither tgeography nor the other geodetic forms.
        # The spatial accessors read it too: tgeo_traversed_area is named for every geo type and
        # declared over tgeometry and tgeography alone.
        if re.search(r'_(rel_(ever|temp)|restrict)$', f.get("group") or "") or \
                (f.get("group") or "") == "meos_geo_accessor":
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
        elif op:
            _k, dret, okind, vary, tail, sig, ocanon = op; n_un += 1
            bodies.append(emit_body_outparam(f, dret, okind, vary, tail, sig, ocanon))
            _rest = "".join(", %s" % x for x in sig)
            argsig = "{type%s}" % _rest
            spec_sig = "{%%s%s}" % _rest
        elif bt:
            kind, dret, vary, tail, sig = bt; n_ter += 1
            bodies.append(emit_body_bound_tail(f, kind, vary, tail, sig))
            _rest = "".join(", %s" % x for x in sig[1:])
            argsig = "{type, %s%s}" % (sig[0], _rest)
            spec_sig = "{%%s, %s%s}" % (sig[0], _rest)
        elif ta:
            kind, dret, tail, sig = ta; n_ter += 1
            # `scope, accs` above reads reg_scope, which resolves TEMPORAL scope and answers
            # None for every set name; the set operand takes the set map the shape already
            # cleared it on, the way the set emit paths below read set_reg_scope.
            if kind == "set":
                scope, accs = set_reg_scope(fn)
            bodies.append(emit_body_text_array(f, kind, tail, sig))
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
        # A retired group's temporal pair registers exactly the pairs MobilityDB declares, the
        # commuted one through `Gen_<fn>_rev`: one scope type for both operands would put
        # tdistance_trgeometry_tpoint over (trgeometry, trgeometry), the pair of
        # tdistance_trgeometry_trgeometry
        _ttpairs = sig_tt_pairs(f) if (tt and retired(f)) else None
        if _ttpairs and any(p[0] != p[1] or p[3] for p in _ttpairs):
            if any(p[3] for p in _ttpairs):
                bodies.append(emit_binary_tt(f, kind, rev=True))
            for a0, a1, ra, rv, snm in _ttpairs:
                _r = (ra or ret_temporal_type(fn, a0, f.get("group"), f.get("sqlReturnType"))
                      if dret == "MD_TEMPORAL" else dret)
                # the operator is the function's: a signature under another name
                # (tjsonbContained, contains_tjsonb_tjsonb over its operands swapped) takes
                # that name alone, never the operator of the name it computes
                for nm in (reg_names(f, snm, aliases, argsig) if snm == sqlfn else [snm]):
                    specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                         f'"{reg_name(nm, f)}", {{{a0}, {a1}}}, {_r}, '
                                         f'Gen_{fn}{"_rev" if rv else ""}));')
            continue
        names = reg_names(f, sqlfn, aliases, argsig)
        if scope == "all":
            rett = (ret_temporal_type(fn, "type", f.get("group"), f.get("sqlReturnType")) if dret == "MD_TEMPORAL"
                    else ret_set_type(fn, "type") if dret == "MD_SET" else dret)
            for nm in names:
                generic_regs.append(f'        RegisterSerializedScalarFunction(loader, ScalarFunction('
                                    f'"{reg_name(nm, f)}", {argsig}, {rett}, Gen_{fn}));')
        else:
            for a in accs:
                # a retired binary kernel declared over this type in the commuted order alone
                # registers that order below (Gen_<fn>_rev), never its own
                if b and retired(f) and sig_only_commuted(f, a):
                    continue
                sig = spec_sig % ((a,) * spec_sig.count("%s"))   # 1 or 2 accessor slots
                r2 = (ret_temporal_type(fn, a, f.get("group"), f.get("sqlReturnType")) if dret == "MD_TEMPORAL"
                      else ret_set_type(fn, a) if dret == "MD_SET" else dret)
                if dret == "MD_TEMPORAL":
                    r2 = sig_declared_ret(f, a, sig.count(",") + 1) or r2
                # a retired group's signature of the same arguments under another name
                # (rotate(tgeompoint, float) beside rotateZ(tgeompoint, float), both over
                # tgeo_rotate_z) registers under that name too
                more = sorted({s["sqlName"] for s in f.get("sqlSignatures") or []
                               if retired(f) and s.get("sqlName") and s["sqlName"] != sqlfn
                               and (s.get("args") or [None])[0] == _acc_sqlname(a)
                               and len(s["args"]) == sig.count(",") + 1}) if not u else []
                for nm in names + [x for m in more for x in reg_names(f, m, aliases, argsig)
                                   if x not in names]:
                    specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                         f'"{reg_name(nm, f)}", {sig}, {r2}, Gen_{fn}));')
        # THE COMMUTED OVERLOAD. Six geo backings are CREATE FUNCTION'd in BOTH operand orders
        # over one MEOS kernel — `tDistance(trgeometry, geometry)` beside
        # `tDistance(geometry, trgeometry)` — because MobilityDB gives each order its own PG
        # wrapper and the commuted one swaps the arguments into the same call
        # (`Tdistance_geo_trgeometry` -> `tdistance_trgeometry_geo`). `sig_geo_first` answers None
        # there, so the block above registers the kernel's order alone and the declared commuted
        # signature reaches no surface. Emit it as its own body: one `Gen_<fn>_rev` beside
        # `Gen_<fn>`, over exactly the types THAT order declares (`sig_geo_accs`), never the union.
        # ⛔ A SECOND BODY, NOT A SECOND REGISTRATION OF THE FIRST: the two differ in which vector
        # carries which operand, and re-emitting one `Gen_` name is a C++ redefinition the load
        # refuses.
        if gt:
            _commuted = not geo_first
            _caccs = sig_geo_accs(f, _commuted)
            if _caccs and sig_geo_first(f) is None:
                bodies.append(emit_geo_temporal(f, kind, _commuted, has_dbl, suffix="_rev"))
                _cslots = ["MobilityDuckGeometryType()", "%s"] if _commuted \
                    else ["%s", "MobilityDuckGeometryType()"]
                if has_dbl:
                    _cslots.append("LogicalType::DOUBLE")
                _cspec = "{" + ", ".join(_cslots) + "}"
                _cnames = reg_names(f, sqlfn, aliases, _cspec.replace("%s", "type"))
                for a in _caccs:
                    _csig = _cspec % ((a,) * _cspec.count("%s"))
                    _cr = (ret_temporal_type(fn, a, f.get("group"), f.get("sqlReturnType"))
                           if dret == "MD_TEMPORAL" else dret)
                    if dret == "MD_TEMPORAL":
                        _cr = sig_declared_ret(f, a, _csig.count(",") + 1) or _cr
                    for nm in _cnames:
                        specific_regs.append(
                            f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                            f'"{reg_name(nm, f)}", {_csig}, {_cr}, Gen_{fn}_rev));')
        # A retired group's binary kernel declared in both orders (nearestApproachDistance over
        # (tcbuffer, cbuffer) and (cbuffer, tcbuffer), one MEOS function) registers the commuted
        # order through `Gen_<fn>_rev`, as the box and the geometry pairs do (#sig_commuted_accs)
        if b and retired(f) and not kind.startswith("outval:") and kind != "outgeo":
            _raccs = sig_commuted_accs(f)
            if _raccs:
                bodies.append(emit_body_binary_rev(f, kind, arg2))
                for a in _raccs:
                    _r = ((sig_declared_ret(f, a, 2)
                           or ret_temporal_type(fn, a, f.get("group"), f.get("sqlReturnType")))
                          if dret == "MD_TEMPORAL" else dret)
                    for nm in reg_names(f, sqlfn, aliases, argsig):
                        specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                             f'"{reg_name(nm, f)}", {{{arg2[0]}, {a}}}, {_r}, Gen_{fn}_rev));')
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
                    r2 = sig_declared_ret(f, a, 1) or r2
                    for nm in names:
                        specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                             f'"{reg_name(nm, f)}", {{{a}}}, {r2}, Gen_{fn}_d));')
        # A (temporal, scalar, scalar-param DEFAULT)->X fn (tquadbin(tgeompoint, integer[,
        # borderInc DEFAULT TRUE])) is callable one argument shorter too: the same overload one
        # position up, the binary body with the trailing default substituted.
        if t and trailing_arg_default(f):
            # The default reaches the kernel the way the argument it stands for does: a by-value
            # scalar as its C++ literal, a text argument through the marshalling its column takes
            # (null_handle_type_from_string of 'raise_exception'), the literal as its string_t, a
            # timestamptz or interval literal through the MEOS parser of its type, which reads it
            # as the SQL default is read (a timestamptz in the session's zone).
            _raw = trailing_arg_default(f)
            _a3 = t[3]
            if _a3[1] == "string_t":
                _lit = '"%s"' % _raw.strip().strip("'")
                dflt = _a3[2].replace("a2", "string_t(%s)" % _lit)
            elif _a3[1] == "timestamp_tz_t":
                dflt = 'timestamptz_in("%s", -1)' % _raw.strip().strip("'")
            elif _a3[1] == "interval_t":
                dflt = 'MeosIntervalLiteral("%s")' % _raw.strip().strip("'")
            else:
                dflt = sql_default_to_cpp(_raw)
            bodies.append(emit_defaulted_ternary(f, kind, arg2, dflt))
            if scope == "all":
                rett = (ret_temporal_type(fn, "type", f.get("group"), f.get("sqlReturnType"))
                        if dret == "MD_TEMPORAL" else dret)
                for nm in names:
                    generic_regs.append(f'        RegisterSerializedScalarFunction(loader, ScalarFunction('
                                        f'"{reg_name(nm, f)}", {{type, {arg2[0]}}}, {rett}, Gen_{fn}_d));')
            else:
                for a in accs:
                    r2 = dret
                    if dret == "MD_TEMPORAL":
                        r2 = (sig_declared_ret(f, a, 2)
                              or ret_temporal_type(fn, a, f.get("group"), f.get("sqlReturnType")))
                    for nm in names:
                        specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                             f'"{reg_name(nm, f)}", {{{a}, {arg2[0]}}}, {r2}, Gen_{fn}_d));')
        # The shorter call of a retired group's temporal pair with a defaulted third argument
        # (insert(tint, tint) for insert(tint, tint, connect boolean DEFAULT TRUE)), registered
        # over the same pairs as the full call, as the ternary's above
        if tts and retired(f) and trailing_arg_default(f) and arg3[1] not in ("string_t",):
            dflt = sql_default_to_cpp(trailing_arg_default(f))
            bodies.append(emit_binary_tt(f, kind, dflt=dflt))
            for a in (accs if scope != "all" else []):
                r2 = dret
                if dret == "MD_TEMPORAL":
                    r2 = (sig_declared_ret(f, a, 2)
                          or ret_temporal_type(fn, a, f.get("group"), f.get("sqlReturnType")))
                for nm in names:
                    specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                         f'"{reg_name(nm, f)}", {{{a}, {a}}}, {r2}, Gen_{fn}_d));')
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
        # the same shorter overload of a geometry-returning one (traversedArea(tgeometry))
        if b and kind == "geo" and trailing_arg_default(f) and scope != "all":
            dflt = sql_default_to_cpp(trailing_arg_default(f))
            bodies.append(emit_defaulted_unary_geo(f, dflt))
            for a in accs:
                for nm in names:
                    specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                         f'"{reg_name(nm, f)}", {{{a}}}, {dret}, Gen_{fn}_d));')
    # CONTAINER SCALARS — one registration per declared signature, over the body whose operand
    # order it spells (shape_container_scalar).
    for f in fns:
        if declared is not None and f["name"] not in declared:
            continue
        ops = shape_container_scalar(f)
        if ops is None:
            continue
        sigs = container_scalar_sigs(f, ops)
        if not sigs:
            continue
        STATE["grp"] = f.get("group") or "meos_ungrouped"
        n_set += 1
        fn = f["name"]
        dret = scalar_ret4(f)[0]
        for rev in sorted({r for _a, r in sigs}):
            set_bodies.append(emit_container_scalar(f, ops, rev))
        names = reg_names(f, f["sqlfn"], aliases)
        for (a0, a1), rev in sigs:
            body = f"Gen_{fn}_rev" if rev else f"Gen_{fn}"
            for nm in names:
                set_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                         f'"{reg_name(nm, f)}", {{{a0}, {a1}}}, {dret}, {body}));')
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
        # (Set, scalar element)->Set whose declared signatures name only sets this surface does not
        # register: no body, no registration, as for the scalar-param case above.
        ep = (declared_element_pairs(f, kind.startswith("setsc_set:"))
              if sp is None and kind.startswith(("setsc_set:", "scset_set:")) else None)
        if ep is not None and not ep:
            continue
        n_set += 1
        set_bodies.append(emit_set(f, kind))
        fn, sqlfn = f["name"], f["sqlfn"]
        # portable bare-name alias; normalize the doxygen `@`-escape (sqlop "\@>" -> "@>").
        names = reg_names(f, sqlfn, aliases)
        # element-typed predicates: accessor from the scalar element type, BOOLEAN ret.
        if kind.startswith("setsc_set:") or kind.startswith("scset_set:"):
            # (Set, scalar element) -> Set, and its reversed (scalar element, Set) order. The
            # registered signature follows the C parameter order so it matches the emitted body.
            b = kind.split(':')[1]
            scd = "LogicalType::VARCHAR" if b == "text" else SCALAR_ARG[b][0]
            setfirst = kind.startswith("setsc_set:")
            # scalar-param: register over the catalog-declared core set types (round->floatset);
            # element-add: register over the set each declared signature names
            # (setUnion(intset, integer)->intset, jsonbsetDelete(jsonbset, text)->jsonbset); a
            # function the catalog declares no signature for takes the element's set type.
            pairs = sp if sp is not None else (ep or [(ELEM_TO_SET[b], ELEM_TO_SET[b], None)])
            dflt = next((d for *_, d in pairs if d is not None), None)
            for acc, rett, _d in pairs:
                sig = f"{{{acc}, {scd}}}" if setfirst else f"{{{scd}, {acc}}}"
                for nm in names:
                    set_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                             f'"{reg_name(nm, f)}", {sig}, {rett}, Gen_{fn}));')
            # A SQL-optional trailing param (round's precision DEFAULT 0) is callable at the
            # shorter arity; emit the (Set)->Set overload with the catalog default substituted.
            if dflt is not None:
                set_bodies.append(emit_defaulted_unary(fn, "BlobToSet", "SetToBlob", "Set", sql_default_to_cpp(dflt)))
                for acc, rett, _d in pairs:
                    for nm in names:
                        set_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                                 f'"{reg_name(nm, f)}", {{{acc}}}, {rett}, Gen_{fn}_d));')
            continue
        if kind.startswith("u2text_d:"):   # (Set)->text alone: asText(jsonbset), its digit count bound
            if set_reg_scope(fn) is None:   # scoped by its declared signatures
                for a, _two, one in declared_text_sigs(f):
                    if one:
                        for nm in names:
                            set_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                                     f'"{reg_name(nm, f)}", {{{a}}}, {dret}, Gen_{fn}_d));')
                continue
            scope, accs = set_reg_scope(fn)
            acc_list = ["type"] if scope == "all" else accs
            sink = set_generic_regs if scope == "all" else set_specific_regs
            for a in acc_list:
                for nm in names:
                    sink.append(f'        RegisterSerializedScalarFunction(loader, ScalarFunction('
                                f'"{reg_name(nm, f)}", {{{a}}}, {dret}, Gen_{fn}_d));')
            continue
        if kind.startswith("u2text:"):   # (Set, scalar PARAM)->text: asText(floatset, integer)
            scd = SCALAR_ARG[kind.split(':', 1)[1]][0]
            _, dflt = text_arities(f)
            if dflt is not None:
                set_bodies.append(emit_set(f, "u2text_d:" + str(sql_default_to_cpp(dflt))))
            if set_reg_scope(fn) is None:   # scoped by its declared signatures, each at its arity
                for a, two, one in declared_text_sigs(f):
                    for nm in names:
                        if two:
                            set_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                                     f'"{reg_name(nm, f)}", {{{a}, {scd}}}, {dret}, Gen_{fn}));')
                        if one:
                            set_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                                     f'"{reg_name(nm, f)}", {{{a}}}, {dret}, Gen_{fn}_d));')
                continue
            scope, accs = set_reg_scope(fn)
            acc_list = ["type"] if scope == "all" else accs
            sink = set_generic_regs if scope == "all" else set_specific_regs
            for a in acc_list:
                for nm in names:
                    sink.append(f'        RegisterSerializedScalarFunction(loader, ScalarFunction('
                                f'"{reg_name(nm, f)}", {{{a}, {scd}}}, {dret}, Gen_{fn}));')
                    if dflt is not None:
                        sink.append(f'        RegisterSerializedScalarFunction(loader, ScalarFunction('
                                    f'"{reg_name(nm, f)}", {{{a}}}, {dret}, Gen_{fn}_d));')
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
            # the geometry and geography sets, which SetTypes::AllTypes() does not hold, over the
            # signatures declared for them (memSize(geomset), setUnion(geogset, geogset))
            for s in f.get("sqlSignatures") or []:
                ga = [GEO_SET_TYPES.get(a) for a in s["args"]]
                if len(ga) != nargs or None in ga:
                    continue
                r2 = GEO_SET_TYPES.get(s.get("ret")) if dret == "LogicalType::BLOB" else dret
                if r2 is None:
                    continue
                for nm in names:
                    set_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                             f'"{reg_name(nm, f)}", {{{", ".join(ga)}}}, {r2}, Gen_{fn}));')
        else:
            for a in accs:
                sig = spec_sig % ((a,) * spec_sig.count("%s"))
                r2 = ret_set_type(fn, a) if dret == "LogicalType::BLOB" else dret
                for nm in names:
                    set_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                             f'"{reg_name(nm, f)}", {sig}, {r2}, Gen_{fn}));')
    # COLLECTION families (Span + SpanSet) — ONE machinery via descriptor (shape_span/emit_span
    # take C). Per-container generic list (its own AllTypes loop); specific list shared.
    # A base VALUE container reaches functions an earlier pass already emits — `pose_hash` is
    # emitted by the unary pass, while `stbox_hash` reaches the surface only through the bare-hash
    # carve-out below. Whether a container owns a function is therefore not a property of the
    # container: it is whether anything emitted it yet. Read that from the bodies already written
    # rather than restating it as a per-descriptor flag, so a future container inherits the rule.
    _already = {m for acc in (bodies, set_bodies, span_bodies)
                for _g, _line in acc.items
                for m in re.findall(r'\bstatic void Gen_([A-Za-z0-9_]+)\(', _line)}
    for C in [SPAN_C, SPANSET_C, STBOX_C, TBOX_C, TPCBOX_C] + BASEVAL_C:
        gen = {"Span": span_generic_regs, "SpanSet": spanset_generic_regs}.get(C["cbase"])
        for f in fns:
            if declared is not None and f["name"] not in declared:
                continue
            if f["name"] in _already:
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
            # set operation between two collections answering a third: one registration per
            # declared signature, its operands and result typed by the span and span set maps
            if kind.startswith("b_cont:"):
                _accmap = {**SPAN_TYPES, **SPANSET_TYPES}
                _seen = set()
                for s in (f.get("sqlSignatures") or []):
                    a0, a1 = _accmap.get(s["args"][0]), _accmap.get(s["args"][1])
                    racc = _accmap.get(s.get("ret"))
                    if not (a0 and a1 and racc):
                        continue
                    for nm in reg_names(f, s.get("sqlName") or sqlfn, aliases):
                        reg = (f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                               f'"{reg_name(nm, f)}", {{{a0}, {a1}}}, {racc}, Gen_{fn}));')
                        if reg not in _seen:
                            _seen.add(reg)
                            span_specific_regs.append(reg)
                continue
            # container-and-value set operation: the operand is the container this element names,
            # and the result is the container the RETURN type names for that same element.
            if kind.startswith("cval:") or kind.startswith("valc:"):
                _k, eb, rbase = kind.split(':', 2)
                acc = C["elem"][eb]; scd = SCALAR_ARG[eb][0]
                racc = RET_CONT_ACC[rbase].get(eb)
                if racc is None:
                    continue
                sig = "{%s, %s}" % (acc, scd) if _k == "cval" else "{%s, %s}" % (scd, acc)
                for nm in names:
                    span_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                              f'"{reg_name(nm, f)}", {sig}, {racc}, Gen_{fn}));')
                continue
            # text rendering alone at one argument: the digit count bound to the literal the
            # declared signature passes, as the set loop registers asText(jsonbset).
            if kind.startswith("u2text_d:"):
                acc = C.get("single") or (C["scope"](fn) or (None, []))[1]
                accs_here = [acc] if isinstance(acc, str) else list(acc)
                for a in accs_here:
                    for nm in names:
                        span_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                                  f'"{reg_name(nm, f)}", {{{a}}}, {dret}, Gen_{fn}_d));')
                continue
            # text rendering: the (X, int) form the MEOS entry declares, plus the shorter arity
            # the canonical SQL reaches through that argument's DEFAULT.
            if kind.startswith("u2text:"):
                acc = C.get("single") or (C["scope"](fn) or (None, []))[1]
                accs_here = [acc] if isinstance(acc, str) else list(acc)
                scd = SCALAR_ARG[kind.split(':', 1)[1]][0]
                for a in accs_here:
                    for nm in names:
                        span_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                                  f'"{reg_name(nm, f)}", {{{a}, {scd}}}, {dret}, Gen_{fn}));')
                _, dflt = text_arities(f)
                if dflt is not None:
                    span_bodies.append(emit_span(f, "u2text_d:" + str(sql_default_to_cpp(dflt)), C))
                    for a in accs_here:
                        for nm in names:
                            span_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                                      f'"{reg_name(nm, f)}", {{{a}}}, {dret}, Gen_{fn}_d));')
                continue
            # unary (X)->X|scalar: name-scoped (X_*→AllTypes, <elem>X_*→accessor).
            if kind in ("u_span", "u_geo") or kind.startswith("u_scalar:"):
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
                # the shorter form the SQL default of the trailing argument allows
                # (duration(tstzspanset) beside duration(tstzspanset, boolean)), as u2sc below
                dflt = trailing_arg_default(f)
                if dflt is not None:
                    span_bodies.append(emit_span(f, "u2iv_d:%s:%s" % (
                        kind.split(':', 1)[1], sql_default_to_cpp(dflt)), C))
                if scope == "all":
                    for nm in names:
                        gen.append(f'        RegisterSerializedScalarFunction(loader, ScalarFunction('
                                   f'"{reg_name(nm, f)}", {{type, {argdt}}}, {dret}, Gen_{fn}));')
                        if dflt is not None:
                            gen.append(f'        RegisterSerializedScalarFunction(loader, ScalarFunction('
                                       f'"{reg_name(nm, f)}", {{type}}, {dret}, Gen_{fn}_d));')
                else:
                    for a in accs:
                        for nm in names:
                            span_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                                      f'"{reg_name(nm, f)}", {{{a}, {argdt}}}, {dret}, Gen_{fn}));')
                            if dflt is not None:
                                span_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                                          f'"{reg_name(nm, f)}", {{{a}}}, {dret}, Gen_{fn}_d));')
                continue
            # (X, by-value scalar)->by-value scalar: name-scoped as u2iv, 2-arg sig {acc, argtype},
            # plus the shorter {acc} through Gen_<fn>_d when the trailing argument has a SQL
            # default (area(stbox) beside area(stbox, boolean)), as the csc arm registers round
            if kind.startswith("u2sc:"):
                argdt = SCALAR_ARG[kind.split(':')[1]][0]
                scope, accs = C["scope"](fn)
                dflt = trailing_arg_default(f)
                if dflt is not None:
                    span_bodies.append(emit_span(f, "u2sc_d:%s:%s" % (
                        kind.split(':', 1)[1], sql_default_to_cpp(dflt)), C))
                for a in (["type"] if scope == "all" else accs):
                    sink = gen if scope == "all" else span_specific_regs
                    for nm in names:
                        sink.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                    f'"{reg_name(nm, f)}", {{{a}, {argdt}}}, {dret}, Gen_{fn}));')
                        if dflt is not None:
                            sink.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                        f'"{reg_name(nm, f)}", {{{a}}}, {dret}, Gen_{fn}_d));')
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
                # a typed kernel serving the declared signatures of a sibling type registers over
                # each type they name (intspan_expand answers expand(datespan, integer) too)
                if scope != "all":
                    _cmap = {**SPAN_TYPES, **SPANSET_TYPES}
                    _decl = [_cmap.get(s["args"][0]) for s in f.get("sqlSignatures") or []
                             if len(s["args"]) == 2 and s.get("ret") == s["args"][0]]
                    if _decl and all(_decl):
                        accs = list(dict.fromkeys(_decl))
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
        dret = box_ret3(f, ret_key)[0]
        for a in accs:
            sig = "{%s, %s}" % (a, box_acc) if kind.startswith("tb_r:") else "{%s, %s}" % (box_acc, a)
            for nm in names:
                temporal_box_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                         f'"{reg_name(nm, f)}", {sig}, {dret}, Gen_{fn}));')
        # A retired group's kernel declared in both orders (nearestApproachDistance over
        # (trgeometry, stbox) and (stbox, trgeometry), one MEOS function) registers the commuted
        # order through `Gen_<fn>_rev`, as the geometry pair does
        _raccs = sig_commuted_accs(f) if retired(f) else []
        if _raccs:
            temporal_box_bodies.append(emit_temporal_box(f, kind, rev=True))
            for a in _raccs:
                sig = "{%s, %s}" % (box_acc, a) if kind.startswith("tb_r:") else "{%s, %s}" % (a, box_acc)
                for nm in names:
                    temporal_box_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                             f'"{reg_name(nm, f)}", {sig}, {dret}, Gen_{fn}_rev));')
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
        fn, sqlfn = f["name"], f["sqlfn"]; side, cont, retk = kind.split(":")
        span_first = (side == "ts_l")
        names = reg_names(f, sqlfn, aliases)
        # THE OPERAND PAIRING IS THE CATALOG'S. Each `sqlSignatures` entry IS one CREATE
        # FUNCTION overload, so its two argument types name the temporal type and the
        # container that pair, for exactly the overloads the extension declares.
        # Reading that pairing off the function NAME answers a different question and gets
        # three things wrong at once. It cannot see which temporal types an overload set
        # covers, so the time arm took a BLANKET list that omits tjsonb, tpcpatch and
        # tpcpoint, while the value arm took a two-entry table that omits tbigint, having
        # no bigintspan row at all. And it cannot see that a family owning its own time
        # restriction — trgeometry does, its varlena appending the reference geometry the
        # generic walker would drop — must register over its own type alone, so that had
        # to be re-derived from a type scope beside it. The catalog states all three where
        # the SQL is declared, which is the same move that retired the class-prefix list
        # from the array-return shape.
        # The overload states its SQL name too. One kernel backs overloads of several names:
        # `before_temporal_tstzspan` is `tboxBefore` over the temporal numbers, `stboxBefore`
        # over the spatiotemporal types, `tpcboxBefore` over the point clouds and `spanBefore`
        # over the rest, the class of the bounding box each temporal type carries. The
        # kernel's own sqlfn names one of them, so each overload registers under its own.
        pairs = []
        for s in f.get("sqlSignatures") or ():
            args = s.get("args") or ()
            if len(args) != 2:
                continue
            tname, cname = (args[1], args[0]) if span_first else (args[0], args[1])
            tacc, cacc = SIG_TEMPORAL_ACC.get(tname), FINITE_SUBSET_ACC.get(cname)
            if tacc and cacc:
                pairs.append((cacc, tacc, s.get("sqlName") or sqlfn))
        for spacc, tacc, signame in pairs:
            sig = "{%s, %s}" % (tacc, spacc) if not span_first else "{%s, %s}" % (spacc, tacc)
            rett = tacc if retk == "T" else "LogicalType::BOOLEAN"   # at/minus span preserves type
            for nm in (names if signame == sqlfn else reg_names(f, signame, aliases)):
                temporal_box_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                         f'"{reg_name(nm, f)}", {sig}, {rett}, Gen_{fn}));')
    # Temporal -> container conversion (timeSpan/valueSpan/tbox), sqlSignatures-driven — the
    # per-overload (temporal arg type -> container ret type) comes straight from the catalog
    # rather than from a name. Gated on retired(f): emit+register only for a group being retired
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
        for aacc, racc, signame in pairs:
            for nm in (names if signame == sqlfn else reg_names(f, signame, aliases)):
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
           '#include "geo/geoset.hpp"\n'              # SpatialSetType::geomset()/geogset()
           '#include "pointcloud/tpcbox.hpp"\n'
           '#include "geo/tgeompoint.hpp"\n'
           '#include "geo/tgeogpoint.hpp"\n'
           '#include "geo/tgeometry.hpp"\n'
           '#include "geo/tgeography.hpp"\n'
           '#include "cbuffer/tcbuffer.hpp"\n'         # CbufferTypes::cbuffer()/tcbuffer()
           '#include "h3/th3index.hpp"\n'              # H3indexTypes::h3index()/th3index()
           '#include "quadbin/tquadbin.hpp"\n'         # QuadbinTypes::quadbin()/tquadbin()
           '#include "s2cell/ts2cell.hpp"\n'           # S2cellTypes::s2cell()/ts2cell()
           '#include "json/tjsonb.hpp"\n'              # TJsonbTypes::jsonb()/tjsonb()
           '#include "pointcloud/tpcpoint.hpp"\n'      # TPcpointTypes::pcpoint()/tpcpoint()
           '#include "pointcloud/tpcpatch.hpp"\n'      # TPcpatchTypes::pcpatch()/tpcpatch()
           '#include "npoint/tnpoint.hpp"\n'           # NpointTypes::npoint()/nsegment()/tnpoint()
           '#include "pose/tpose.hpp"\n'               # PoseTypes::pose()/tpose()
           '#include "rgeo/trgeometry.hpp"\n'          # TrgeometryTypes::trgeometry()
           '#include "posechain/tposechain.hpp"\n'     # PosechainTypes::posechain()/tposechain()
           '#include "raster/raquet.hpp"\n'           # RaquetTypes::raquet()
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
           '#include <cfloat>\n#include <cstring>\n#include <cstdlib>\n#include <limits>\n\n'
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
           "// JsonPath is a varlena, so the blob carries the value whole.\n"
           "inline JsonPath *BlobToJsonpath(string_t blob) {\n"
           "    size_t sz = blob.GetSize();\n"
           "    uint8_t *copy = (uint8_t *)malloc(sz);\n"
           "    memcpy(copy, blob.GetData(), sz);\n"
           "    return reinterpret_cast<JsonPath *>(copy);\n}\n"
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
           "// Self-contained blob<->Raquet marshalling. The Raquet structure is opaque, so the\n"
           "// stored BLOB is the WKB serialisation of the tile, the wire form its text hex-encodes.\n"
           "inline string_t RaquetToBlob(Vector &result, Raquet *rq) {\n"
           "    size_t sz = 0;\n"
           "    uint8_t *wkb = raquet_as_wkb(rq, 0, &sz);\n"
           "    free(rq);\n"
           "    if (!wkb) throw InvalidInputException(\"raquet: cannot serialise the tile\");\n"
           "    string_t out = StringVector::AddStringOrBlob(result, (const char *)wkb, sz);\n"
           "    free(wkb);\n    return out;\n}\n"
           "inline string_t RaquetToBlobN(Vector &result, Raquet *rq, ValidityMask &mask, idx_t idx) {\n"
           "    if (!rq) { mask.SetInvalid(idx); return string_t(); }\n    return RaquetToBlob(result, rq);\n}\n"
           "inline Raquet *BlobToRaquet(string_t blob) {\n"
           "    Raquet *rq = raquet_from_wkb((const uint8_t *)blob.GetData(), blob.GetSize());\n"
           "    if (!rq) throw InvalidInputException(\"raquet: invalid tile serialisation\");\n"
           "    return rq;\n}\n"
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
           "// A geography from a DuckDB GEOMETRY, read as geodetic in the default SRID as MobilityDB\n"
           "// reads a geography literal; caller frees.\n"
           "inline GSERIALIZED *GeogFromGeometry(string_t blob) {\n"
           "    GSERIALIZED *gs = GeometryToGSerialized(blob, 0);\n"
           "    GSERIALIZED *geog = geom_to_geog(gs);\n"
           "    free(gs);\n    return geog;\n}\n"
           "// text** from a DuckDB LIST(VARCHAR); the caller frees via FreeTextArr.\n"
           "inline text **ListToTextArr(Vector &child, list_entry_t le, int *count) {\n"
           "    auto data = FlatVector::GetData<string_t>(child);\n"
           "    int n = (int) le.length;\n"
           "    text **arr = (text **) malloc(sizeof(text *) * (n > 0 ? n : 1));\n"
           "    for (idx_t i = 0; i < le.length; i++) arr[i] = MakeText(data[le.offset + i]);\n"
           "    *count = n;\n    return arr;\n}\n"
           "inline void FreeTextArr(text **arr, int n) {\n"
           "    for (int i = 0; i < n; i++) free(arr[i]);\n    free(arr);\n}\n"
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
    if FORWARDED:
        src += ("// The functions whose last parameter every SQL signature fixes to one literal, at the\n"
                "// arity the SQL surface calls them (project_bound_tail).\n"
                + "".join(FORWARDED) + "\n")
    PREAMBLE[:] = [src]

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
            sig, rett = "{MobilityDuckGeometryType(), LogicalType::INTEGER}", "SetTypes::h3indexset()"
        elif hpk == "geo2scalar":
            sig, rett = "{MobilityDuckGeometryType(), LogicalType::INTEGER}", "H3indexTypes::h3index()"
        elif hpk.startswith("cell_l:"):
            temp_acc = hpk.split(":", 1)[1]
            sig, rett = "{%s, %s}" % (CELL_BASEVAL[temp_acc], temp_acc), "LogicalType::BOOLEAN"
        elif hpk.startswith("cell_r:"):
            temp_acc = hpk.split(":", 1)[1]
            sig, rett = "{%s, %s}" % (temp_acc, CELL_BASEVAL[temp_acc]), "LogicalType::BOOLEAN"
        else:   # settemp_ebool
            sig, rett = "{SetTypes::h3indexset(), H3indexTypes::th3index()}", "LogicalType::BOOLEAN"
        for nm in names:
            set_specific_regs.append(f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                                     f'"{reg_name(nm, f)}", {sig}, {rett}, Gen_{fn}));')

    # The functions of a retired group over no temporal value that no shape above emits, one
    # registration per declared signature the body marshals (#shape_static_retired), as the
    # static cell functions register theirs
    _emitted = {m for acc in (bodies, set_bodies, span_bodies, temporal_box_bodies)
                for _g, _line in acc.items
                for m in re.findall(r'\bstatic void Gen_([A-Za-z0-9_]+)\(', _line)}
    for f in fns:
        if declared is not None and f["name"] not in declared:
            continue
        if f["name"] in _emitted:
            continue
        stc = shape_static_retired(f)
        if not stc:
            continue
        STATE["grp"] = f.get("group") or "meos_ungrouped"
        fn = f["name"]
        # one body per way of marshalling the operands and the result (a geometry and a
        # geography read apart), the first Gen_<fn>, the next Gen_<fn>_v1, ...
        # A signature whose DuckDB arguments another one already takes registers nothing: a
        # geography is read as a GEOMETRY, so set(geography) would answer the call set(geometry)
        # answers. The shorter form a trailing SQL default allows registers beside the full one.
        body_of, taken = {}, set()
        for s, sargs, sret in stc:
            key = (tuple(a[1:] for a in sargs), sret[1:])
            n_ = len(sargs)
            dflt = (s.get("argDefaults") or [None] * n_)[-1] if n_ > 1 else None
            forms = [(sargs, None)] + ([(sargs[:-1], sql_default_to_cpp(dflt))] if dflt is not None else [])
            for nm in reg_names(f, s.get("sqlName", f["sqlfn"]), aliases,
                                "{%s}" % ", ".join(a[0] for a in sargs)):
                for fargs, dv in forms:
                    slots = ", ".join(a[0] for a in fargs)
                    if (reg_name(nm, f).lower(), slots) in taken:
                        continue
                    taken.add((reg_name(nm, f).lower(), slots))
                    if key not in body_of:
                        body_of[key] = "" if not body_of else "_v%d" % len(body_of)
                    bkey = (key, dv)
                    if bkey not in body_of:
                        body_of[bkey] = body_of[key] + ("_d" if dv is not None else "")
                        bodies.append(emit_static_cell(f, sargs, sret, body_of[key], dv))
                    specific_regs.append(
                        f'    RegisterSerializedScalarFunction(loader, ScalarFunction('
                        f'"{reg_name(nm, f)}", {{{slots}}}, {sret[0]}, Gen_{fn}{body_of[bkey]}));')

    # a set-returning function over a set the other engines call by its `altSqlName`
    # (setUnnest): a table function over the set types its signatures declare. The typed
    # <set>_values functions also answer getValues, which an earlier arm emits, so they are read
    # here in a pass of their own
    for f in fns:
        if declared is not None and f["name"] not in declared:
            continue
        _su = shape_set_unnest(f)
        if not _su:
            continue
        STATE["grp"] = f.get("group") or "meos_ungrouped"
        su_name, su_accs, su_dt, su_ctype, su_write = _su
        bodies.append(emit_set_unnest(f, su_name, su_dt, su_ctype, su_write))
        for a in su_accs:
            specific_regs.append(f'    loader.RegisterFunction(SetUnnest_{f["name"]}Table({a}));')

    # The functions of a retired group no shape above emits, called per declared signature as the
    # catalog places its arguments (#shape_sig_call). A signature whose name and DuckDB arguments
    # another registration already takes registers nothing, as DuckDB refuses a second one.
    _made = {m for acc in (bodies, set_bodies, span_bodies, temporal_box_bodies)
             for _g, _line in acc.items
             for m in re.findall(r'\bstatic void Gen_([A-Za-z0-9_]+)\(', _line)}
    _regd = {(m.group(1).lower(), m.group(2))
             for acc in (generic_regs, specific_regs, set_generic_regs, set_specific_regs,
                         span_generic_regs, span_specific_regs, temporal_box_regs)
             for _g, _line in acc.items
             for m in [re.search(r'ScalarFunction\("([^"]+)", \{([^}]*)\}', _line)] if m}
    for f in fns:
        if declared is not None and f["name"] not in declared:
            continue
        if f["name"] in _made:
            continue
        plans = shape_sig_call(f)
        if not plans:
            continue
        STATE["grp"] = f.get("group") or "meos_ungrouped"
        fn = f["name"]
        nbody = 0
        for plan in plans:
            s, _order, sargs, _bound, nb, sret = plan
            for nargs, omitted in sig_call_forms(f, plan):
                slots = ", ".join(a["dt"] for a in sargs[:nargs])
                names = [nm for nm in reg_names(f, s.get("sqlName", f["sqlfn"]), aliases,
                                                "{%s}" % slots)
                         if (reg_name(nm, f).lower(), slots) not in _regd]
                if not names:
                    continue
                sfx = "" if nbody == 0 else "_v%d" % nbody
                nbody += 1
                bodies.append(emit_sig_call(f, plan, sfx, nargs, omitted))
                for nm in names:
                    _regd.add((reg_name(nm, f).lower(), slots))
                    sf = (f'ScalarFunction("{reg_name(nm, f)}", {{{slots}}}, {sret[0]}, '
                          f'Gen_{fn}{sfx})')
                    if any(k < nargs for k in nb):
                        # a NULL argument binds a value rather than answering NULL, so the
                        # function reads its NULL arguments itself
                        specific_regs.append(
                            f'    {{ ScalarFunction sf = {sf}; '
                            f'sf.null_handling = FunctionNullHandling::SPECIAL_HANDLING; '
                            f'RegisterSerializedScalarFunction(loader, sf); }}')
                    else:
                        specific_regs.append(
                            f'    RegisterSerializedScalarFunction(loader, {sf});')

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
    # A group's unit is a hash of its name alone, so a change to one group rewrites only the
    # unit that group lands in and every other unit keeps its bytes: the compiler cache then
    # serves them, where a packing by size moves groups between units whenever one grows and
    # rewrites them all. Sixteen units hold the largest one near the size of the units a packing
    # by size gives eight.
    bins = [[] for _ in range(GENERATED_CHUNKS)]
    for u in units:
        bins[zlib.crc32(u[0].encode()) % GENERATED_CHUNKS].append(u)

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
BASE_HEADER = {
    "jsonb": "json/tjsonb.hpp", "cbuffer": "cbuffer/tcbuffer.hpp",
    "npoint": "npoint/tnpoint.hpp", "h3index": "h3/th3index.hpp",
    "quadbin": "quadbin/tquadbin.hpp",
    "s2cell": "s2cell/ts2cell.hpp",
    "pose": "pose/tpose.hpp", "posechain": "posechain/tposechain.hpp",
    "pcpoint": "pointcloud/tpcpoint.hpp", "pcpatch": "pointcloud/tpcpatch.hpp",
    "raquet": "raster/raquet.hpp",
}
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
    # The set types the enum admits and the set names the UDF surface scopes by must be the same
    # list: a type registered without its name scope leaves its whole family unbound, and a scope
    # without a type names an accessor that does not compile. Both read BASE_ORDER, so this states
    # the invariant rather than checking two independent lists.
    reg_sets = {b + "set" for b in typereg_members(enum_names, "set")}
    if reg_sets != set(SET_TYPES):
        raise SystemExit("set type registration and SET_TYPES disagree: "
                         "registered-only %s, scoped-only %s"
                         % (sorted(reg_sets - set(SET_TYPES)), sorted(set(SET_TYPES) - reg_sets)))
    counts = {f["cls"]: len(typereg_members(enum_names, f["suffix"])) for f in TYPEREG_FAMILIES}
    return counts

def gen_temporal_covering(catalog, out_path):
    """Emit the TemporalParquet description of each temporal base type temporalFooter writes.

    The covering keys, the struct-column suffixes and the bounds each covering carries come from
    the catalog's temporalCovering descriptor (MEOS-API meta/temporal-covering.json), in the order
    it gives them. A field the descriptor conditions on a property of the value (`when`, the z
    bounds of a 3D value) is left out: temporalFooter reads only the base type of a column, so it
    states only what the base type fixes. The GeoParquet edges come from the catalog's
    temporalTypes: a geometry base is planar and a geography base spherical, the counterparts of the
    Parquet GEOMETRY and GEOGRAPHY logical types."""
    cov = catalog.get("temporalCovering") or {}
    by_type = cov.get("byType") or {}
    if not by_type:
        raise SystemExit("the catalog carries no temporalCovering: derive it from the MEOS-API "
                         "repository root, where run.py finds meta/temporal-covering.json")
    ttypes = catalog.get("temporalTypes", {})
    edges_of_base = {"geometry": "planar", "geography": "spherical"}
    unknown = sorted(t for t in by_type if t not in ttypes)
    if unknown:
        raise SystemExit("temporalCovering lists types absent from temporalTypes: %s" % unknown)
    # A type whose base fixes edges but that has no covering would lose its edges silently.
    uncovered = sorted(t for t, v in ttypes.items()
                       if v.get("base") in edges_of_base and t not in by_type)
    if uncovered:
        raise SystemExit("temporal types with a geometry or geography base and no covering: %s"
                         % uncovered)
    ident = lambda s: re.sub(r"[^A-Za-z0-9]", "_", s).upper()
    lines = [
        "// GENERATED by tools/codegen_duck_udfs.py — do not edit by hand.",
        "// The TemporalParquet covering of each temporal base type the catalog's",
        "// temporalCovering descriptor lists, and the GeoParquet edges its base type",
        "// fixes: a geometry base is planar and a geography base spherical.",
        "",
        '#include "temporal/temporal_covering.hpp"',
        "",
        "namespace duckdb {",
        "",
        "namespace {",
        "",
    ]
    entries = []
    for t, entry in by_type.items():
        rows = []
        for c in entry["coverings"]:
            template = c["column"]
            if not template.startswith("{col}"):
                raise SystemExit("covering %r of %s names the column %r, not {col}<suffix>"
                                 % (c["key"], t, template))
            bounds = [f["name"] for f in c["fields"] if "when" not in f]
            arr = "BOUNDS_%s_%s" % (ident(t), ident(c["key"]))
            lines.append("const char *const %s[] = {%s, nullptr};"
                         % (arr, ", ".join('"%s"' % b for b in bounds)))
            rows.append('    {"%s", "%s", %s},' % (c["key"], template[len("{col}"):], arr))
        lines.append("const TemporalCovering COVERINGS_%s[] = {" % ident(t))
        lines += rows
        lines += ["    {nullptr, nullptr, nullptr}};", ""]
        edges = edges_of_base.get(ttypes[t].get("base"))
        entries.append('    {"%s", %s, COVERINGS_%s},'
                       % (t, '"%s"' % edges if edges else "nullptr", ident(t)))
    lines.append("const TemporalCoveringType TEMPORAL_COVERING_TYPES[] = {")
    lines += entries
    lines += ["};", "", "} // namespace", "",
              "const TemporalCoveringType *TemporalCoveringOf(const std::string &base_type) {",
              "    for (const auto &entry : TEMPORAL_COVERING_TYPES) {",
              "        if (base_type == entry.base_type) return &entry;",
              "    }",
              "    return nullptr;",
              "}",
              "",
              "} // namespace duckdb",
              ""]
    open(out_path, "w").write("\n".join(lines))
    return len(entries)


def gen_index_search_ops(catalog, out_path):
    """The index search each operator name asks of an in-memory index, from the catalog's
    per-signature `indexSearch` {columnLeft, columnRight} (MEOS-API docs/index-search.md), as
    #gen_temporal_covering emits the covering from the catalog's temporalCovering. A signature is
    reached under its SQL name and under its operator's symbol, the symbol read from the doc
    comment of its columnLeft value in the catalog's IndexSearchOp enum ("..., `&<` operator").
    A name answering two different pairs fails generation: one name routes one way."""
    enum = next((e for e in catalog.get("enums", []) if e.get("name") == "IndexSearchOp"), None)
    if enum is None:
        raise SystemExit("FATAL: the catalog carries no IndexSearchOp enum")
    symbol = {}
    for v in enum["values"]:
        m = re.search(r"`([^`]+)`\s+operator", v.get("doc") or "")
        if not m:
            raise SystemExit("FATAL: IndexSearchOp %s names no operator in its doc" % v["name"])
        symbol[v["name"]] = m.group(1)
    routes = {}
    for f in catalog["functions"]:
        for sig in f.get("sqlSignatures") or []:
            ix = sig.get("indexSearch")
            if not ix:
                continue
            pair = (ix["columnLeft"], ix.get("columnRight"))
            for name in (symbol[ix["columnLeft"]], sig.get("sqlName") or f["sqlfn"]):
                routes.setdefault(name, set()).add(pair)
    clash = sorted(n for n, pairs in routes.items() if len(pairs) > 1)
    if clash:
        raise SystemExit("FATAL: names routed to two index searches: %s" % ", ".join(clash))
    lines = [
        "// GENERATED by tools/codegen_duck_udfs.py from the MEOS-API catalog — DO NOT EDIT.",
        "// The index search of every operator name, `column <name> query` and `query <name> column`,",
        "// from the per-signature indexSearch of the catalog.",
        "#pragma once",
        "",
        "#include \"meos_wrapper_simple.hpp\"",
        "",
        "namespace duckdb {",
        "",
        "//! A name an in-memory index answers and the MEOS search it asks for. A predicate is read as",
        "//! `column <name> query`; with the query on the left it asks for `commuted`, and an operator",
        "//! whose MobilityDB declaration has no COMMUTATOR (the overlapping orderings) has none, so a",
        "//! predicate written that way is answered by a scan.",
        "struct IndexOperatorEntry {",
        "\tconst char *name;",
        "\t//! The search of `column <name> query`",
        "\tIndexSearchOp direct;",
        "\t//! True when `query <name> column` has a search",
        "\tbool has_commuted;",
        "\t//! The search of `query <name> column`, meaningful only when has_commuted",
        "\tIndexSearchOp commuted;",
        "};",
        "",
        "static constexpr IndexOperatorEntry INDEX_OPERATORS[] = {",
    ]
    for name in sorted(routes):
        left, right = next(iter(routes[name]))
        lines.append('    {"%s", %s, %s, %s},' % (name, left, "true" if right else "false",
                                                  right or left))
    lines += ["};", "", "} // namespace duckdb", ""]
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    open(out_path, "w").write("\n".join(lines))
    return len(routes)

# ── aggregates: the catalog's `aggregates` section ──
# An aggregate of the catalog states, per CREATE AGGREGATE of MobilityDB, its argument and result
# types, the type of its state, and the public MEOS function of each role it defines: transition,
# combine and final. Each one registers as a #MeosAggregate (src/include/mobilityduck/
# meos_aggregate.hpp) over an operations struct written here, which decodes the arguments of a
# row, calls the roles and encodes the result; the template holds what is common to every one,
# the release of the states and the gathering of the rows of an aggregate stating no combine.
# The rules are JMEOS's (#_catalog_aggregates of tools/codegen_jvm.py): the SQL arguments after
# the first pair with the transition's parameters after its state in the order of the signature
# the aggregate names (its sqlArgParams), a parameter that signature binds takes its literal, an
# aggregate is registered under its `altSqlName` where it has one, since DuckDB holds a scalar
# and an aggregate of one name in one catalog slot, and under its own name otherwise.

# The DuckDB type of each SQL type an aggregate takes or answers. A SQL type absent from it is one
# the binding registers no type for (raster, geography, summarystats), and an aggregate over it is
# left out.
AGG_SQL_TYPE = {**SIG_TEMPORAL_ACC, **SET_SIG_ACC, **SQL_CONTAINER_ACC, **SQL_BASE_TO_DUCK, **CELL_SQL,
                **{b: acc for b, acc in BASE_LOGICAL.items() if not acc.startswith("LogicalType::")},
                "geometry": "MobilityDuckGeometryType()",
                "geomset": "SpatialSetType::geomset()", "geogset": "SpatialSetType::geogset()"}
# The C++ type a column of each SQL scalar type holds; every other type is a blob (string_t).
AGG_SQL_PHYSICAL = {"integer": "int32_t", "bigint": "int64_t", "float": "double", "boolean": "bool",
                    "date": "date_t", "timestamptz": "timestamp_tz_t", "interval": "interval_t",
                    **{c: "int64_t" for c in CELL_SQL}}
# The C value of a by-value parameter from the column value `v`: (field type, expression).
AGG_BYVALUE = {"int": ("int32_t", "v"), "int32": ("int32_t", "v"), "int32_t": ("int32_t", "v"),
               "int64": ("int64_t", "v"), "int64_t": ("int64_t", "v"),
               "double": ("double", "v"), "float8": ("double", "v"), "bool": ("bool", "v"),
               "uint64_t": ("uint64_t", "(uint64_t) v"),
               "TimestampTz": ("TimestampTz", "(TimestampTz) DuckDBToMeosTimestamp(v).value"),
               "DateADT": ("DateADT", "ToMeosDate(v)")}
# The encoder of a result of each C type into a DuckDB blob, releasing it.
AGG_WRITE = {**{b: e for b, (_, e) in PTR_RET.items()}, "TPCBox": "TpcboxToBlob(result, %s)"}
# The MEOS function answering the start timestamp of a temporal value, the order an aggregate with
# no combine folds its rows in.
AGG_ORDER_KEY = "temporal_start_timestamptz"


def _agg_literal(p, lit):
    """The C++ literal of a bound or defaulted parameter, or None."""
    b = base(p["canonical"])
    if lit.strip().upper() == "NULL":
        return "nullptr" if "*" in norm(p["canonical"]) else None
    if b in ENUM_MEMBERS:
        return lit if lit in ENUM_MEMBERS[b] else None
    if b in AGG_BYVALUE:
        return sql_default_to_cpp(lit)
    return None


def _agg_input(p, sql, k):
    """How SQL argument k of type `sql` reaches the C parameter p: (fields, decode, call, free)
    with `v` the column value in decode, or None."""
    canon, b = norm(p["canonical"]), base(p["canonical"])
    a = "row.a%d" % k
    if "*" in canon:
        if b in PTR_IN and AGG_SQL_PHYSICAL.get(sql) is None:
            dec = PTR_IN[b][1] % "v"
            ft = "Temporal" if PTR_IN[b][0] == "MD_TEMPORAL" else b
            call = a if ft == b else "(const %s *) %s" % (b, a)
            return ["%s *a%d;" % (ft, k)], "%s = %s;" % (a, dec), call, "free(%s);" % a
        if b == "GSERIALIZED" and sql == "geometry":
            return (["GSERIALIZED *a%d;" % k], "%s = GeometryToGSerialized(v, 0);" % a, a,
                    "free(%s);" % a)
        if b == "text" and sql == "text":
            return ["text *a%d;" % k], "%s = MakeText(v);" % a, a, "free(%s);" % a
        if b == "Interval" and sql == "interval":
            return (["MeosInterval a%d;" % k, "bool a%d_set;" % k],
                    "%s = IntervaltToInterval(v); %s_set = true;" % (a, a),
                    "(%s_set ? &%s : nullptr)" % (a, a), "")
        return None
    if b in ENUM_READER and sql == "text":
        return (["%s a%d;" % (b, k)], "%s = %s(v.GetString().c_str());" % (a, ENUM_READER[b]),
                a, "")
    if b in AGG_BYVALUE and sql in AGG_SQL_PHYSICAL:
        ft, e = AGG_BYVALUE[b]
        return ["%s a%d;" % (ft, k)], "%s = %s;" % (a, e), a, ""
    return None


def _agg_ops(n, a, fns_by_name):
    """The operations struct of catalog aggregate `a`, numbered n, or (None, reason)."""
    roles = {r: (a.get(r) or {}).get("meos") for r in ("transition", "combine", "final")}
    args, ret, stype = a.get("args") or [], a["ret"], a["stype"]
    if not args:
        return None, "no argument"
    for t in args + [ret]:
        if t not in AGG_SQL_TYPE:
            return None, "type %s" % t
    if any(r and r not in fns_by_name for r in roles.values()):
        return None, "role not in the catalog"
    tf = fns_by_name[roles["transition"]]
    gathers = not roles["combine"]
    if gathers and AGG_ORDER_KEY not in fns_by_name:
        return None, "no combine and no %s" % AGG_ORDER_KEY
    params = tf.get("params") or []
    state_c = base(params[0]["canonical"])
    vis = params[1:]
    sig = next((s for s in tf.get("sqlSignatures") or [] if s.get("args") == [stype] + args), {})
    bound = {**((tf.get("shape") or {}).get("boundArgs") or {}), **(sig.get("boundArgs") or {})}
    bound = {n: v for n, v in bound.items() if n in {p["name"] for p in vis}}
    walk = [i for i, p in enumerate(vis) if p["name"] not in bound]
    order = sig.get("sqlArgParams") or (tf.get("shape") or {}).get("sqlArgParams")
    if order:
        at = {p["name"]: i for i, p in enumerate(vis)}
        if set(order) <= set(at) and sorted(at[n] for n in order) == walk:
            walk = [at[n] for n in order]
    if len(walk) != len(args):
        return None, "arity"
    call = [None] * len(vis)
    for i, p in enumerate(vis):
        if p["name"] in bound:
            call[i] = _agg_literal(p, bound[p["name"]])
            if call[i] is None:
                return None, "bound %s" % bound[p["name"]]
    nullable = set((tf.get("shape") or {}).get("nullable") or [])
    null_binds = sig.get("nullDefaultBinds") or {}
    fields, checks, decodes, frees = [], [], [], []
    for k, (i, sql) in enumerate(zip(walk, args)):
        p = vis[i]
        got = _agg_input(p, sql, k)
        if got is None:
            return None, "argument %s/%s" % (sql, norm(p["canonical"]))
        fl, dec, c, fr = got
        fields += fl
        call[i] = c
        if fr:
            frees.append(fr)
        phys = AGG_SQL_PHYSICAL.get(sql, "string_t")
        checks.append("        idx_t j%d = fmt[%d].sel->get_index(i);\n"
                      "        bool n%d = !fmt[%d].validity.RowIsValid(j%d);" % (k, k, k, k, k))
        get = "UnifiedVectorFormat::GetData<%s>(fmt[%d])[j%d]" % (phys, k, k)
        # A NULL argument: the first skips the row, as a strict transition does; another takes
        # the value the wrapper binds for it (nullDefaultBinds), or a NULL pointer where the
        # parameter admits one, and otherwise skips the row
        if k == 0:
            null = "return false;"
        else:
            lit = (null_binds.get(str(k + 1)) or {}).get(p["name"])
            lit = _agg_literal(p, lit) if lit is not None else None
            if lit is not None and fl[0].startswith("MeosInterval"):
                null = "row.a%d_set = false;" % k
            elif lit is not None:
                null = "row.a%d = %s;" % (k, lit)
            elif p["name"] in nullable and "*" in norm(p["canonical"]):
                null = ("row.a%d_set = false;" % k if fl[0].startswith("MeosInterval")
                        else "row.a%d = nullptr;" % k)
            else:
                null = "return false;"
        # Every skip is decided before anything is decoded, so a skipped row allocates nothing
        if null == "return false;":
            checks.append("        if (n%d) return false;" % k)
            decodes.append("        { auto &v = %s; %s }" % (get, dec))
        else:
            decodes.append("        if (n%d) { %s } else { auto &v = %s; %s }" % (k, null, get, dec))
    ff = fns_by_name.get(roles["final"]) if roles["final"] else None
    keeps = bool(ff and ff.get("params") and norm(ff["params"][0]["canonical"]).startswith("const "))
    res_c = base((ff or tf)["returnType"]["canonical"])
    if res_c not in AGG_WRITE:
        return None, "result %s" % res_c
    cast = lambda fn, i: "static_cast<%s>" % norm(fn["params"][i]["canonical"])  # noqa: E731
    name = "AggOps_%d" % n
    L = ["struct %s {" % name,
         "    struct Row {", *("        %s" % f for f in fields), "    };",
         "    static constexpr idx_t ARITY = %d;" % len(args),
         "    static constexpr bool GATHERS = %s;" % ("true" if gathers else "false"),
         "    static constexpr bool HAS_FINAL = %s;" % ("true" if ff else "false"),
         "    static constexpr bool KEEPS = %s;" % ("true" if keeps else "false"),
         "    static bool Read(UnifiedVectorFormat fmt[], idx_t i, Row &row) {",
         *checks, *decodes, "        return true;", "    }",
         "    static void *Transition(void *s, Row &row) {",
         "        return %s(%s(s)%s);" % (tf["name"], cast(tf, 0), "".join(", " + c for c in call)),
         "    }",
         "    static void Free(Row &row) {",
         *("        %s" % fr for fr in frees),
         "        (void) row;", "    }"]
    if roles["combine"]:
        cf = fns_by_name[roles["combine"]]
        L += ["    static void *Combine(void *a, void *b) {",
              "        return %s(%s(a), %s(b));" % (cf["name"], cast(cf, 0), cast(cf, 1)), "    }"]
    else:
        L += ["    static void *Combine(void *, void *) {", "        return nullptr;", "    }"]
    if ff:
        L += ["    static void *Final(void *s) {",
              "        return %s(%s(s));" % (ff["name"], cast(ff, 0)), "    }"]
    else:
        L += ["    static void *Final(void *s) {", "        return s;", "    }"]
    L += ["    static void Write(Vector &result, idx_t i, void *r) {",
          "        FlatVector::GetData<string_t>(result)[i] = %s;"
          % (AGG_WRITE[res_c] % ("static_cast<%s *>(r)" % res_c)), "    }",
          "    static TimestampTz Order(const Row &row) {",
          ("        return %s(row.a0);" % AGG_ORDER_KEY) if gathers else
          "        (void) row;\n        return 0;", "    }",
          "};", ""]
    reg = "MeosAggregate<%s>::Function({%s}, %s)" % (
        name, ", ".join(AGG_SQL_TYPE[t] for t in args), AGG_SQL_TYPE[ret])
    return ("\n".join(L), reg, state_c), None


def gen_aggregates(d, out_path, scalars):
    """Write one unit registering every aggregate of the catalog's `aggregates` section that the
    binding's types reach, grouped into one function set per name, and answer
    (registered, {reason: count}) for what is left out. `scalars` holds the folded names of the
    scalar functions the surface registers: an aggregate sharing one is a load failure, so it
    stops the generation."""
    fns_by_name = {f["name"]: f for f in d["functions"]}
    sets, left, seen, bodies = defaultdict(list), Counter(), set(), []
    for a in d.get("aggregates") or []:
        name = a.get("altSqlName") or a["sqlName"]
        key = (name.lower(), tuple(a.get("args") or []))
        if key in seen:
            continue
        got, why = _agg_ops(len(bodies), a, fns_by_name)
        if got is None:
            left[why] += 1
            continue
        seen.add(key)
        body, reg, _ = got
        bodies.append(body)
        sets[name].append(reg)
    clash = sorted(n for n in sets if n.lower() in scalars)
    if clash:
        raise SystemExit("an aggregate and a scalar share a name, which DuckDB keeps in one "
                         "catalog slot: %s" % ", ".join(clash))
    regs = []
    for name in sorted(sets, key=str.lower):
        regs.append("    {\n        AggregateFunctionSet set(\"%s\");" % name)
        regs += ["        set.AddFunction(%s);" % r for r in sets[name]]
        regs.append("        RegisterMeosFunction(loader, std::move(set));\n    }")
    text = (PREAMBLE[0].replace('#include "mobilityduck/meos_exec_serial.hpp"\n',
                                '#include "mobilityduck/meos_exec_serial.hpp"\n'
                                '#include "mobilityduck/meos_aggregate.hpp"\n', 1)
            + "\n".join(bodies) + "} // anonymous\n\n"
            + "// The aggregates of the MEOS catalog, one function set per name.\n"
            + "void RegisterGeneratedAggregates(ExtensionLoader &loader) {\n"
            + "\n".join(regs) + "\n}\n\n} // namespace duckdb\n")
    open(out_path, "w").write(text)
    return sum(len(v) for v in sets.values()), len(sets), left


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
    # BASE_CBASE names each base value type's C struct, the one datum the lowercase base does not
    # spell. Read it back from the catalog rather than trusting the spelling: the struct a base's
    # own functions take as their first pointer argument IS that base's C type, so a rename
    # upstream reports here instead of silently emitting a container that matches nothing.
    for _b, _cb in BASE_CBASE.items():
        _seen = collections.Counter(
            base(f["params"][0]["canonical"]) for f in fns
            if f["name"].startswith(_b + "_") and f.get("params")
            and norm(f["params"][0]["canonical"]).endswith("*"))
        if not _seen or _seen.most_common(1)[0][0] != _cb:
            raise SystemExit("BASE_CBASE[%r] = %r does not match the catalog: %s"
                             % (_b, _cb, _seen.most_common(3) or "no pointer-carried function"))
    # struct layouts (e.g. Match {i,j}) for the array-return LIST(STRUCT) shape — from the catalog.
    STRUCTS.update({s["name"]: s for s in d.get("structs", [])})
    # macro values, so a literal a PG wrapper binds by macro name renders as its value.
    MACRO_VALUES.update({m["name"]: m["value"] for m in d.get("macros", []) if "value" in m})
    ENUM_MEMBERS.update({e["name"]: {v["name"] for v in e.get("values") or []}
                         for e in d.get("enums", [])})
    # An enum is read from its name; so is a by-value C type no SQL scalar carries and one public
    # function reads from a string, which the SQL text stands for: the WKB variant every writer
    # takes, read from its endian by wkb_variant_from_endian. The rule is JMEOS's
    # (_enum_parsers in tools/codegen_jvm.py).
    _readers = defaultdict(list)
    for _f in fns:
        _ps = _f.get("params") or []
        _rt = norm(_f["returnType"]["canonical"])
        if (_f.get("api") == "public" and len(_ps) == 1 and norm(_ps[0]["canonical"]) == "char *"
                and "*" not in _rt):
            _readers[_rt].append(_f["name"])
    for _rt, _names in _readers.items():
        if _rt in ENUM_MEMBERS or (len(_names) == 1 and _rt not in SCALAR_ARG):
            ENUM_READER.setdefault(_rt, _names[0])
    # A typed text or binary output names two wrappers, its type's I/O function and its asText
    # (tint_out: `#Temporal_out(), #Temporal_as_text()`), so its first wrapper's SQL name is an
    # I/O name, which supported() drops as a cast, while one of its signatures carries the SQL name
    # the surface registers. Such a function takes that name when its signatures carry exactly
    # one non-I/O name, and keeps that name's signatures only.
    for f in fns:
        if not re.search(r'_(out|in|send|recv)$', f.get("sqlfn") or ""):
            continue
        sigs = f.get("sqlSignatures") or []
        other = {s.get("sqlName") for s in sigs
                 if s.get("sqlName") and not re.search(r'_(out|in|send|recv)$', s["sqlName"])}
        if len(other) == 1:
            f["sqlfn"] = other.pop()
            f["sqlSignatures"] = [s for s in sigs if s.get("sqlName") == f["sqlfn"]]
    # A last parameter every signature fixes to one literal leaves the SQL surface: the shapes
    # see the shorter record, and the generated units forward it with the literal.
    projected = {f["name"]: project_bound_tail(f) for f in fns}
    fixed = {p["name"] for f in fns if projected[f["name"]]
             for p in f["params"] if p not in projected[f["name"]]["params"]}
    fns = [projected[f["name"]] or project_bound_tail(f, fixed) or f for f in fns]
    # A retired group's projected function keeps the longer call too, over the signatures that
    # declare the parameter (length(tgeogpoint, boolean DEFAULT true), where a false asks for the
    # sphere): its own record #keep_declared_tail forwards under another name, without the
    # default, so the shorter call stays the projected record's alone.
    fns = fns + [k for f in fns for k in keep_declared_tail(f)]
    fns = [g for f in fns for g in project_signature_forms(f)]
    fns = yield_to_type_kernels(fns)
    # name -> catalog entry, so the name-keyed scope rules can read the function's own
    # sqlSignatures (the tspatial_* surface asks which types MobilityDB declares it for).
    FN_BY_NAME.update({f["name"]: f for f in fns})
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
        # The TemporalParquet description temporalFooter writes, from the catalog's
        # temporalCovering descriptor and temporalTypes, emitted as a sibling file.
        tcov = os.path.join(os.path.dirname(out), "generated_temporal_covering.cpp")
        ncov = gen_temporal_covering(d, tcov)
        print(f"wrote the TemporalParquet covering of {ncov} temporal types -> {tcov}")
        # The index search of every operator name, from the catalog's per-signature indexSearch,
        # emitted beside the hand-written routing that reads it (src/include/index).
        isearch = os.path.join(os.path.dirname(os.path.dirname(out)), "include", "generated",
                               "index_search_ops.hpp")
        nis = gen_index_search_ops(d, isearch)
        print(f"wrote the index search of {nis} operator names -> {isearch}")
        # The aggregates of the catalog, emitted as a sibling unit.
        agg = os.path.join(os.path.dirname(out), "generated_aggregates.cpp")
        scalars = {n.lower() for n in re.findall(r'ScalarFunction\("([^"]+)"', GENERATED_TEXT[0])}
        na, nn, agg_left = gen_aggregates(d, agg, scalars)
        print(f"wrote {na} aggregates under {nn} names -> {agg}; left out: {dict(agg_left)}")
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
            # A type I/O name (intset_recv, intset_send) is PostgreSQL's type support, which this
            # surface never registers, as #supported drops it; the function's other signatures
            # (intsetFromBinary, asBinary) are what the shapes emit.
            if (f.get("group") in RETIRED_GROUPS) and f.get("sqlfn") and not f.get("sqlfnBackingOnly") \
                    and not re.search(r'_(out|in|send|recv)$', f["sqlfn"]):
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
        argts = ", ".join(arg_type(p["canonical"], f["name"])[0] for p in ins)
        rt, kind = ret_type(f, out)
        print(f'  // {f["sqlfn"]}  <- {f["name"]}  ({kind})')
        print(f'  RegisterSerializedScalarFunction(loader, ScalarFunction("{f["sqlfn"]}", '
              f'{{{argts}}}, {rt}, Gen_{f["name"]}));')
    return emittable, by_fam

if __name__ == "__main__":
    main()
