#include "meos_wrapper_simple.hpp"

#include "common.hpp"
#include "temporal/temporal.hpp"
#include "temporal/temporal_functions.hpp"
#include "temporal/spanset.hpp"
#include "temporal/tbox.hpp"
#include "temporal/set.hpp"
#include "temporal/span.hpp"
#include "geo/stbox.hpp"
#include "geo/tgeompoint.hpp"
#include "geo/tgeometry.hpp"

#include "duckdb/common/types/blob.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/common/types/data_chunk.hpp"

#include "time_util.hpp"
#include "mobilityduck/meos_exec_serial.hpp"

namespace duckdb {

#define DEFINE_TEMPORAL_TYPE(NAME) \
    LogicalType TemporalTypes::NAME() { \
        LogicalType type(LogicalTypeId::BLOB); \
        type.SetAlias(#NAME); \
        return type; \
    }

DEFINE_TEMPORAL_TYPE(tint)
DEFINE_TEMPORAL_TYPE(tbigint)
DEFINE_TEMPORAL_TYPE(tbool)
DEFINE_TEMPORAL_TYPE(tfloat)
DEFINE_TEMPORAL_TYPE(ttext)

#undef DEFINE_TEMPORAL_TYPE

void TemporalTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType( "tint", tint());
    loader.RegisterType( "tbigint", tbigint());
    loader.RegisterType( "tbool", tbool());
    loader.RegisterType( "tfloat", tfloat());
    loader.RegisterType( "ttext", ttext());
}

const std::vector<LogicalType> &TemporalTypes::AllTypes() {
    static std::vector<LogicalType> types = {
        tint(),
        tbigint(),
        tbool(),
        tfloat(),
        ttext()
    };
    return types;
}

LogicalType TemporalTypes::GetBaseTypeFromAlias(const char *alias) {
    for (size_t i = 0; i < sizeof(BASE_TYPES) / sizeof(BASE_TYPES[0]); i++) {
        if (strcmp(alias, BASE_TYPES[i].alias) == 0) {
            return BASE_TYPES[i].basetype;
        }
    }
    throw InternalException("Invalid temporal type alias: %s", alias);
}

void TemporalTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    for (auto &type : TemporalTypes::AllTypes()) {
        RegisterMeosCastFunction(loader, 
            LogicalType::VARCHAR,
            type,
            TemporalFunctions::Temporal_in
        );

        RegisterMeosCastFunction(loader, 
            type,
            LogicalType::VARCHAR,
            TemporalFunctions::Temporal_out
        );

    //     ExtensionUtil::RegisterCastFunction(
    //         instance,
    //         type,
    //         type,
    //         TemporalFunctions::Temporal_enforce_typmod_cast,
    //         100
    //     );
    // }

    RegisterMeosCastFunction(loader, 
        LogicalType::BLOB,
        SpansetTypes::tstzspanset(),
        TemporalFunctions::Blob_to_tstzspanset
    );

    RegisterMeosCastFunction(loader, 
        TemporalTypes::tbool(),
        TemporalTypes::tint(),
        TemporalFunctions::Tbool_to_tint_cast
    );

    RegisterMeosCastFunction(loader, 
        TemporalTypes::tint(),
        TemporalTypes::tfloat(),
        TemporalFunctions::Tint_to_tfloat_cast
    );

    RegisterMeosCastFunction(loader, 
        TemporalTypes::tfloat(),
        TemporalTypes::tint(),
        TemporalFunctions::Tfloat_to_tint_cast
    );

    RegisterMeosCastFunction(loader, 
        TemporalTypes::tint(),
        TboxType::tbox(),
        TemporalFunctions::Tnumber_to_tbox_cast
    );

    RegisterMeosCastFunction(loader, 
        TemporalTypes::tfloat(),
        TboxType::tbox(),
        TemporalFunctions::Tnumber_to_tbox_cast
    );

}
}

void TemporalTypes::RegisterScalarFunctions(ExtensionLoader &loader) {
    for (auto &type : TemporalTypes::AllTypes()) {
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), LogicalType::TIMESTAMP_TZ},
                type,
                TemporalFunctions::Tinstant_constructor
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {type, LogicalType::INTEGER},
                type,
                TemporalFunctions::Temporal_enforce_typmod
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), SetTypes::tstzset()},
                type,
                TemporalFunctions::Tsequence_from_base_tstzset
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), SpanTypes::tstzspan()},
                type,
                TemporalFunctions::Tsequence_from_base_tstzspan
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), SpanTypes::tstzspan(), LogicalType::VARCHAR},
                type,
                TemporalFunctions::Tsequence_from_base_tstzspan
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), SpansetTypes::tstzspanset()},
                type,
                TemporalFunctions::Tsequenceset_from_base_tstzspanset
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), SpansetTypes::tstzspanset(), LogicalType::VARCHAR},
                type,
                TemporalFunctions::Tsequenceset_from_base_tstzspanset
            )
        );







        // timeSpan/valueSpan (temporal_to_tstzspan / tnumber_to_span, group
        // meos_temporal_conversion) are generated from the catalog sqlSignatures
        // in src/generated/generated_temporal_udfs.cpp (RETIRED_GROUPS).

        // timestamps(<temporal_type>) is generated from the catalog (temporal_timestamps) in generated_temporal_udfs.cpp.

        if (type.GetAlias() == "tint" || type.GetAlias() == "tfloat") {
            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction(
                    "shiftValue",
                    {type, LogicalType::BIGINT},
                    type,
                    TemporalFunctions::Tnumber_shift_value
                )
            );

            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction(
                    "scaleValue",
                    {type, LogicalType::BIGINT},
                    type,
                    TemporalFunctions::Tnumber_scale_value
                )
            );

            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction(
                    "shiftScaleValue",
                    {type, LogicalType::BIGINT, LogicalType::BIGINT},
                    type,
                    TemporalFunctions::Tnumber_shift_scale_value
                )
            );

        }
        if (type.GetAlias() != "tbool") {
            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction(
                    "tempDump",
                    {type},
                    LogicalType::LIST(
                        LogicalType::STRUCT(
                            {{"value", TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str())},
                            {"time", SpansetTypes::tstzspanset()}}
                        )
                    ),
                    TemporalFunctions::Temporal_dump
                )
            );
        }
        

        



        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "=",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_eq
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "<>",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_ne
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "<=",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_le
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "<",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_lt
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                ">=",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_ge
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                ">",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_gt
            )
        );

    }

    // The temporal accessors (getValue/startValue/endValue/minValue/maxValue, getValues,
    // valueSet, getTime, instants, segmentMinDuration...; group meos_temporal_accessor) are
    // generated from the catalog sqlSignatures in src/generated/generated_temporal_udfs.cpp
    // (RETIRED_GROUPS).

    // atValues / minusValues (Temporal<T>, set-of-T) are now generated from the catalog
    // (temporal_at_values / temporal_minus_values, sqlSignatures-driven) — see
    // src/generated/generated_temporal_udfs.cpp. Behaviour-equivalent to the retired hand
    // regs (both call the MEOS set restriction), and the generated surface adds the
    // previously-missing tbigint overload.
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "whenTrue",
            {TemporalTypes::tbool()},
            SpansetTypes::tstzspanset(),
            TemporalFunctions::Tbool_when_true
        )
    );

    // atTbox / minusTbox (TNumber<T>, tbox) are now generated from the catalog
    // (tnumber_at_tbox / tnumber_minus_tbox, sqlSignatures-driven) — see
    // src/generated/generated_temporal_udfs.cpp. Behaviour-equivalent to the retired hand
    // regs (both call the MEOS box restriction), and the generated surface adds the
    // previously-missing tbigint overload.

    // round(tfloat) and round(tfloat, integer) are float-base scalar transforms
    // generated from the catalog (generated_temporal_udfs.cpp): the shorter
    // DEFAULT-arg overload comes from sqlSignatures argDefaults.

    // The temporal-number casts (tint/tfloat) and tbox() conversion function forms
    // (tbool_to_tint / tint_to_tfloat / tfloat_to_tint / tnumber_to_tbox, group
    // meos_temporal_conversion) are generated from the catalog sqlSignatures in
    // src/generated/generated_temporal_udfs.cpp (RETIRED_GROUPS) — incl the tbigint
    // overloads the hand layer was missing.

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "avgValue",
            {TemporalTypes::tint()},
            LogicalType::DOUBLE,
            TemporalFunctions::Tnumber_avg_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "avgValue",
            {TemporalTypes::tfloat()},
            LogicalType::DOUBLE,
            TemporalFunctions::Tnumber_avg_value
        )
    );

    // tbool boolean operators
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&", {TemporalTypes::tbool(), LogicalType::BOOLEAN}, TemporalTypes::tbool(), TemporalFunctions::Tand_tbool_bool));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&", {LogicalType::BOOLEAN, TemporalTypes::tbool()}, TemporalTypes::tbool(), TemporalFunctions::Tand_bool_tbool));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&", {TemporalTypes::tbool(), TemporalTypes::tbool()}, TemporalTypes::tbool(), TemporalFunctions::Tand_tbool_tbool));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("|", {TemporalTypes::tbool(), LogicalType::BOOLEAN}, TemporalTypes::tbool(), TemporalFunctions::Tor_tbool_bool));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("|", {LogicalType::BOOLEAN, TemporalTypes::tbool()}, TemporalTypes::tbool(), TemporalFunctions::Tor_bool_tbool));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("|", {TemporalTypes::tbool(), TemporalTypes::tbool()}, TemporalTypes::tbool(), TemporalFunctions::Tor_tbool_tbool));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("~", {TemporalTypes::tbool()}, TemporalTypes::tbool(), TemporalFunctions::Tnot_tbool));
    // Portable-SQL aliases (MobilityDB names): tbool_and / tbool_or / tbool_not

    // tnumber arithmetic operators
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("+", {LogicalType::INTEGER, TemporalTypes::tint()}, TemporalTypes::tint(), TemporalFunctions::Add_int_tint));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("+", {TemporalTypes::tint(), LogicalType::INTEGER}, TemporalTypes::tint(), TemporalFunctions::Add_tint_int));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("+", {LogicalType::DOUBLE, TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Add_float_tfloat));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("+", {TemporalTypes::tfloat(), LogicalType::DOUBLE}, TemporalTypes::tfloat(), TemporalFunctions::Add_tfloat_float));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("+", {TemporalTypes::tint(), TemporalTypes::tint()}, TemporalTypes::tint(), TemporalFunctions::Add_tnumber_tnumber));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("+", {TemporalTypes::tfloat(), TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Add_tnumber_tnumber));

    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-", {LogicalType::INTEGER, TemporalTypes::tint()}, TemporalTypes::tint(), TemporalFunctions::Sub_int_tint));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-", {TemporalTypes::tint(), LogicalType::INTEGER}, TemporalTypes::tint(), TemporalFunctions::Sub_tint_int));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-", {LogicalType::DOUBLE, TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Sub_float_tfloat));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-", {TemporalTypes::tfloat(), LogicalType::DOUBLE}, TemporalTypes::tfloat(), TemporalFunctions::Sub_tfloat_float));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-", {TemporalTypes::tint(), TemporalTypes::tint()}, TemporalTypes::tint(), TemporalFunctions::Sub_tnumber_tnumber));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-", {TemporalTypes::tfloat(), TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Sub_tnumber_tnumber));

    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("*", {LogicalType::INTEGER, TemporalTypes::tint()}, TemporalTypes::tint(), TemporalFunctions::Mult_int_tint));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("*", {TemporalTypes::tint(), LogicalType::INTEGER}, TemporalTypes::tint(), TemporalFunctions::Mult_tint_int));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("*", {LogicalType::DOUBLE, TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Mult_float_tfloat));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("*", {TemporalTypes::tfloat(), LogicalType::DOUBLE}, TemporalTypes::tfloat(), TemporalFunctions::Mult_tfloat_float));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("*", {TemporalTypes::tint(), TemporalTypes::tint()}, TemporalTypes::tint(), TemporalFunctions::Mult_tnumber_tnumber));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("*", {TemporalTypes::tfloat(), TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Mult_tnumber_tnumber));

    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("/", {LogicalType::INTEGER, TemporalTypes::tint()}, TemporalTypes::tint(), TemporalFunctions::Div_int_tint));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("/", {TemporalTypes::tint(), LogicalType::INTEGER}, TemporalTypes::tint(), TemporalFunctions::Div_tint_int));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("/", {LogicalType::DOUBLE, TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Div_float_tfloat));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("/", {TemporalTypes::tfloat(), LogicalType::DOUBLE}, TemporalTypes::tfloat(), TemporalFunctions::Div_tfloat_float));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("/", {TemporalTypes::tint(), TemporalTypes::tint()}, TemporalTypes::tint(), TemporalFunctions::Div_tnumber_tnumber));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("/", {TemporalTypes::tfloat(), TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Div_tnumber_tnumber));

    // Unary tfloat transforms floor/ceil/round/degrees/radians are float-base scalar
    // transforms generated from the catalog (generated_temporal_udfs.cpp), full arity
    // plus the shorter DEFAULT-arg overload via sqlSignatures argDefaults.
    // The meos_temporal_math group (abs/derivative/exp/ln/log10/deltaValue/trend) is
    // supplied by the generated surface (RETIRED_GROUPS in the codegen).

    // Named-function aliases for the arithmetic operators (MobilityDB exposes
    // both `+`/`-`/`*`/`/` and `tnumber_add`/`sub`/`mult`/`div`).




    // tnumber distance and nearest-approach-distance.
    //
    // Value-distance variants `<-> ` for (tint, INTEGER), (INTEGER, tint),
    // (tfloat, DOUBLE), (DOUBLE, tfloat) are intentionally NOT registered
    // here: in the installed MEOS library, tdistance_tfloat_float / tint_int
    // return the temporal's own value at each instant rather than the
    // |t.value - v| absolute difference. Verified by smoke test:
    //   SELECT 5.0::DOUBLE <-> tfloat '5.0@2000-01-01';   -- returns 5.0, expected 0.0
    //   SELECT 100.0::DOUBLE <-> tfloat '2.5@2000-01-01'; -- returns 2.5, expected 97.5
    // The temporal-temporal variant DOES work correctly, and so does nad_*.
    // Restore the value-distance registrations once the MEOS issue is resolved.
    // The (tint,tint) temporal distance is owned by the generated surface, which
    // returns a tfloat (the temporal distance is always a tfloat — the hand line
    // wrongly typed the result BLOB as tint). The tfloat,tfloat variant matches the
    // generated return and is kept.
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<->", {TemporalTypes::tfloat(), TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Tdistance_tnumber_tnumber));
    // Named form of the same function for SQL portability.
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tdistance", {TemporalTypes::tfloat(), TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Tdistance_tnumber_tnumber));

    // nearestApproachDistance / nad — scalar return

    // Temporal topological predicates (contains/contained/overlaps/same/adjacent +
    // @>/<@/&&/~=/-|-) for temporal × temporal (same base family) and temporal ×
    // tstzspan (both directions) are generated from the catalog sqlSignatures in
    // src/generated/generated_temporal_udfs.cpp (@ingroup meos_temporal_bbox_topo).
    // The non-canonical temporal_* snake aliases and the spurious mixed-family
    // cross-product overloads are dropped (bare same-family names supersede them).

    // Temporal time-position predicates (before/after/overbefore/overafter for
    // temporal×temporal, temporal×tstzspan both directions, and tgeompoint;
    // @ingroup meos_temporal_bbox_pos) are generated from the catalog sqlSignatures
    // in src/generated/generated_temporal_udfs.cpp. DuckDB's parser rejects `#` in
    // operator names, so only the named forms are reachable — the generator emits
    // exactly those, same-base-family per the catalog (no spurious mixed pairs).
    // The stale-snake temporal_* aliases are dropped (bare names supersede them).

    // Ever / always equality and inequality (named functions; DuckDB
    // parser does not accept ?= / #= operator names).
    // Traditional/ever/always temporal comparisons (meos_temporal_comp_ever) are
    // supplied by the generated surface (RETIRED_GROUPS). The spatial (geometry-arg)
    // ever/always overloads remain hand-registered in the geo module.

    // Similarity: frechetDistance/dynTimeWarpDistance/hausdorffDistance (scalar) and
    // frechetDistancePath/dynTimeWarpPath (SETOF -> DuckDB table function) are all GENERATED
    // from the catalog now. The non-canonical discreteFrechet/dynTimeWarp hand aliases were
    // retired (MobilityDB canonical = frechetDistance/dynTimeWarpDistance).

    // tnumber × {numspan, tbox} topological predicates (contains/contained/overlaps/
    // same/adjacent + @>/<@/&&/~=/-|-, both directions) are generated from the catalog
    // sqlSignatures (@ingroup meos_temporal_bbox_topo); the temporal_* snake aliases
    // are dropped. The numeric-axis positional predicates are likewise generated.

    // Temporal #= comparison (temporal_teq/tne/tlt/tle/tgt/tge) is generated
    // (group meos_temporal_comp_temp).
    // tsample — time-domain rebinning (tprecision is generated from the catalog)
    for (auto &t : TemporalTypes::AllTypes()) {
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tsample",    {t, LogicalType::INTERVAL}, t, TemporalFunctions::Temporal_tsample));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tsample",    {t, LogicalType::INTERVAL, LogicalType::TIMESTAMP_TZ}, t, TemporalFunctions::Temporal_tsample));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tsample",    {t, LogicalType::INTERVAL, LogicalType::TIMESTAMP_TZ, LogicalType::VARCHAR}, t, TemporalFunctions::Temporal_tsample));
    }

    // tspatial × {stbox, tspatial} position predicates (spatial axes
    // left/right/below/above/front/back + over*, and the time axis before/after/
    // overbefore/overafter; @ingroup meos_geo_box_pos / meos_geo_bbox_pos) — bare
    // names AND the <<, >>, &<, &> operator forms — are generated from the catalog
    // sqlSignatures in src/generated/generated_temporal_udfs.cpp. The stale-snake
    // temporal_* aliases are dropped (bare names supersede them).

    // ttext text functions
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("||", {LogicalType::VARCHAR, TemporalTypes::ttext()}, TemporalTypes::ttext(), TemporalFunctions::Textcat_text_ttext));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("||", {TemporalTypes::ttext(), LogicalType::VARCHAR}, TemporalTypes::ttext(), TemporalFunctions::Textcat_ttext_text));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("||", {TemporalTypes::ttext(), TemporalTypes::ttext()}, TemporalTypes::ttext(), TemporalFunctions::Textcat_ttext_ttext));
    // Portable-SQL alias (MobilityDB name): ttext_cat
}


// ─── portable WKB I/O for scalar temporal types ──────────────────────────────
// Uses temporal_as_wkb / temporal_from_wkb (type-agnostic MEOS functions) to
// produce the same MEOS-WKB bytes that tgeompoint's asBinary/tgeompointFromBinary
// already use.  Adding these overloads gives tint / tfloat / tbool / ttext the
// same Parquet-round-trip story as spatial temporals.

namespace {

inline Temporal *ScalarBlobToTemp(const string_t &b) {
    size_t sz = b.GetSize();
    uint8_t *copy = (uint8_t *)malloc(sz);
    if (!copy) throw InternalException("asBinary: malloc failed");
    memcpy(copy, b.GetData(), sz);
    return reinterpret_cast<Temporal *>(copy);
}

void TemporalScalarAsWkbExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            Temporal *t = ScalarBlobToTemp(input);
            size_t sz = 0;
            uint8_t *wkb = temporal_as_wkb(t, WKB_EXTENDED, &sz);
            free(t);
            if (!wkb || sz == 0) {
                if (wkb) free(wkb);
                throw InternalException("temporal_as_wkb returned null");
            }
            string_t stored = StringVector::AddStringOrBlob(
                result, string_t(reinterpret_cast<const char *>(wkb), sz));
            free(wkb);
            return stored;
        });
}

void TemporalScalarFromWkbExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            if (input.GetSize() == 0)
                throw InvalidInputException("fromBinary: empty WKB input");
            uint8_t *wkb = (uint8_t *)malloc(input.GetSize());
            if (!wkb) throw InternalException("fromBinary: malloc failed");
            memcpy(wkb, input.GetData(), input.GetSize());
            Temporal *t = temporal_from_wkb(wkb, input.GetSize());
            free(wkb);
            if (!t) throw InvalidInputException("fromBinary: invalid MEOS-WKB");
            size_t sz = temporal_mem_size(t);
            string_t stored = StringVector::AddStringOrBlob(
                result, string_t(reinterpret_cast<const char *>(t), sz));
            free(t);
            return stored;
        });
}

} // anonymous namespace

void TemporalTypes::RegisterWkbFunctions(ExtensionLoader &loader) {
    const auto B = LogicalType::BLOB;
    struct Entry {
        LogicalType type;
        const char *bin_name;
    };
    const Entry types[] = {
        { tint(),   "tintFromBinary" },
        { tfloat(), "tfloatFromBinary" },
        { tbool(),  "tboolFromBinary" },
        { ttext(),  "ttextFromBinary" },
    };
    for (auto &e : types) {
        RegisterMeosFunction(loader, 
            ScalarFunction("asBinary", {e.type}, B, TemporalScalarAsWkbExec));
        duckdb::RegisterSerializedScalarFunction(
            loader,
            ScalarFunction(e.bin_name, {B}, e.type, TemporalScalarFromWkbExec));
    }
}

namespace {

inline string_t MallocBlobToResultLocal(Vector &result, void *buf, size_t sz) {
    string_t blob(reinterpret_cast<const char *>(buf), UnsafeNumericCast<uint32_t>(sz));
    string_t stored = StringVector::AddStringOrBlob(result, blob);
    free(buf);
    return stored;
}

// ----- getBin: single-bin getters returning spans -----
//
// MobilityDB SQL: getBin(value, size, origin) -> <type>span. We compute the
// lower bound via MEOS *_get_bin and pair it with upper = lower + size to
// emit a [lower, upper) span blob. Time/date variants stride the duration
// interval rather than a numeric size.

inline string_t SpanToBlob(Vector &result, Span *span) {
    string_t out = StringVector::AddStringOrBlob(
        result, reinterpret_cast<const char *>(span), sizeof(Span));
    free(span);
    return out;
}

// ----- tbox tile emitters: LIST(tbox) outputs -----

inline void EmitTboxList(Vector &result, idx_t row_idx, TBox *tiles, int count,
                         idx_t &total_offset, list_entry_t *list_entries,
                         Vector &child_vector, ValidityMask &result_validity) {
    if (!tiles || count <= 0) {
        if (tiles) free(tiles);
        result_validity.SetInvalid(row_idx);
        return;
    }
    ListVector::SetListSize(result, total_offset + count);
    list_entries[row_idx] = list_entry_t{total_offset, static_cast<uint64_t>(count)};
    auto *child_data = FlatVector::GetData<string_t>(child_vector);
    const size_t tbox_bytes = sizeof(TBox);
    for (int j = 0; j < count; ++j) {
        child_data[total_offset + j] = StringVector::AddStringOrBlob(
            child_vector, reinterpret_cast<const char *>(&tiles[j]), tbox_bytes);
    }
    free(tiles);
    total_offset += count;
}

// valueTiles(tbox, vsize [, vorigin [, borderInc]]) — int branch uses int xsize/xorigin, float uses
// double
void TboxValueTilesExec(DataChunk &args, ExpressionState &, Vector &result) {
    auto &tbox_vec = args.data[0];
    auto &vsize_vec = args.data[1];
    Vector *vorigin_vec = args.ColumnCount() >= 3 ? &args.data[2] : nullptr;
    Vector *border_vec = args.ColumnCount() >= 4 ? &args.data[3] : nullptr;
    idx_t row_count = args.size();
    if (border_vec) border_vec->Flatten(row_count);
    tbox_vec.Flatten(row_count);
    vsize_vec.Flatten(row_count);
    if (vorigin_vec) vorigin_vec->Flatten(row_count);

    auto &result_validity = FlatVector::Validity(result);
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &child_vector = ListVector::GetEntry(result);
    child_vector.SetVectorType(VectorType::FLAT_VECTOR);
    ListVector::Reserve(result, row_count);
    idx_t total_offset = 0;

    auto tbox_data = FlatVector::GetData<string_t>(tbox_vec);
    for (idx_t i = 0; i < row_count; ++i) {
        if (FlatVector::IsNull(tbox_vec, i) || FlatVector::IsNull(vsize_vec, i) ||
            (vorigin_vec && FlatVector::IsNull(*vorigin_vec, i)) ||
            (border_vec && FlatVector::IsNull(*border_vec, i))) {
            result_validity.SetInvalid(i);
            continue;
        }
        string_t blob = tbox_data[i];
        TBox *box = (TBox *) malloc(blob.GetSize());
        memcpy(box, blob.GetData(), blob.GetSize());

        int count = 0;
        TBox *tiles = nullptr;
        if (box->span.spantype == T_INTSPAN) {
            int32_t vsize = FlatVector::GetData<int32_t>(vsize_vec)[i];
            int32_t vorigin = vorigin_vec ? FlatVector::GetData<int32_t>(*vorigin_vec)[i] : 0;
            tiles = tintbox_value_tiles(box, vsize, vorigin, border_vec ? FlatVector::GetData<bool>(*border_vec)[i] : true, &count);
        } else if (box->span.spantype == T_FLOATSPAN) {
            double vsize = FlatVector::GetData<double>(vsize_vec)[i];
            double vorigin = vorigin_vec ? FlatVector::GetData<double>(*vorigin_vec)[i] : 0.0;
            tiles = tfloatbox_value_tiles(box, vsize, vorigin, border_vec ? FlatVector::GetData<bool>(*border_vec)[i] : true, &count);
        } else {
            free(box);
            throw InvalidInputException("valueTiles: tbox has no value dimension");
        }
        free(box);
        EmitTboxList(result, i, tiles, count, total_offset, list_entries, child_vector,
                     result_validity);
    }
}

// timeTiles(tbox, duration [, torigin [, borderInc]])
void TboxTimeTilesExec(DataChunk &args, ExpressionState &, Vector &result) {
    auto &tbox_vec = args.data[0];
    auto &dur_vec = args.data[1];
    Vector *torigin_vec = args.ColumnCount() >= 3 ? &args.data[2] : nullptr;
    Vector *border_vec = args.ColumnCount() >= 4 ? &args.data[3] : nullptr;
    idx_t row_count = args.size();
    if (border_vec) border_vec->Flatten(row_count);
    tbox_vec.Flatten(row_count);
    dur_vec.Flatten(row_count);
    if (torigin_vec) torigin_vec->Flatten(row_count);

    auto &result_validity = FlatVector::Validity(result);
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &child_vector = ListVector::GetEntry(result);
    child_vector.SetVectorType(VectorType::FLAT_VECTOR);
    ListVector::Reserve(result, row_count);
    idx_t total_offset = 0;

    auto tbox_data = FlatVector::GetData<string_t>(tbox_vec);
    auto dur_data = FlatVector::GetData<interval_t>(dur_vec);
    for (idx_t i = 0; i < row_count; ++i) {
        if (FlatVector::IsNull(tbox_vec, i) || FlatVector::IsNull(dur_vec, i) ||
            (torigin_vec && FlatVector::IsNull(*torigin_vec, i)) ||
            (border_vec && FlatVector::IsNull(*border_vec, i))) {
            result_validity.SetInvalid(i);
            continue;
        }
        string_t blob = tbox_data[i];
        TBox *box = (TBox *) malloc(blob.GetSize());
        memcpy(box, blob.GetData(), blob.GetSize());

        MeosInterval iv = IntervaltToInterval(dur_data[i]);
        timestamp_tz_t torigin_meos;
        torigin_meos.value = DEFAULT_TIME_ORIGIN_MEOS;
        if (torigin_vec) {
            timestamp_tz_t torigin_in = FlatVector::GetData<timestamp_tz_t>(*torigin_vec)[i];
            torigin_meos = DuckDBToMeosTimestamp(torigin_in);
        }

        int count = 0;
        TBox *tiles = nullptr;
        if (box->span.spantype == T_INTSPAN) {
            tiles = tintbox_time_tiles(box, &iv, (TimestampTz) torigin_meos.value, border_vec ? FlatVector::GetData<bool>(*border_vec)[i] : true, &count);
        } else if (box->span.spantype == T_FLOATSPAN) {
            tiles = tfloatbox_time_tiles(box, &iv, (TimestampTz) torigin_meos.value, border_vec ? FlatVector::GetData<bool>(*border_vec)[i] : true, &count);
        } else {
            free(box);
            throw InvalidInputException("timeTiles: tbox has no value dimension to dispatch on");
        }
        free(box);
        EmitTboxList(result, i, tiles, count, total_offset, list_entries, child_vector,
                     result_validity);
    }
}

// valueTimeTiles(tbox, vsize, duration [, vorigin, torigin [, borderInc]])
void TboxValueTimeTilesExec(DataChunk &args, ExpressionState &, Vector &result) {
    auto &tbox_vec = args.data[0];
    auto &vsize_vec = args.data[1];
    auto &dur_vec = args.data[2];
    Vector *vorigin_vec = args.ColumnCount() >= 4 ? &args.data[3] : nullptr;
    Vector *torigin_vec = args.ColumnCount() >= 5 ? &args.data[4] : nullptr;
    Vector *border_vec = args.ColumnCount() >= 6 ? &args.data[5] : nullptr;
    idx_t row_count = args.size();
    if (border_vec) border_vec->Flatten(row_count);
    tbox_vec.Flatten(row_count);
    vsize_vec.Flatten(row_count);
    dur_vec.Flatten(row_count);
    if (vorigin_vec) vorigin_vec->Flatten(row_count);
    if (torigin_vec) torigin_vec->Flatten(row_count);

    auto &result_validity = FlatVector::Validity(result);
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &child_vector = ListVector::GetEntry(result);
    child_vector.SetVectorType(VectorType::FLAT_VECTOR);
    ListVector::Reserve(result, row_count);
    idx_t total_offset = 0;

    auto tbox_data = FlatVector::GetData<string_t>(tbox_vec);
    auto dur_data = FlatVector::GetData<interval_t>(dur_vec);
    for (idx_t i = 0; i < row_count; ++i) {
        if (FlatVector::IsNull(tbox_vec, i) || FlatVector::IsNull(vsize_vec, i) ||
            FlatVector::IsNull(dur_vec, i) ||
            (vorigin_vec && FlatVector::IsNull(*vorigin_vec, i)) ||
            (torigin_vec && FlatVector::IsNull(*torigin_vec, i)) ||
            (border_vec && FlatVector::IsNull(*border_vec, i))) {
            result_validity.SetInvalid(i);
            continue;
        }
        string_t blob = tbox_data[i];
        TBox *box = (TBox *) malloc(blob.GetSize());
        memcpy(box, blob.GetData(), blob.GetSize());

        MeosInterval iv = IntervaltToInterval(dur_data[i]);
        timestamp_tz_t torigin_meos;
        torigin_meos.value = DEFAULT_TIME_ORIGIN_MEOS;
        if (torigin_vec) {
            timestamp_tz_t torigin_in = FlatVector::GetData<timestamp_tz_t>(*torigin_vec)[i];
            torigin_meos = DuckDBToMeosTimestamp(torigin_in);
        }

        int count = 0;
        TBox *tiles = nullptr;
        if (box->span.spantype == T_INTSPAN) {
            int32_t vsize = FlatVector::GetData<int32_t>(vsize_vec)[i];
            int32_t vorigin = vorigin_vec ? FlatVector::GetData<int32_t>(*vorigin_vec)[i] : 0;
            tiles = tintbox_value_time_tiles(box, vsize, &iv, vorigin,
                                             (TimestampTz) torigin_meos.value, border_vec ? FlatVector::GetData<bool>(*border_vec)[i] : true, &count);
        } else if (box->span.spantype == T_FLOATSPAN) {
            double vsize = FlatVector::GetData<double>(vsize_vec)[i];
            double vorigin = vorigin_vec ? FlatVector::GetData<double>(*vorigin_vec)[i] : 0.0;
            tiles = tfloatbox_value_time_tiles(box, vsize, &iv, vorigin,
                                               (TimestampTz) torigin_meos.value, border_vec ? FlatVector::GetData<bool>(*border_vec)[i] : true, &count);
        } else {
            free(box);
            throw InvalidInputException("valueTimeTiles: tbox has no value dimension");
        }
        free(box);
        EmitTboxList(result, i, tiles, count, total_offset, list_entries, child_vector,
                     result_validity);
    }
}

} // namespace

void TemporalTypes::RegisterTileGetters(ExtensionLoader &loader) {
    // Single-bin getters — getBin(value, size, origin) -> <type>span

    LogicalType list_tbox = LogicalType::LIST(TboxType::tbox());

    // valueTiles(tbox, vsize [, vorigin]) — both INTEGER and DOUBLE size variants
    RegisterMeosFunction(loader, ScalarFunction(
        "valueTiles", {TboxType::tbox(), LogicalType::INTEGER},
        list_tbox, TboxValueTilesExec));
    RegisterMeosFunction(loader, ScalarFunction(
        "valueTiles", {TboxType::tbox(), LogicalType::INTEGER, LogicalType::INTEGER},
        list_tbox, TboxValueTilesExec));
    RegisterMeosFunction(loader, ScalarFunction(
        "valueTiles", {TboxType::tbox(), LogicalType::DOUBLE},
        list_tbox, TboxValueTilesExec));
    RegisterMeosFunction(loader, ScalarFunction(
        "valueTiles", {TboxType::tbox(), LogicalType::DOUBLE, LogicalType::DOUBLE},
        list_tbox, TboxValueTilesExec));
    RegisterMeosFunction(loader, ScalarFunction(
        "valueTiles", {TboxType::tbox(), LogicalType::INTEGER, LogicalType::INTEGER, LogicalType::BOOLEAN},
        list_tbox, TboxValueTilesExec));
    RegisterMeosFunction(loader, ScalarFunction(
        "valueTiles", {TboxType::tbox(), LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::BOOLEAN},
        list_tbox, TboxValueTilesExec));

    // timeTiles(tbox, duration [, torigin])
    RegisterMeosFunction(loader, ScalarFunction(
        "timeTiles", {TboxType::tbox(), LogicalType::INTERVAL},
        list_tbox, TboxTimeTilesExec));
    RegisterMeosFunction(loader, ScalarFunction(
        "timeTiles", {TboxType::tbox(), LogicalType::INTERVAL, LogicalType::TIMESTAMP_TZ},
        list_tbox, TboxTimeTilesExec));
    RegisterMeosFunction(loader, ScalarFunction(
        "timeTiles",
        {TboxType::tbox(), LogicalType::INTERVAL, LogicalType::TIMESTAMP_TZ, LogicalType::BOOLEAN},
        list_tbox, TboxTimeTilesExec));

    // valueTimeTiles(tbox, vsize, duration [, vorigin, torigin])
    RegisterMeosFunction(loader, ScalarFunction(
        "valueTimeTiles", {TboxType::tbox(), LogicalType::INTEGER, LogicalType::INTERVAL},
        list_tbox, TboxValueTimeTilesExec));
    RegisterMeosFunction(loader, ScalarFunction(
        "valueTimeTiles",
        {TboxType::tbox(), LogicalType::INTEGER, LogicalType::INTERVAL,
         LogicalType::INTEGER, LogicalType::TIMESTAMP_TZ},
        list_tbox, TboxValueTimeTilesExec));
    RegisterMeosFunction(loader, ScalarFunction(
        "valueTimeTiles", {TboxType::tbox(), LogicalType::DOUBLE, LogicalType::INTERVAL},
        list_tbox, TboxValueTimeTilesExec));
    RegisterMeosFunction(loader, ScalarFunction(
        "valueTimeTiles",
        {TboxType::tbox(), LogicalType::DOUBLE, LogicalType::INTERVAL,
         LogicalType::DOUBLE, LogicalType::TIMESTAMP_TZ},
        list_tbox, TboxValueTimeTilesExec));
    RegisterMeosFunction(loader, ScalarFunction(
        "valueTimeTiles",
        {TboxType::tbox(), LogicalType::INTEGER, LogicalType::INTERVAL,
         LogicalType::INTEGER, LogicalType::TIMESTAMP_TZ, LogicalType::BOOLEAN},
        list_tbox, TboxValueTimeTilesExec));
    RegisterMeosFunction(loader, ScalarFunction(
        "valueTimeTiles",
        {TboxType::tbox(), LogicalType::DOUBLE, LogicalType::INTERVAL,
         LogicalType::DOUBLE, LogicalType::TIMESTAMP_TZ, LogicalType::BOOLEAN},
        list_tbox, TboxValueTimeTilesExec));
}

// ============================================================
// timeSplit / valueSplit / valueTimeSplit  — table-function impls
// ============================================================

namespace {

// ---------- timeSplit / valueSplit / valueTimeSplit ----------
//
// Each takes its temporal value from a literal or from a LATERAL column
// through DuckDB's in_out_function, as spaceSplit and spaceTimeSplit do
// (src/geo/tgeompoint.cpp). The bind declares the result columns from the
// function_info of the registration, which states the temporal type, the bins
// the split produces and whether its value bins are integers; each input row
// is split when the output reaches it, and its bins are emitted at most
// STANDARD_VECTOR_SIZE at a time.

struct TemporalSplitInfo : public TableFunctionInfo {
    TemporalSplitInfo(LogicalType ttype_p, bool with_value_p, bool is_int_p, bool with_time_p)
        : ttype(std::move(ttype_p)), with_value(with_value_p), is_int(is_int_p),
          with_time(with_time_p) {
    }
    LogicalType ttype;  // the temporal type of the input and of each piece
    bool with_value;    // valueSplit / valueTimeSplit: a value bin
    bool is_int;        // integer value bin size and origin, else double
    bool with_time;     // timeSplit / valueTimeSplit: a time bin
};

struct TemporalSplitBindData : public TableFunctionData {
    TemporalSplitBindData(LogicalType ttype_p, bool with_value_p, bool is_int_p, bool with_time_p)
        : ttype(std::move(ttype_p)), with_value(with_value_p), is_int(is_int_p),
          with_time(with_time_p) {
    }
    LogicalType ttype;
    bool with_value;
    bool is_int;
    bool with_time;
};

struct TemporalSplitLocalState : public LocalTableFunctionState {
    idx_t current_input_row = 0;
    bool initialized_row = false;
    idx_t out_idx = 0;
    vector<Value> value_bins;  // valueSplit / valueTimeSplit
    vector<Value> time_bins;   // timeSplit / valueTimeSplit
    vector<Value> temporals;

    void Reset() {
        value_bins.clear();
        time_bins.clear();
        temporals.clear();
        out_idx = 0;
    }
};

unique_ptr<FunctionData> TemporalSplitBind(ClientContext &, TableFunctionBindInput &input,
                                           vector<LogicalType> &return_types, vector<string> &names) {
    auto &info = input.info->Cast<TemporalSplitInfo>();
    if (!info.with_value) {
        return_types = {LogicalType::TIMESTAMP_TZ, info.ttype};
        names = {"time", StringUtil::Lower(info.ttype.GetAlias())};
    } else if (info.with_time) {
        return_types = {info.is_int ? LogicalType::INTEGER : LogicalType::DOUBLE,
                        LogicalType::TIMESTAMP_TZ, info.ttype};
        names = {"value", "time", StringUtil::Lower(info.ttype.GetAlias())};
    } else {
        return_types = {info.is_int ? LogicalType::BIGINT : LogicalType::DOUBLE, info.ttype};
        names = {"number", "tnumber"};
    }
    return make_uniq<TemporalSplitBindData>(info.ttype, info.with_value, info.is_int, info.with_time);
}

unique_ptr<LocalTableFunctionState> TemporalSplitLocalInit(ExecutionContext &, TableFunctionInitInput &,
                                                           GlobalTableFunctionState *) {
    EnsureMeosThreadInitialized();
    return make_uniq<TemporalSplitLocalState>();
}

/* Split one input row into the local state.  Input column layout:
 *   timeSplit:      [0]=temporal, [1]=duration, [2]=origin (optional), [3]=borderInc (optional)
 *   valueSplit:     [0]=tnumber, [1]=size, [2]=origin (optional), [3]=borderInc (optional)
 *   valueTimeSplit: [0]=tnumber, [1]=vsize, [2]=duration,
 *                   [3]=vorigin and [4]=torigin (optional, together), [5]=borderInc (optional)
 * A NULL temporal, size, duration or borderInc splits into no row; an absent borderInc is true. */
void LoadTemporalSplitRow(TemporalSplitLocalState &state, const TemporalSplitBindData &bd,
                          DataChunk &input, idx_t row) {
    state.Reset();
    for (idx_t c = 0; c < input.ColumnCount(); c++) {
        input.data[c].Flatten(input.size());
    }
    if (FlatVector::IsNull(input.data[0], row) || FlatVector::IsNull(input.data[1], row)) {
        return;
    }
    if (bd.with_value && bd.with_time && FlatVector::IsNull(input.data[2], row)) {
        return;
    }
    const idx_t duration_col = bd.with_value ? 2 : 1;
    const idx_t vorigin_col = bd.with_time ? 3 : 2;
    const idx_t torigin_col = bd.with_value ? 4 : 2;
    const bool has_vorigin = bd.with_value && input.ColumnCount() > vorigin_col &&
                             !FlatVector::IsNull(input.data[vorigin_col], row);
    const idx_t border_col = (bd.with_value && bd.with_time) ? 5 : 3;
    bool border_inc = true;
    if (input.ColumnCount() > border_col) {
        if (FlatVector::IsNull(input.data[border_col], row)) {
            return;
        }
        border_inc = FlatVector::GetData<bool>(input.data[border_col])[row];
    }
    MeosInterval mi {};
    TimestampTz torigin = DEFAULT_TIME_ORIGIN_MEOS;
    if (bd.with_time) {
        mi = IntervaltToInterval(FlatVector::GetData<interval_t>(input.data[duration_col])[row]);
        if (input.ColumnCount() > torigin_col && !FlatVector::IsNull(input.data[torigin_col], row)) {
            timestamp_tz_t t = FlatVector::GetData<timestamp_tz_t>(input.data[torigin_col])[row];
            torigin = static_cast<TimestampTz>(DuckDBToMeosTimestamp(t).value);
        }
    }

    string_t blob = FlatVector::GetData<string_t>(input.data[0])[row];
    Temporal *temp = static_cast<Temporal *>(malloc(blob.GetSize()));
    memcpy(temp, blob.GetData(), blob.GetSize());

    int count = 0;
    Temporal **parts = nullptr;
    TimestampTz *tbins = nullptr;
    int *ibins = nullptr;
    double *dbins = nullptr;
    if (!bd.with_value) {
        parts = temporal_time_split(temp, &mi, torigin, border_inc, &tbins, &count);
    } else if (bd.is_int) {
        int vsize = FlatVector::GetData<int32_t>(input.data[1])[row];
        int vorigin = has_vorigin ? FlatVector::GetData<int32_t>(input.data[vorigin_col])[row] : 0;
        parts = bd.with_time
            ? tint_value_time_split(temp, vsize, &mi, vorigin, torigin, border_inc, &ibins, &tbins, &count)
            : tint_value_split(temp, vsize, vorigin, border_inc, &ibins, &count);
    } else {
        double vsize = FlatVector::GetData<double>(input.data[1])[row];
        double vorigin = has_vorigin ? FlatVector::GetData<double>(input.data[vorigin_col])[row] : 0.0;
        parts = bd.with_time
            ? tfloat_value_time_split(temp, vsize, &mi, vorigin, torigin, border_inc, &dbins, &tbins, &count)
            : tfloat_value_split(temp, vsize, vorigin, border_inc, &dbins, &count);
    }
    free(temp);

    if (parts && count > 0) {
        state.temporals.reserve(count);
        if (bd.with_value) {
            state.value_bins.reserve(count);
        }
        if (bd.with_time) {
            state.time_bins.reserve(count);
        }
        for (int i = 0; i < count; i++) {
            if (bd.with_value && bd.is_int) {
                state.value_bins.push_back(bd.with_time ? Value::INTEGER(ibins[i]) : Value::BIGINT(ibins[i]));
            } else if (bd.with_value) {
                state.value_bins.push_back(Value::DOUBLE(dbins[i]));
            }
            if (bd.with_time) {
                timestamp_tz_t ts = MeosToDuckDBTimestamp(timestamp_tz_t(static_cast<int64_t>(tbins[i])));
                state.time_bins.push_back(Value::TIMESTAMPTZ(ts));
            }
            Value tblob = Value::BLOB(const_data_ptr_cast(parts[i]), temporal_mem_size(parts[i]));
            tblob.Reinterpret(bd.ttype);
            state.temporals.push_back(std::move(tblob));
            free(parts[i]);
        }
    }
    free(parts);
    free(ibins);
    free(dbins);
    free(tbins);
}

OperatorResultType TemporalSplitInOut(ExecutionContext &, TableFunctionInput &data_p, DataChunk &input,
                                      DataChunk &output) {
    auto &bd = data_p.bind_data->Cast<TemporalSplitBindData>();
    auto &state = data_p.local_state->Cast<TemporalSplitLocalState>();
    idx_t out_row = 0;

    /* As in SpaceSplitInOutCommon: on a drained input chunk the call returns
     * the rows it emitted as HAVE_MORE_OUTPUT, or NEED_MORE_INPUT when it
     * emitted none, so the source-scan path stops and the LATERAL path
     * advances to the next chunk. */
    while (out_row < STANDARD_VECTOR_SIZE) {
        if (!state.initialized_row) {
            if (state.current_input_row >= input.size()) {
                if (out_row > 0) {
                    output.SetCardinality(out_row);
                    return OperatorResultType::HAVE_MORE_OUTPUT;
                }
                state.current_input_row = 0;
                return OperatorResultType::NEED_MORE_INPUT;
            }
            LoadTemporalSplitRow(state, bd, input, state.current_input_row);
            state.initialized_row = true;
        }
        if (state.out_idx >= state.temporals.size()) {
            state.current_input_row++;
            state.initialized_row = false;
            continue;
        }
        idx_t col = 0;
        if (bd.with_value) {
            output.data[col++].SetValue(out_row, state.value_bins[state.out_idx]);
        }
        if (bd.with_time) {
            output.data[col++].SetValue(out_row, state.time_bins[state.out_idx]);
        }
        output.data[col].SetValue(out_row, state.temporals[state.out_idx]);
        state.out_idx++;
        out_row++;
    }
    output.SetCardinality(out_row);
    return OperatorResultType::HAVE_MORE_OUTPUT;
}

void RegisterTemporalSplit(ExtensionLoader &loader, const string &name, vector<LogicalType> args,
                           LogicalType ttype, bool with_value, bool is_int, bool with_time) {
    TableFunction fn(name, std::move(args), /*function=*/nullptr, TemporalSplitBind,
                     /*init_global=*/nullptr, TemporalSplitLocalInit);
    fn.in_out_function = TemporalSplitInOut;
    fn.function_info = make_shared_ptr<TemporalSplitInfo>(std::move(ttype), with_value, is_int, with_time);
    RegisterMeosFunction(loader, fn);
}

} // anonymous namespace

void TemporalTypes::RegisterTemporalTileSplit(ExtensionLoader &loader) {
    const auto I  = LogicalType::INTEGER;
    const auto D  = LogicalType::DOUBLE;
    const auto IV = LogicalType::INTERVAL;
    const auto TS = LogicalType::TIMESTAMP_TZ;
    const auto B  = LogicalType::BOOLEAN;

    // timeSplit(temporal, interval [, timestamptz [, borderInc]])
    for (const auto &ttype : AllTypes()) {
        RegisterTemporalSplit(loader, "timeSplit", {ttype, IV}, ttype, false, false, true);
        RegisterTemporalSplit(loader, "timeSplit", {ttype, IV, TS}, ttype, false, false, true);
        RegisterTemporalSplit(loader, "timeSplit", {ttype, IV, TS, B}, ttype, false, false, true);
    }
    // also for tgeompoint
    for (const auto &ttype : {TgeompointType::tgeompoint()}) {
        RegisterTemporalSplit(loader, "timeSplit", {ttype, IV}, ttype, false, false, true);
        RegisterTemporalSplit(loader, "timeSplit", {ttype, IV, TS}, ttype, false, false, true);
        RegisterTemporalSplit(loader, "timeSplit", {ttype, IV, TS, B}, ttype, false, false, true);
    }

    // valueSplit is registered separately via RegisterTnumberValueSplit

    // valueTimeSplit(tint, integer, interval [, integer, timestamptz])
    RegisterTemporalSplit(loader, "valueTimeSplit", {tint(), I, IV}, tint(), true, true, true);
    RegisterTemporalSplit(loader, "valueTimeSplit", {tint(), I, IV, I, TS}, tint(), true, true, true);
    // valueTimeSplit(tfloat, double, interval [, double, timestamptz])
    RegisterTemporalSplit(loader, "valueTimeSplit", {tfloat(), D, IV}, tfloat(), true, false, true);
    RegisterTemporalSplit(loader, "valueTimeSplit", {tfloat(), D, IV, D, TS}, tfloat(), true, false, true);
    // each full form also takes borderInc
    RegisterTemporalSplit(loader, "valueTimeSplit", {tint(), I, IV, I, TS, B}, tint(), true, true, true);
    RegisterTemporalSplit(loader, "valueTimeSplit", {tfloat(), D, IV, D, TS, B}, tfloat(), true, false, true);
}

/* ***************************************************
 * valueSplit(tint|tfloat, size [, origin]) → SETOF (number, tnumber)
 * ---------------------------------------------------
 * Wraps MEOS tint_value_split / tfloat_value_split through the in_out
 * function valueTimeSplit shares. Each row is a (bin-start value,
 * sub-temporal) pair where the sub-temporal is the slice of the input whose
 * value fell into that bin.
 ****************************************************/

void TemporalTypes::RegisterTnumberValueSplit(ExtensionLoader &loader) {
    const auto I = LogicalType::INTEGER;
    const auto D = LogicalType::DOUBLE;
    RegisterTemporalSplit(loader, "valueSplit", {tint(), I}, tint(), true, true, false);
    RegisterTemporalSplit(loader, "valueSplit", {tint(), I, I}, tint(), true, true, false);
    RegisterTemporalSplit(loader, "valueSplit", {tfloat(), D}, tfloat(), true, false, false);
    RegisterTemporalSplit(loader, "valueSplit", {tfloat(), D, D}, tfloat(), true, false, false);
    const auto B = LogicalType::BOOLEAN;
    RegisterTemporalSplit(loader, "valueSplit", {tint(), I, I, B}, tint(), true, true, false);
    RegisterTemporalSplit(loader, "valueSplit", {tfloat(), D, D, B}, tfloat(), true, false, false);
}

} // namespace duckdb
