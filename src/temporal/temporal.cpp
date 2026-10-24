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
        // The constructors from a base value and a time value, tint(integer, timestamptz) ..
        // ttext(text, tstzspanset), are generated (meos_temporal_constructor, RETIRED_GROUPS).
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {type, LogicalType::INTEGER},
                type,
                TemporalFunctions::Temporal_enforce_typmod
            )
        );

        // timeSpan/valueSpan (temporal_to_tstzspan / tnumber_to_span, group
        // meos_temporal_conversion) are generated from the catalog sqlSignatures
        // in src/generated/generated_temporal_udfs.cpp (RETIRED_GROUPS).

        // timestamps(<temporal_type>) is generated from the catalog (temporal_timestamps) in generated_temporal_udfs.cpp.
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

    // whenTrue(tbool) is generated from the catalog (tbool_when_true, meos_temporal_bool).

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

    // avgValue(tint | tbigint | tfloat) is generated from the catalog (Gen_tnumber_avg_value).

    // The boolean, arithmetic, text and distance operators over the temporal types are
    // generated from the catalog (src/generated).

    // Unary tfloat transforms floor/ceil/round/degrees/radians are float-base scalar
    // transforms generated from the catalog (generated_temporal_udfs.cpp), full arity
    // plus the shorter DEFAULT-arg overload via sqlSignatures argDefaults.
    // The meos_temporal_math group (abs/derivative/exp/ln/log10/deltaValue/trend) is
    // supplied by the generated surface (RETIRED_GROUPS in the codegen).


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
    // tsample over every temporal type is generated from the catalog (temporal_tsample,
    // meos_temporal_analytics_reduction).

    // tspatial × {stbox, tspatial} position predicates (spatial axes
    // left/right/below/above/front/back + over*, and the time axis before/after/
    // overbefore/overafter; @ingroup meos_geo_box_pos / meos_geo_bbox_pos) — bare
    // names AND the <<, >>, &<, &> operator forms — are generated from the catalog
    // sqlSignatures in src/generated/generated_temporal_udfs.cpp. The stale-snake
    // temporal_* aliases are dropped (bare names supersede them).

    // ttext text functions
    // Portable-SQL alias (MobilityDB name): ttext_cat
}

} // namespace duckdb
