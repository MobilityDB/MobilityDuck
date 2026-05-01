// Cross-type predicate registrations for tgeometry. The MEOS C functions
// dispatched here all take Temporal * (subtype-agnostic), so this file is
// purely registration-and-glue; the heavy lifting stays in libmeos.

#include "meos_wrapper_simple.hpp"

#include "common.hpp"
#include "geo/tgeometry.hpp"
#include "geo/tgeometry_ops.hpp"
#include "geo/stbox.hpp"
#include "temporal/span.hpp"
#include "temporal/temporal.hpp"
#include "geo_util.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "spatial/spatial_types.hpp"

extern "C" {
    #include <meos.h>
    #include <meos_geo.h>
    #include <meos_internal.h>
    #include <meos_internal_geo.h>
}

namespace duckdb {

namespace {

// ====================================================================
// Argument-decoding helpers
// ====================================================================

inline Temporal *DecodeTemporalCopy(string_t blob) {
    size_t sz = blob.GetSize();
    Temporal *t = (Temporal *) malloc(sz);
    memcpy(t, blob.GetData(), sz);
    return t;
}

inline STBox *DecodeStboxCopy(string_t blob) {
    STBox *b = (STBox *) malloc(sizeof(STBox));
    memcpy(b, blob.GetData(), sizeof(STBox));
    return b;
}

inline Span *DecodeSpanCopy(string_t blob) {
    Span *s = (Span *) malloc(sizeof(Span));
    memcpy(s, blob.GetData(), sizeof(Span));
    return s;
}

// ====================================================================
// Bool-returning binary BLOB×BLOB executors (boxops + posops)
// ====================================================================

template <bool (*FN)(const Temporal *, const STBox *)>
void TspatialStboxBoolExec(DataChunk &args, ExpressionState &, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t t_blob, string_t b_blob) {
            Temporal *t = DecodeTemporalCopy(t_blob);
            STBox *b = DecodeStboxCopy(b_blob);
            bool r = FN(t, b);
            free(t); free(b);
            return r;
        });
}

template <bool (*FN)(const Temporal *, const STBox *)>
void StboxTspatialBoolExec(DataChunk &args, ExpressionState &, Vector &result) {
    // The MEOS function takes (Temporal, STBox); for the (STBox, Temporal)
    // SQL signature we just swap argument order — these predicates are
    // commutative for box-only checks (overlaps, same, adjacent) and the
    // non-commutative pairs (contains/contained) are already defined as a
    // distinct MEOS pair.
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t b_blob, string_t t_blob) {
            STBox *b = DecodeStboxCopy(b_blob);
            Temporal *t = DecodeTemporalCopy(t_blob);
            bool r = FN(t, b);
            free(t); free(b);
            return r;
        });
}

template <bool (*FN)(const Temporal *, const Temporal *)>
void TspatialTspatialBoolExec(DataChunk &args, ExpressionState &, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b) {
            Temporal *t1 = DecodeTemporalCopy(a);
            Temporal *t2 = DecodeTemporalCopy(b);
            bool r = FN(t1, t2);
            free(t1); free(t2);
            return r;
        });
}

template <bool (*FN)(const Temporal *, const Span *)>
void TemporalTstzspanBoolExec(DataChunk &args, ExpressionState &, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t t_blob, string_t s_blob) {
            Temporal *t = DecodeTemporalCopy(t_blob);
            Span *s = DecodeSpanCopy(s_blob);
            bool r = FN(t, s);
            free(t); free(s);
            return r;
        });
}

template <bool (*FN)(const Span *, const Temporal *)>
void TstzspanTemporalBoolExec(DataChunk &args, ExpressionState &, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t s_blob, string_t t_blob) {
            Span *s = DecodeSpanCopy(s_blob);
            Temporal *t = DecodeTemporalCopy(t_blob);
            bool r = FN(s, t);
            free(t); free(s);
            return r;
        });
}

// ====================================================================
// Spatial-relation int→bool helpers (e/a contains/disjoint/intersects/
// touches and the dwithin family that adds a distance argument)
// ====================================================================

template <int (*FN)(const Temporal *, const GSERIALIZED *)>
void TgeoGeoIntExec(DataChunk &args, ExpressionState &, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t t_blob, string_t g_blob, ValidityMask &mask, idx_t idx) {
            Temporal *t = DecodeTemporalCopy(t_blob);
            int32 srid = tspatial_srid(t);
            GSERIALIZED *gs = GeometryToGSerialized(g_blob, srid);
            int r = FN(t, gs);
            free(t); free(gs);
            if (r < 0) { mask.SetInvalid(idx); return false; }
            return r != 0;
        });
}

template <int (*FN)(const GSERIALIZED *, const Temporal *)>
void GeoTgeoIntExec(DataChunk &args, ExpressionState &, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t g_blob, string_t t_blob, ValidityMask &mask, idx_t idx) {
            Temporal *t = DecodeTemporalCopy(t_blob);
            int32 srid = tspatial_srid(t);
            GSERIALIZED *gs = GeometryToGSerialized(g_blob, srid);
            int r = FN(gs, t);
            free(t); free(gs);
            if (r < 0) { mask.SetInvalid(idx); return false; }
            return r != 0;
        });
}

template <int (*FN)(const Temporal *, const Temporal *)>
void TgeoTgeoIntExec(DataChunk &args, ExpressionState &, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) {
            Temporal *t1 = DecodeTemporalCopy(a);
            Temporal *t2 = DecodeTemporalCopy(b);
            int r = FN(t1, t2);
            free(t1); free(t2);
            if (r < 0) { mask.SetInvalid(idx); return false; }
            return r != 0;
        });
}

template <int (*FN)(const Temporal *, const GSERIALIZED *, double)>
void TgeoGeoDistIntExec(DataChunk &args, ExpressionState &, Vector &result) {
    TernaryExecutor::ExecuteWithNulls<string_t, string_t, double, bool>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t t_blob, string_t g_blob, double dist, ValidityMask &mask, idx_t idx) {
            Temporal *t = DecodeTemporalCopy(t_blob);
            int32 srid = tspatial_srid(t);
            GSERIALIZED *gs = GeometryToGSerialized(g_blob, srid);
            int r = FN(t, gs, dist);
            free(t); free(gs);
            if (r < 0) { mask.SetInvalid(idx); return false; }
            return r != 0;
        });
}

template <int (*FN)(const GSERIALIZED *, const Temporal *, double)>
void GeoTgeoDistIntExec(DataChunk &args, ExpressionState &, Vector &result) {
    TernaryExecutor::ExecuteWithNulls<string_t, string_t, double, bool>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t g_blob, string_t t_blob, double dist, ValidityMask &mask, idx_t idx) {
            Temporal *t = DecodeTemporalCopy(t_blob);
            int32 srid = tspatial_srid(t);
            GSERIALIZED *gs = GeometryToGSerialized(g_blob, srid);
            int r = FN(gs, t, dist);
            free(t); free(gs);
            if (r < 0) { mask.SetInvalid(idx); return false; }
            return r != 0;
        });
}

// dwithin's (GEOM, TGEOM, dist) signature reuses the (TGEOM, GEOM, dist)
// MEOS function with arguments swapped — distance is symmetric.
template <int (*FN)(const Temporal *, const GSERIALIZED *, double)>
void GeoTgeoDistIntExec_FromTgeoGeo(DataChunk &args, ExpressionState &, Vector &result) {
    TernaryExecutor::ExecuteWithNulls<string_t, string_t, double, bool>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t g_blob, string_t t_blob, double dist, ValidityMask &mask, idx_t idx) {
            Temporal *t = DecodeTemporalCopy(t_blob);
            int32 srid = tspatial_srid(t);
            GSERIALIZED *gs = GeometryToGSerialized(g_blob, srid);
            int r = FN(t, gs, dist);
            free(t); free(gs);
            if (r < 0) { mask.SetInvalid(idx); return false; }
            return r != 0;
        });
}

template <int (*FN)(const Temporal *, const Temporal *, double)>
void TgeoTgeoDistIntExec(DataChunk &args, ExpressionState &, Vector &result) {
    TernaryExecutor::ExecuteWithNulls<string_t, string_t, double, bool>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t a, string_t b, double dist, ValidityMask &mask, idx_t idx) {
            Temporal *t1 = DecodeTemporalCopy(a);
            Temporal *t2 = DecodeTemporalCopy(b);
            int r = FN(t1, t2, dist);
            free(t1); free(t2);
            if (r < 0) { mask.SetInvalid(idx); return false; }
            return r != 0;
        });
}

// ====================================================================
// Temporal-relation Temporal→Temporal helpers — `restr=false`,
// `atvalue=false` are the SQL defaults that produce a temporal value
// covering the whole input duration.
// ====================================================================

inline string_t TemporalToBlob(Vector &result, Temporal *t) {
    size_t sz = temporal_mem_size(t);
    string_t out = StringVector::AddStringOrBlob(
        result, reinterpret_cast<const char *>(t), sz);
    free(t);
    return out;
}

template <Temporal *(*FN)(const Temporal *, const GSERIALIZED *, bool, bool)>
void TgeoGeoTempExec(DataChunk &args, ExpressionState &, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t t_blob, string_t g_blob, ValidityMask &mask, idx_t idx) -> string_t {
            Temporal *t = DecodeTemporalCopy(t_blob);
            int32 srid = tspatial_srid(t);
            GSERIALIZED *gs = GeometryToGSerialized(g_blob, srid);
            Temporal *r = FN(t, gs, false, false);
            free(t); free(gs);
            if (!r) { mask.SetInvalid(idx); return string_t(); }
            return TemporalToBlob(result, r);
        });
}

template <Temporal *(*FN)(const GSERIALIZED *, const Temporal *, bool, bool)>
void GeoTgeoTempExec(DataChunk &args, ExpressionState &, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t g_blob, string_t t_blob, ValidityMask &mask, idx_t idx) -> string_t {
            Temporal *t = DecodeTemporalCopy(t_blob);
            int32 srid = tspatial_srid(t);
            GSERIALIZED *gs = GeometryToGSerialized(g_blob, srid);
            Temporal *r = FN(gs, t, false, false);
            free(t); free(gs);
            if (!r) { mask.SetInvalid(idx); return string_t(); }
            return TemporalToBlob(result, r);
        });
}

template <Temporal *(*FN)(const Temporal *, const Temporal *, bool, bool)>
void TgeoTgeoTempExec(DataChunk &args, ExpressionState &, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> string_t {
            Temporal *t1 = DecodeTemporalCopy(a);
            Temporal *t2 = DecodeTemporalCopy(b);
            Temporal *r = FN(t1, t2, false, false);
            free(t1); free(t2);
            if (!r) { mask.SetInvalid(idx); return string_t(); }
            return TemporalToBlob(result, r);
        });
}

// tDwithin variants take an extra distance argument.
template <Temporal *(*FN)(const Temporal *, const GSERIALIZED *, double, bool, bool)>
void TgeoGeoDistTempExec(DataChunk &args, ExpressionState &, Vector &result) {
    TernaryExecutor::ExecuteWithNulls<string_t, string_t, double, string_t>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t t_blob, string_t g_blob, double dist, ValidityMask &mask, idx_t idx) -> string_t {
            Temporal *t = DecodeTemporalCopy(t_blob);
            int32 srid = tspatial_srid(t);
            GSERIALIZED *gs = GeometryToGSerialized(g_blob, srid);
            Temporal *r = FN(t, gs, dist, false, false);
            free(t); free(gs);
            if (!r) { mask.SetInvalid(idx); return string_t(); }
            return TemporalToBlob(result, r);
        });
}

template <Temporal *(*FN)(const GSERIALIZED *, const Temporal *, double, bool, bool)>
void GeoTgeoDistTempExec(DataChunk &args, ExpressionState &, Vector &result) {
    TernaryExecutor::ExecuteWithNulls<string_t, string_t, double, string_t>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t g_blob, string_t t_blob, double dist, ValidityMask &mask, idx_t idx) -> string_t {
            Temporal *t = DecodeTemporalCopy(t_blob);
            int32 srid = tspatial_srid(t);
            GSERIALIZED *gs = GeometryToGSerialized(g_blob, srid);
            Temporal *r = FN(gs, t, dist, false, false);
            free(t); free(gs);
            if (!r) { mask.SetInvalid(idx); return string_t(); }
            return TemporalToBlob(result, r);
        });
}

template <Temporal *(*FN)(const Temporal *, const Temporal *, double, bool, bool)>
void TgeoTgeoDistTempExec(DataChunk &args, ExpressionState &, Vector &result) {
    TernaryExecutor::ExecuteWithNulls<string_t, string_t, double, string_t>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t a, string_t b, double dist, ValidityMask &mask, idx_t idx) -> string_t {
            Temporal *t1 = DecodeTemporalCopy(a);
            Temporal *t2 = DecodeTemporalCopy(b);
            Temporal *r = FN(t1, t2, dist, false, false);
            free(t1); free(t2);
            if (!r) { mask.SetInvalid(idx); return string_t(); }
            return TemporalToBlob(result, r);
        });
}

// ====================================================================
// Distance helpers — `tdistance(...)` and `<->`
// ====================================================================

template <Temporal *(*FN)(const Temporal *, const GSERIALIZED *)>
void TgeoGeoDistanceExec(DataChunk &args, ExpressionState &, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t t_blob, string_t g_blob, ValidityMask &mask, idx_t idx) -> string_t {
            Temporal *t = DecodeTemporalCopy(t_blob);
            int32 srid = tspatial_srid(t);
            GSERIALIZED *gs = GeometryToGSerialized(g_blob, srid);
            Temporal *r = FN(t, gs);
            free(t); free(gs);
            if (!r) { mask.SetInvalid(idx); return string_t(); }
            return TemporalToBlob(result, r);
        });
}

template <Temporal *(*FN)(const Temporal *, const Temporal *)>
void TgeoTgeoDistanceExec(DataChunk &args, ExpressionState &, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> string_t {
            Temporal *t1 = DecodeTemporalCopy(a);
            Temporal *t2 = DecodeTemporalCopy(b);
            Temporal *r = FN(t1, t2);
            free(t1); free(t2);
            if (!r) { mask.SetInvalid(idx); return string_t(); }
            return TemporalToBlob(result, r);
        });
}

} // namespace

void TGeometryOps::RegisterScalarFunctions(ExtensionLoader &loader) {
    const LogicalType TGEOM = TGeometryTypes::TGEOMETRY();
    const LogicalType GEOM  = GeoTypes::GEOMETRY();
    const LogicalType STBOX = StboxType::STBOX();
    const LogicalType TSTZSPAN = SpanTypes::TSTZSPAN();
    const LogicalType BOOL = LogicalType::BOOLEAN;
    const LogicalType DBL = LogicalType::DOUBLE;
    const LogicalType TFLOAT = TemporalTypes::TFLOAT();

    // -----------------------------------------------------------------
    // Box predicates: temporal_(overlaps|contains|contained|same|adjacent)
    // and the equivalent operators (&&, @>, <@, ~=, -|-).
    // -----------------------------------------------------------------
    struct BoxOp {
        const char *fn_name;
        const char *op_name;
        bool (*tspatial_stbox)(const Temporal *, const STBox *);
        bool (*tspatial_tspatial)(const Temporal *, const Temporal *);
        bool (*temporal_tstzspan)(const Temporal *, const Span *);
        bool (*tstzspan_temporal)(const Span *, const Temporal *);
    };
    const BoxOp box_ops[] = {
        {"temporal_overlaps", "&&",
         overlaps_tspatial_stbox, overlaps_tspatial_tspatial,
         overlaps_temporal_tstzspan, overlaps_tstzspan_temporal},
        {"temporal_contains", "@>",
         contains_tspatial_stbox, contains_tspatial_tspatial,
         contains_temporal_tstzspan, contains_tstzspan_temporal},
        {"temporal_contained", "<@",
         contained_tspatial_stbox, contained_tspatial_tspatial,
         contained_temporal_tstzspan, contained_tstzspan_temporal},
        {"temporal_same",     "~=",
         same_tspatial_stbox, same_tspatial_tspatial,
         same_temporal_tstzspan, same_tstzspan_temporal},
        {"temporal_adjacent", "-|-",
         adjacent_tspatial_stbox, adjacent_tspatial_tspatial,
         adjacent_temporal_tstzspan, adjacent_tstzspan_temporal},
    };

    // Build per-overload registrations through static dispatch on the
    // function pointer values supplied above. We can't bind those to
    // template parameters at runtime, so we register each predicate
    // explicitly below using the macros.
#define BOX_REG(NAME, OP, F_TSPAT_STBOX, F_TSPAT_TSPAT, F_TEMP_TSTZSPAN, F_TSTZSPAN_TEMP) \
    do { \
        loader.RegisterFunction(ScalarFunction(NAME, {TGEOM, STBOX},   BOOL, \
            TspatialStboxBoolExec<F_TSPAT_STBOX>)); \
        loader.RegisterFunction(ScalarFunction(NAME, {STBOX, TGEOM},   BOOL, \
            StboxTspatialBoolExec<F_TSPAT_STBOX>)); \
        loader.RegisterFunction(ScalarFunction(NAME, {TGEOM, TGEOM},   BOOL, \
            TspatialTspatialBoolExec<F_TSPAT_TSPAT>)); \
        loader.RegisterFunction(ScalarFunction(NAME, {TGEOM, TSTZSPAN},BOOL, \
            TemporalTstzspanBoolExec<F_TEMP_TSTZSPAN>)); \
        loader.RegisterFunction(ScalarFunction(NAME, {TSTZSPAN, TGEOM},BOOL, \
            TstzspanTemporalBoolExec<F_TSTZSPAN_TEMP>)); \
        loader.RegisterFunction(ScalarFunction(OP,   {TGEOM, STBOX},   BOOL, \
            TspatialStboxBoolExec<F_TSPAT_STBOX>)); \
        loader.RegisterFunction(ScalarFunction(OP,   {STBOX, TGEOM},   BOOL, \
            StboxTspatialBoolExec<F_TSPAT_STBOX>)); \
        loader.RegisterFunction(ScalarFunction(OP,   {TGEOM, TGEOM},   BOOL, \
            TspatialTspatialBoolExec<F_TSPAT_TSPAT>)); \
        loader.RegisterFunction(ScalarFunction(OP,   {TGEOM, TSTZSPAN},BOOL, \
            TemporalTstzspanBoolExec<F_TEMP_TSTZSPAN>)); \
        loader.RegisterFunction(ScalarFunction(OP,   {TSTZSPAN, TGEOM},BOOL, \
            TstzspanTemporalBoolExec<F_TSTZSPAN_TEMP>)); \
    } while (0)

    BOX_REG("temporal_overlaps", "&&",
            overlaps_tspatial_stbox, overlaps_tspatial_tspatial,
            overlaps_temporal_tstzspan, overlaps_tstzspan_temporal);
    BOX_REG("temporal_contains", "@>",
            contains_tspatial_stbox, contains_tspatial_tspatial,
            contains_temporal_tstzspan, contains_tstzspan_temporal);
    BOX_REG("temporal_contained", "<@",
            contained_tspatial_stbox, contained_tspatial_tspatial,
            contained_temporal_tstzspan, contained_tstzspan_temporal);
    BOX_REG("temporal_same", "~=",
            same_tspatial_stbox, same_tspatial_tspatial,
            same_temporal_tstzspan, same_tstzspan_temporal);
    BOX_REG("temporal_adjacent", "-|-",
            adjacent_tspatial_stbox, adjacent_tspatial_tspatial,
            adjacent_temporal_tstzspan, adjacent_tstzspan_temporal);
#undef BOX_REG

    // -----------------------------------------------------------------
    // Position predicates (left / right / below / above / front / back /
    // before / after and their over-* variants). Only tgeometry × stbox
    // and tgeometry × tgeometry — there are no time-axis positions for
    // a tstzspan input that aren't already covered by the time-domain
    // predicates wired in `temporal.cpp` for arbitrary temporals.
    // -----------------------------------------------------------------
#define POS_REG(NAME, F_TSPAT_STBOX, F_TSPAT_TSPAT) \
    do { \
        loader.RegisterFunction(ScalarFunction(NAME, {TGEOM, STBOX}, BOOL, \
            TspatialStboxBoolExec<F_TSPAT_STBOX>)); \
        loader.RegisterFunction(ScalarFunction(NAME, {STBOX, TGEOM}, BOOL, \
            StboxTspatialBoolExec<F_TSPAT_STBOX>)); \
        loader.RegisterFunction(ScalarFunction(NAME, {TGEOM, TGEOM}, BOOL, \
            TspatialTspatialBoolExec<F_TSPAT_TSPAT>)); \
    } while (0)

    POS_REG("temporal_left",       left_tspatial_stbox,       left_tspatial_tspatial);
    POS_REG("temporal_overleft",   overleft_tspatial_stbox,   overleft_tspatial_tspatial);
    POS_REG("temporal_right",      right_tspatial_stbox,      right_tspatial_tspatial);
    POS_REG("temporal_overright",  overright_tspatial_stbox,  overright_tspatial_tspatial);
    POS_REG("temporal_below",      below_tspatial_stbox,      below_tspatial_tspatial);
    POS_REG("temporal_overbelow",  overbelow_tspatial_stbox,  overbelow_tspatial_tspatial);
    POS_REG("temporal_above",      above_tspatial_stbox,      above_tspatial_tspatial);
    POS_REG("temporal_overabove",  overabove_tspatial_stbox,  overabove_tspatial_tspatial);
    POS_REG("temporal_front",      front_tspatial_stbox,      front_tspatial_tspatial);
    POS_REG("temporal_overfront",  overfront_tspatial_stbox,  overfront_tspatial_tspatial);
    POS_REG("temporal_back",       back_tspatial_stbox,       back_tspatial_tspatial);
    POS_REG("temporal_overback",   overback_tspatial_stbox,   overback_tspatial_tspatial);
    POS_REG("temporal_before",     before_tspatial_stbox,     before_tspatial_tspatial);
    POS_REG("temporal_overbefore", overbefore_tspatial_stbox, overbefore_tspatial_tspatial);
    POS_REG("temporal_after",      after_tspatial_stbox,      after_tspatial_tspatial);
    POS_REG("temporal_overafter",  overafter_tspatial_stbox,  overafter_tspatial_tspatial);
#undef POS_REG

    // Time-axis position predicates also accept a tstzspan operand on the
    // non-tspatial side — these reuse the generic temporal_* MEOS exports
    // rather than the tspatial_* ones since they don't touch the spatial
    // axes.
#define TIME_POS_REG(NAME, F_TEMP_TSTZSPAN, F_TSTZSPAN_TEMP) \
    do { \
        loader.RegisterFunction(ScalarFunction(NAME, {TGEOM, TSTZSPAN}, BOOL, \
            TemporalTstzspanBoolExec<F_TEMP_TSTZSPAN>)); \
        loader.RegisterFunction(ScalarFunction(NAME, {TSTZSPAN, TGEOM}, BOOL, \
            TstzspanTemporalBoolExec<F_TSTZSPAN_TEMP>)); \
    } while (0)
    TIME_POS_REG("temporal_before",     before_temporal_tstzspan,     before_tstzspan_temporal);
    TIME_POS_REG("temporal_overbefore", overbefore_temporal_tstzspan, overbefore_tstzspan_temporal);
    TIME_POS_REG("temporal_after",      after_temporal_tstzspan,      after_tstzspan_temporal);
    TIME_POS_REG("temporal_overafter",  overafter_temporal_tstzspan,  overafter_tstzspan_temporal);
#undef TIME_POS_REG

    // -----------------------------------------------------------------
    // Spatial relationships — ever (e*) and always (a*) versions for
    // contains / disjoint / intersects / touches / dwithin.
    // -----------------------------------------------------------------
#define EA_REG_2(NAME, F_TGEO_GEO, F_GEO_TGEO, F_TGEO_TGEO) \
    do { \
        loader.RegisterFunction(ScalarFunction(NAME, {GEOM, TGEOM}, BOOL, \
            GeoTgeoIntExec<F_GEO_TGEO>)); \
        loader.RegisterFunction(ScalarFunction(NAME, {TGEOM, GEOM}, BOOL, \
            TgeoGeoIntExec<F_TGEO_GEO>)); \
        loader.RegisterFunction(ScalarFunction(NAME, {TGEOM, TGEOM}, BOOL, \
            TgeoTgeoIntExec<F_TGEO_TGEO>)); \
    } while (0)
    // disjoint / intersects / touches don't have a (geo, tgeo) MEOS export
    // because they are commutative — eDisjoint(g, t) is just eDisjoint(t, g).
#define EA_REG_2_COMMUT(NAME, F_TGEO_GEO, F_TGEO_TGEO) \
    do { \
        loader.RegisterFunction(ScalarFunction(NAME, {GEOM, TGEOM}, BOOL, \
            TgeoGeoIntExec<F_TGEO_GEO>)); \
        loader.RegisterFunction(ScalarFunction(NAME, {TGEOM, GEOM}, BOOL, \
            TgeoGeoIntExec<F_TGEO_GEO>)); \
        loader.RegisterFunction(ScalarFunction(NAME, {TGEOM, TGEOM}, BOOL, \
            TgeoTgeoIntExec<F_TGEO_TGEO>)); \
    } while (0)

    // contains is non-commutative — uses both directions.
    EA_REG_2("eContains", econtains_tgeo_geo, econtains_geo_tgeo, econtains_tgeo_tgeo);
    EA_REG_2("aContains", acontains_tgeo_geo, acontains_geo_tgeo, acontains_tgeo_tgeo);

    EA_REG_2_COMMUT("eDisjoint",   edisjoint_tgeo_geo,   edisjoint_tgeo_tgeo);
    EA_REG_2_COMMUT("aDisjoint",   adisjoint_tgeo_geo,   adisjoint_tgeo_tgeo);
    EA_REG_2_COMMUT("eIntersects", eintersects_tgeo_geo, eintersects_tgeo_tgeo);
    EA_REG_2_COMMUT("aIntersects", aintersects_tgeo_geo, aintersects_tgeo_tgeo);
    EA_REG_2_COMMUT("eTouches",    etouches_tgeo_geo,    etouches_tgeo_tgeo);
    EA_REG_2_COMMUT("aTouches",    atouches_tgeo_geo,    atouches_tgeo_tgeo);
#undef EA_REG_2
#undef EA_REG_2_COMMUT

    // dwithin is symmetric in its first two arguments; MEOS only exports
    // the (Temporal *, GSERIALIZED *) flavour, so the (geo, tgeo, dist)
    // SQL form reuses it with arguments swapped at the call site.
#define EA_DWITHIN_REG(NAME, F_TGEO_GEO, F_TGEO_TGEO) \
    do { \
        loader.RegisterFunction(ScalarFunction(NAME, {GEOM, TGEOM, DBL}, BOOL, \
            GeoTgeoDistIntExec_FromTgeoGeo<F_TGEO_GEO>)); \
        loader.RegisterFunction(ScalarFunction(NAME, {TGEOM, GEOM, DBL}, BOOL, \
            TgeoGeoDistIntExec<F_TGEO_GEO>)); \
        loader.RegisterFunction(ScalarFunction(NAME, {TGEOM, TGEOM, DBL}, BOOL, \
            TgeoTgeoDistIntExec<F_TGEO_TGEO>)); \
    } while (0)
    EA_DWITHIN_REG("eDwithin", edwithin_tgeo_geo, edwithin_tgeo_tgeo);
    EA_DWITHIN_REG("aDwithin", adwithin_tgeo_geo, adwithin_tgeo_tgeo);
#undef EA_DWITHIN_REG

    // -----------------------------------------------------------------
    // Temporal spatial relationships (`tContains`, `tDisjoint`,
    // `tIntersects`, `tTouches`, `tDwithin`) — return a temporal
    // boolean whose truth value tracks the relation over time.
    // -----------------------------------------------------------------
#define TREL_REG(NAME, F_TGEO_GEO, F_GEO_TGEO, F_TGEO_TGEO) \
    do { \
        loader.RegisterFunction(ScalarFunction(NAME, {GEOM, TGEOM}, TemporalTypes::TBOOL(), \
            GeoTgeoTempExec<F_GEO_TGEO>)); \
        loader.RegisterFunction(ScalarFunction(NAME, {TGEOM, GEOM}, TemporalTypes::TBOOL(), \
            TgeoGeoTempExec<F_TGEO_GEO>)); \
        loader.RegisterFunction(ScalarFunction(NAME, {TGEOM, TGEOM}, TemporalTypes::TBOOL(), \
            TgeoTgeoTempExec<F_TGEO_TGEO>)); \
    } while (0)

    TREL_REG("tContains",   tcontains_tgeo_geo,   tcontains_geo_tgeo,   tcontains_tgeo_tgeo);
    TREL_REG("tDisjoint",   tdisjoint_tgeo_geo,   tdisjoint_geo_tgeo,   tdisjoint_tgeo_tgeo);
    TREL_REG("tIntersects", tintersects_tgeo_geo, tintersects_geo_tgeo, tintersects_tgeo_tgeo);
    TREL_REG("tTouches",    ttouches_tgeo_geo,    ttouches_geo_tgeo,    ttouches_tgeo_tgeo);
#undef TREL_REG

    // tDwithin takes the extra distance argument.
    loader.RegisterFunction(ScalarFunction("tDwithin",
        {GEOM, TGEOM, DBL}, TemporalTypes::TBOOL(),
        GeoTgeoDistTempExec<tdwithin_geo_tgeo>));
    loader.RegisterFunction(ScalarFunction("tDwithin",
        {TGEOM, GEOM, DBL}, TemporalTypes::TBOOL(),
        TgeoGeoDistTempExec<tdwithin_tgeo_geo>));
    loader.RegisterFunction(ScalarFunction("tDwithin",
        {TGEOM, TGEOM, DBL}, TemporalTypes::TBOOL(),
        TgeoTgeoDistTempExec<tdwithin_tgeo_tgeo>));

    // -----------------------------------------------------------------
    // Distance — `tdistance(...)` and `<->`.
    // -----------------------------------------------------------------
    loader.RegisterFunction(ScalarFunction("tdistance",
        {TGEOM, GEOM}, TFLOAT, TgeoGeoDistanceExec<tdistance_tgeo_geo>));
    loader.RegisterFunction(ScalarFunction("tdistance",
        {TGEOM, TGEOM}, TFLOAT, TgeoTgeoDistanceExec<tdistance_tgeo_tgeo>));
    loader.RegisterFunction(ScalarFunction("<->",
        {TGEOM, GEOM}, TFLOAT, TgeoGeoDistanceExec<tdistance_tgeo_geo>));
    loader.RegisterFunction(ScalarFunction("<->",
        {TGEOM, TGEOM}, TFLOAT, TgeoTgeoDistanceExec<tdistance_tgeo_tgeo>));
}

} // namespace duckdb
