#include "meos_wrapper_simple.hpp"

#include "common.hpp"
#include "geo/tgeogpoint.hpp"
#include "geo/tgeogpoint_functions.hpp"
#include "geo/tgeompoint_functions.hpp"
#include "geo/geoset.hpp"
#include "temporal/temporal_functions.hpp"
#include "geo/stbox.hpp"
#include "temporal/spanset.hpp"
#include "temporal/temporal.hpp"
#include "temporal/set.hpp"
#include "temporal/span.hpp"

#include "duckdb/common/types/blob.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "spatial/spatial_types.hpp"
#include "mobilityduck/meos_exec_serial.hpp"

namespace duckdb {

LogicalType TgeogpointType::TGEOGPOINT() {
    LogicalType type(LogicalTypeId::BLOB);
    type.SetAlias("TGEOGPOINT");
    return type;
}

void TgeogpointType::RegisterType(ExtensionLoader &loader) {
    loader.RegisterType("TGEOGPOINT", TGEOGPOINT());
}

void TgeogpointType::RegisterCastFunctions(ExtensionLoader &loader) {
    loader.RegisterCastFunction(
        LogicalType::VARCHAR,
        TGEOGPOINT(),
        TgeogpointFunctions::Tpoint_in
    );

    loader.RegisterCastFunction(
        TGEOGPOINT(),
        LogicalType::VARCHAR,
        TemporalFunctions::Temporal_out
    );

    loader.RegisterCastFunction(
        TGEOGPOINT(),
        StboxType::STBOX(),
        TgeompointFunctions::Tspatial_to_stbox_cast
    );

    loader.RegisterCastFunction(
        TGEOGPOINT(),
        SpanTypes::TSTZSPAN(),
        TgeompointFunctions::Temporal_to_tstzspan_cast
    );
}

void TgeogpointType::RegisterScalarFunctions(ExtensionLoader &loader) {

    /* ***************************************************
     * In/out functions
     ****************************************************/

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "asText",
            {TGEOGPOINT()},
            LogicalType::VARCHAR,
            TgeompointFunctions::Tspatial_as_text
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "asEWKT",
            {TGEOGPOINT()},
            LogicalType::VARCHAR,
            TgeompointFunctions::Tspatial_as_ewkt
        )
    );

    const auto varchar_list = LogicalType::LIST(LogicalType::VARCHAR);

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "asText",
            {LogicalType::LIST(TGEOGPOINT())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "asText",
            {LogicalType::LIST(TGEOGPOINT()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(TGEOGPOINT())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(TGEOGPOINT()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );

    /* ***************************************************
    * Constructor functions
    ****************************************************/
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "TGEOGPOINT",
            {GeoTypes::GEOMETRY(), LogicalType::TIMESTAMP_TZ},
            TGEOGPOINT(),
            TgeogpointFunctions::Tpointinst_constructor
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "TGEOGPOINT",
            {GeoTypes::GEOMETRY(), LogicalType::TIMESTAMP_TZ, LogicalType::INTEGER},
            TGEOGPOINT(),
            TgeogpointFunctions::Tpointinst_constructor
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpoint",
            {GeoTypes::GEOMETRY(), SetTypes::tstzset()},
            TGEOGPOINT(),
            TemporalFunctions::Tsequence_from_base_tstzset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpoint",
            {GeoTypes::GEOMETRY(), SpanTypes::TSTZSPAN()},
            TGEOGPOINT(),
            TemporalFunctions::Tsequence_from_base_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpoint",
            {GeoTypes::GEOMETRY(), SpanTypes::TSTZSPAN(), LogicalType::VARCHAR},
            TGEOGPOINT(),
            TemporalFunctions::Tsequence_from_base_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpoint",
            {GeoTypes::GEOMETRY(), SpansetTypes::tstzspanset()},
            TGEOGPOINT(),
            TemporalFunctions::Tsequenceset_from_base_tstzspanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpoint",
            {GeoTypes::GEOMETRY(), SpansetTypes::tstzspanset(), LogicalType::VARCHAR},
            TGEOGPOINT(),
            TemporalFunctions::Tsequenceset_from_base_tstzspanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpointSeq",
            {LogicalType::LIST(TGEOGPOINT())},
            TGEOGPOINT(),
            TgeompointFunctions::Tgeompoint_sequence_constructor
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpointSeqSet",
            {LogicalType::LIST(TGEOGPOINT())},
            TGEOGPOINT(),
            TemporalFunctions::Tsequenceset_constructor
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "stbox",
            {TGEOGPOINT()},
            StboxType::STBOX(),
            TgeompointFunctions::Tspatial_to_stbox
        )
    );

    /* ***************************************************
     * Conversion functions
     ****************************************************/
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "timeSpan",
            {TGEOGPOINT()},
            SpanTypes::TSTZSPAN(),
            TgeompointFunctions::Temporal_to_tstzspan
        )
    );

    /***************************************************
     * Transformation functions
     ****************************************************/

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpointInst",
            {TGEOGPOINT()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_to_tinstant
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpointSeq",
            {TGEOGPOINT(), LogicalType::VARCHAR},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_to_tsequence
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpointSeq",
            {TGEOGPOINT()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_to_tsequence
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpointSeqSet",
            {TGEOGPOINT(), LogicalType::VARCHAR},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_to_tsequenceset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpointSeqSet",
            {TGEOGPOINT()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_to_tsequenceset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "setInterp",
            {TGEOGPOINT(), LogicalType::VARCHAR},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_set_interp
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "appendInstant",
            {TGEOGPOINT(), TGEOGPOINT()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_append_tinstant
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "appendSequence",
            {TGEOGPOINT(), TGEOGPOINT()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_append_tsequence
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "merge",
            {TGEOGPOINT(), TGEOGPOINT()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_merge
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "merge",
            {LogicalType::LIST(TGEOGPOINT())},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_merge_array
        )
    );

    /* ***************************************************
    * Accessor functions
    ****************************************************/
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tempSubtype",
            {TGEOGPOINT()},
            LogicalType::VARCHAR,
            TemporalFunctions::Temporal_subtype
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "interp",
            {TGEOGPOINT()},
            LogicalType::VARCHAR,
            TemporalFunctions::Temporal_interp
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "getValue",
            {TGEOGPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::Tgeompoint_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "getTimestamp",
            {TGEOGPOINT()},
            LogicalType::TIMESTAMP_TZ,
            TemporalFunctions::Tinstant_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "valueSet",
            {TGEOGPOINT()},
            SpatialSetType::geomset(),
            TemporalFunctions::Temporal_valueset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "valueN",
            {TGEOGPOINT(), LogicalType::BIGINT},
            GeoTypes::GEOMETRY(),
            TemporalFunctions::Temporal_value_n
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "getTime",
            {TGEOGPOINT()},
            SpansetTypes::tstzspanset(),
            TemporalFunctions::Temporal_time
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "startValue",
            {TGEOGPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::Tgeompoint_start_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "endValue",
            {TGEOGPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::Tgeompoint_end_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "duration",
            {TGEOGPOINT()},
            LogicalType::INTERVAL,
            TemporalFunctions::Temporal_duration
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "duration",
            {TGEOGPOINT(), LogicalType::BOOLEAN},
            LogicalType::INTERVAL,
            TemporalFunctions::Temporal_duration
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "memSize",
            {TGEOGPOINT()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_mem_size
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "lowerInc",
            {TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_lower_inc
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "upperInc",
            {TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_upper_inc
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "numInstants",
            {TGEOGPOINT()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_num_instants
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "startInstant",
            {TGEOGPOINT()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_start_instant
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "endInstant",
            {TGEOGPOINT()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_end_instant
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "instantN",
            {TGEOGPOINT(), LogicalType::INTEGER},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_instant_n
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "instants",
            {TGEOGPOINT()},
            LogicalType::LIST(TGEOGPOINT()),
            TemporalFunctions::Temporal_instants
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "numTimestamps",
            {TGEOGPOINT()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_num_timestamps
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "startTimestamp",
            {TGEOGPOINT()},
            LogicalType::TIMESTAMP_TZ,
            TemporalFunctions::Temporal_start_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "endTimestamp",
            {TGEOGPOINT()},
            LogicalType::TIMESTAMP_TZ,
            TemporalFunctions::Temporal_end_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "timestampN",
            {TGEOGPOINT(), LogicalType::INTEGER},
            LogicalType::TIMESTAMP_TZ,
            TemporalFunctions::Temporal_timestamptz_n
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "timestamps",
            {TGEOGPOINT()},
            LogicalType::LIST(LogicalType::TIMESTAMP_TZ),
            TemporalFunctions::Temporal_timestamps
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "numSequences",
            {TGEOGPOINT()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_num_sequences
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "startSequence",
            {TGEOGPOINT()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_start_sequence
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "endSequence",
            {TGEOGPOINT()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_end_sequence
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "sequenceN",
            {TGEOGPOINT(), LogicalType::INTEGER},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_sequence_n
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "sequences",
            {TGEOGPOINT()},
            LogicalType::LIST(TGEOGPOINT()),
            TemporalFunctions::Temporal_sequences
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "segments",
            {TGEOGPOINT()},
            LogicalType::LIST(TGEOGPOINT()),
            TemporalFunctions::Temporal_segments
        )
    );

    /* ***************************************************
     * Shift and Scale functions
     ****************************************************/
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "shiftTime",
            {TGEOGPOINT(), LogicalType::INTERVAL},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_shift_time
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "scaleTime",
            {TGEOGPOINT(), LogicalType::INTERVAL},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_scale_time
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "shiftScaleTime",
            {TGEOGPOINT(), LogicalType::INTERVAL, LogicalType::INTERVAL},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_shift_scale_time
        )
    );

    /* ***************************************************
     * Restriction functions
     ****************************************************/

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "atValues",
            {TGEOGPOINT(), GeoTypes::GEOMETRY()},
            TGEOGPOINT(),
            TgeompointFunctions::Tgeompoint_at_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "minusValues",
            {TGEOGPOINT(), GeoTypes::GEOMETRY()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_minus_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "atValues",
            {TGEOGPOINT(), SpatialSetType::geomset()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_at_values
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "minusValues",
            {TGEOGPOINT(), SpatialSetType::geomset()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_minus_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "atTime",
            {TGEOGPOINT(), LogicalType::TIMESTAMP_TZ},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_at_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "minusTime",
            {TGEOGPOINT(), LogicalType::TIMESTAMP_TZ},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_minus_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "valueAtTimestamp",
            {TGEOGPOINT(), LogicalType::TIMESTAMP_TZ},
            GeoTypes::GEOMETRY(),
            TemporalFunctions::Temporal_value_at_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "atTime",
            {TGEOGPOINT(), SetTypes::tstzset()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_at_tstzset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "minusTime",
            {TGEOGPOINT(), SetTypes::tstzset()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_minus_tstzset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "atTime",
            {TGEOGPOINT(), SpanTypes::TSTZSPAN()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_at_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "minusTime",
            {TGEOGPOINT(), SpanTypes::TSTZSPAN()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_minus_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "atTime",
            {TGEOGPOINT(), SpansetTypes::tstzspanset()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_at_tstzspanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "minusTime",
            {TGEOGPOINT(), SpansetTypes::tstzspanset()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_minus_tstzspanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "beforeTimestamp",
            {TGEOGPOINT(), LogicalType::TIMESTAMP_TZ},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_before_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "afterTimestamp",
            {TGEOGPOINT(), LogicalType::TIMESTAMP_TZ},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_after_timestamptz
        )
    );

    /* ***************************************************
     * Modification function
     ****************************************************/
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "insert",
            {TGEOGPOINT(), TGEOGPOINT()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_update
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "insert",
            {TGEOGPOINT(), TGEOGPOINT(), LogicalType::BOOLEAN},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_update
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "update",
            {TGEOGPOINT(), TGEOGPOINT()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_update
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "update",
            {TGEOGPOINT(), TGEOGPOINT(), LogicalType::BOOLEAN},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_update
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "deleteTime",
            {TGEOGPOINT(), LogicalType::TIMESTAMP_TZ},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_delete_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "deleteTime",
            {TGEOGPOINT(), LogicalType::TIMESTAMP_TZ, LogicalType::BOOLEAN},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_delete_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "deleteTime",
            {TGEOGPOINT(), SetTypes::tstzset()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_delete_tstzset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "deleteTime",
            {TGEOGPOINT(), SetTypes::tstzset(), LogicalType::BOOLEAN},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_delete_tstzset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "deleteTime",
            {TGEOGPOINT(), SpanTypes::TSTZSPAN()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_delete_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "deleteTime",
            {TGEOGPOINT(), SpanTypes::TSTZSPAN(), LogicalType::BOOLEAN},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_delete_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "deleteTime",
            {TGEOGPOINT(), SpansetTypes::tstzspanset()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_delete_tstzspanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "deleteTime",
            {TGEOGPOINT(), SpansetTypes::tstzspanset(), LogicalType::BOOLEAN},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_delete_tstzspanset
        )
    );

    /* ***************************************************
     * Stops function
     ****************************************************/

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "stops",
            {TGEOGPOINT(), LogicalType::DOUBLE, LogicalType::INTERVAL},
            TGEOGPOINT(),
            TgeompointFunctions::Tgeompoint_stops
        )
    );

    /* ***************************************************
     * Comparison functions
     ****************************************************/
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "temporal_eq",
            {TGEOGPOINT(), TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_eq
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "temporal_ne",
            {TGEOGPOINT(), TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_ne
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "temporal_lt",
            {TGEOGPOINT(), TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_lt
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "temporal_le",
            {TGEOGPOINT(), TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_le
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "temporal_gt",
            {TGEOGPOINT(), TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_gt
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "temporal_ge",
            {TGEOGPOINT(), TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_ge
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "temporal_cmp",
            {TGEOGPOINT(), TGEOGPOINT()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_cmp
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "=",
            {TGEOGPOINT(), TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_eq
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "<>",
            {TGEOGPOINT(), TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_ne
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "<",
            {TGEOGPOINT(), TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_lt
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "<=",
            {TGEOGPOINT(), TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_le
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            ">",
            {TGEOGPOINT(), TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_gt
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            ">=",
            {TGEOGPOINT(), TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_ge
        )
    );

    /* ***************************************************
     * Spatial functions
     ****************************************************/
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "getX",
            {TGEOGPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tpoint_get_x
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "getY",
            {TGEOGPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tpoint_get_y
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "getZ",
            {TGEOGPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tpoint_get_z
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "length",
            {TGEOGPOINT()},
            LogicalType::DOUBLE,
            TgeompointFunctions::Tpoint_length
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "cumulativeLength",
            {TGEOGPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tpoint_cumulative_length
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "speed",
            {TGEOGPOINT()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Temporal_derivative
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "twCentroid",
            {TGEOGPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::Tpoint_twcentroid
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "direction",
            {TGEOGPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tpoint_direction
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "azimuth",
            {TGEOGPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tpoint_azimuth
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "angularDifference",
            {TGEOGPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::Tpoint_angular_difference
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "isSimple",
            {TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Tpoint_is_simple
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "makeSimple",
            {TGEOGPOINT()},
            LogicalType::LIST(TGEOGPOINT()),
            TgeompointFunctions::Tpoint_make_simple
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "trajectory",
            {TGEOGPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::Tpoint_trajectory
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "trajectory_gs",
            {TGEOGPOINT()},
            LogicalType::BLOB,
            TgeompointFunctions::Tpoint_trajectory_gs
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "atGeometry",
            {TGEOGPOINT(), GeoTypes::GEOMETRY()},
            TGEOGPOINT(),
            TgeompointFunctions::Tgeo_at_geom
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "minusGeometry",
            {TGEOGPOINT(), GeoTypes::GEOMETRY()},
            TGEOGPOINT(),
            TgeompointFunctions::Tgeo_minus_geom
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "minusGeometry",
            {TGEOGPOINT(), GeoTypes::GEOMETRY(), SpanTypes::FLOATSPAN()},
            TGEOGPOINT(),
            TgeompointFunctions::Tgeo_minus_geom
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "atStbox",
            {TGEOGPOINT(), StboxType::STBOX()},
            TGEOGPOINT(),
            TgeompointFunctions::Tgeo_at_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "atStbox",
            {TGEOGPOINT(), StboxType::STBOX(), LogicalType::BOOLEAN},
            TGEOGPOINT(),
            TgeompointFunctions::Tgeo_at_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "minusStbox",
            {TGEOGPOINT(), StboxType::STBOX()},
            TGEOGPOINT(),
            TgeompointFunctions::Tgeo_minus_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "minusStbox",
            {TGEOGPOINT(), StboxType::STBOX(), LogicalType::BOOLEAN},
            TGEOGPOINT(),
            TgeompointFunctions::Tgeo_minus_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "transform",
            {TGEOGPOINT(), LogicalType::INTEGER},
            TGEOGPOINT(),
            TgeompointFunctions::Tspatial_transform
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "round",
            {TGEOGPOINT(), LogicalType::INTEGER},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_round
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "round",
            {TGEOGPOINT()},
            TGEOGPOINT(),
            TemporalFunctions::Temporal_round
        )
    );

    /* ***************************************************
     * Spatial relationships
     ****************************************************/
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "eContains",
            {GeoTypes::GEOMETRY(), TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Econtains_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "aContains",
            {GeoTypes::GEOMETRY(), TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Acontains_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "eDisjoint",
            {GeoTypes::GEOMETRY(), TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Edisjoint_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "eDisjoint",
            {TGEOGPOINT(), GeoTypes::GEOMETRY()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Edisjoint_tgeo_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "eDisjoint",
            {TGEOGPOINT(), TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Edisjoint_tgeo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "aDisjoint",
            {GeoTypes::GEOMETRY(), TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Adisjoint_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "aDisjoint",
            {TGEOGPOINT(), GeoTypes::GEOMETRY()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Adisjoint_tgeo_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "aDisjoint",
            {TGEOGPOINT(), TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Adisjoint_tgeo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "eIntersects",
            {TGEOGPOINT(), GeoTypes::GEOMETRY()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Eintersects_tgeo_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "eIntersects",
            {GeoTypes::GEOMETRY(), TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Eintersects_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "eIntersects",
            {TGEOGPOINT(), TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Eintersects_tgeo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "aIntersects",
            {TGEOGPOINT(), GeoTypes::GEOMETRY()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Aintersects_tgeo_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "aIntersects",
            {GeoTypes::GEOMETRY(), TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Aintersects_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "aIntersects",
            {TGEOGPOINT(), TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Aintersects_tgeo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "eTouches",
            {GeoTypes::GEOMETRY(), TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Etouches_geo_tpoint
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "eTouches",
            {TGEOGPOINT(), GeoTypes::GEOMETRY()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Etouches_tpoint_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "aTouches",
            {GeoTypes::GEOMETRY(), TGEOGPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Atouches_geo_tpoint
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "aTouches",
            {TGEOGPOINT(), GeoTypes::GEOMETRY()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Atouches_tpoint_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "eDwithin",
            {TGEOGPOINT(), TGEOGPOINT(), LogicalType::DOUBLE},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Edwithin_tgeo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "eDwithin",
            {GeoTypes::GEOMETRY(), TGEOGPOINT(), LogicalType::DOUBLE},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Edwithin_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "eDwithin",
            {TGEOGPOINT(), GeoTypes::GEOMETRY(), LogicalType::DOUBLE},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Edwithin_tgeo_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "aDwithin",
            {GeoTypes::GEOMETRY(), TGEOGPOINT(), LogicalType::DOUBLE},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Adwithin_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "aDwithin",
            {TGEOGPOINT(), GeoTypes::GEOMETRY(), LogicalType::DOUBLE},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Adwithin_tgeo_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "aDwithin",
            {TGEOGPOINT(), TGEOGPOINT(), LogicalType::DOUBLE},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Adwithin_tgeo_tgeo
        )
    );

    /* ***************************************************
     * Temporal-spatial relationships
     ****************************************************/
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tContains",
            {GeoTypes::GEOMETRY(), TGEOGPOINT()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tcontains_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tContains",
            {GeoTypes::GEOMETRY(), TGEOGPOINT(), LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tcontains_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tDisjoint",
            {TGEOGPOINT(), GeoTypes::GEOMETRY()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdisjoint_tgeo_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tDisjoint",
            {TGEOGPOINT(), GeoTypes::GEOMETRY(), LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdisjoint_tgeo_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tDisjoint",
            {GeoTypes::GEOMETRY(), TGEOGPOINT()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdisjoint_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tDisjoint",
            {GeoTypes::GEOMETRY(), TGEOGPOINT(), LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdisjoint_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tDisjoint",
            {TGEOGPOINT(), TGEOGPOINT(), LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdisjoint_tgeo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tDisjoint",
            {TGEOGPOINT(), TGEOGPOINT()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdisjoint_tgeo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tIntersects",
            {GeoTypes::GEOMETRY(), TGEOGPOINT(), LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tintersects_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tIntersects",
            {GeoTypes::GEOMETRY(), TGEOGPOINT()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tintersects_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tIntersects",
            {TGEOGPOINT(), GeoTypes::GEOMETRY(), LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tintersects_tgeo_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tIntersects",
            {TGEOGPOINT(), GeoTypes::GEOMETRY()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tintersects_tgeo_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tIntersects",
            {TGEOGPOINT(), TGEOGPOINT(), LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tintersects_tgeo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tIntersects",
            {TGEOGPOINT(), TGEOGPOINT()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tintersects_tgeo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tTouches",
            {GeoTypes::GEOMETRY(), TGEOGPOINT(), LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Ttouches_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tTouches",
            {GeoTypes::GEOMETRY(), TGEOGPOINT()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Ttouches_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tTouches",
            {TGEOGPOINT(), GeoTypes::GEOMETRY(), LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Ttouches_tgeo_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tTouches",
            {TGEOGPOINT(), GeoTypes::GEOMETRY()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Ttouches_tgeo_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tDwithin",
            {GeoTypes::GEOMETRY(), TGEOGPOINT(), LogicalType::DOUBLE},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdwithin_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tDwithin",
            {GeoTypes::GEOMETRY(), TGEOGPOINT(), LogicalType::DOUBLE, LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdwithin_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tDwithin",
            {TGEOGPOINT(), GeoTypes::GEOMETRY(), LogicalType::DOUBLE, LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdwithin_tgeo_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tDwithin",
            {TGEOGPOINT(), GeoTypes::GEOMETRY(), LogicalType::DOUBLE},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdwithin_tgeo_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tDwithin",
            {TGEOGPOINT(), TGEOGPOINT(), LogicalType::DOUBLE},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdwithin_tgeo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tDwithin",
            {TGEOGPOINT(), TGEOGPOINT(), LogicalType::DOUBLE, LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdwithin_tgeo_tgeo
        )
    );

    /* ***************************************************
     * Operators (workaround as functions)
     ****************************************************/

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "&&",
            {TGEOGPOINT(), StboxType::STBOX()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Temporal_overlaps_tgeompoint_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "&&",
            {TGEOGPOINT(), SpanTypes::TSTZSPAN()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Temporal_overlaps_tgeompoint_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "@>",
            {TGEOGPOINT(), StboxType::STBOX()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Temporal_contains_tgeompoint_stbox
        )
    );

    /* ***************************************************
     * Distance functions
     ****************************************************/

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "<->",
            {TGEOGPOINT(), TGEOGPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tdistance_tgeo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "shortestLine",
            {TGEOGPOINT(), TGEOGPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::ShortestLine_tgeo_tgeo
        )
    );
}

/* ***************************************************
 * Round-trip I/O for tgeogpoint: asEWKB / asHexWKB / asHexEWKB /
 * asMFJSON and the matching tgeogpointFromText / FromBinary / FromEWKB /
 * FromHexWKB / FromHexEWKB / FromMFJSON constructors.
 ****************************************************/

namespace {

constexpr uint8_t GEOG_WKB_BASE = 0x00;

inline Temporal *GeogBlobToTemp(string_t b) {
    size_t sz = b.GetSize();
    uint8_t *copy = (uint8_t *)malloc(sz);
    memcpy(copy, b.GetData(), sz);
    return reinterpret_cast<Temporal *>(copy);
}

string_t GeogTempToResultBlob(Vector &result, Temporal *t) {
    size_t sz = temporal_mem_size(t);
    string_t blob(reinterpret_cast<const char *>(t), sz);
    return StringVector::AddStringOrBlob(result, blob);
}

void TgeogAsWkbExec(DataChunk &args, ExpressionState &state, Vector &result, uint8_t variant) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            Temporal *t = GeogBlobToTemp(input);
            size_t sz = 0;
            uint8_t *wkb = temporal_as_wkb(t, variant, &sz);
            free(t);
            if (!wkb || sz == 0) {
                if (wkb) free(wkb);
                throw InternalException("temporal_as_wkb returned null");
            }
            string_t blob(reinterpret_cast<const char *>(wkb), sz);
            string_t stored = StringVector::AddStringOrBlob(result, blob);
            free(wkb);
            return stored;
        });
}

void TgeogAsHexWkbExec(DataChunk &args, ExpressionState &state, Vector &result, uint8_t variant) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            Temporal *t = GeogBlobToTemp(input);
            size_t sz = 0;
            char *hex = temporal_as_hexwkb(t, variant, &sz);
            (void) sz;
            free(t);
            if (!hex) throw InternalException("temporal_as_hexwkb returned null");
            string_t stored = StringVector::AddString(result, hex);
            free(hex);
            return stored;
        });
}

void TgeogFromWkbExec(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            if (input.GetSize() == 0) throw InvalidInputException("Empty WKB input");
            uint8_t *wkb = (uint8_t *)malloc(input.GetSize());
            memcpy(wkb, input.GetData(), input.GetSize());
            Temporal *t = temporal_from_wkb(wkb, input.GetSize());
            free(wkb);
            if (!t) throw InvalidInputException("temporal_from_wkb: invalid WKB");
            string_t stored = GeogTempToResultBlob(result, t);
            free(t);
            return stored;
        });
}

void TgeogFromHexWkbExec(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string hex(input.GetData(), input.GetSize());
            Temporal *t = temporal_from_hexwkb(hex.c_str());
            if (!t) throw InvalidInputException("temporal_from_hexwkb: invalid hex-WKB");
            string_t stored = GeogTempToResultBlob(result, t);
            free(t);
            return stored;
        });
}

void TgeogAsMfjsonExec(DataChunk &args, ExpressionState &state, Vector &result) {
    const idx_t row_count = args.size();
    for (idx_t i = 0; i < args.ColumnCount(); i++) args.data[i].Flatten(row_count);
    auto in = FlatVector::GetData<string_t>(args.data[0]);
    auto out_data = FlatVector::GetData<string_t>(result);
    auto &out_validity = FlatVector::Validity(result);
    const idx_t cc = args.ColumnCount();
    for (idx_t row = 0; row < row_count; row++) {
        if (!FlatVector::Validity(args.data[0]).RowIsValid(row)) {
            out_validity.SetInvalid(row);
            continue;
        }
        Temporal *t = GeogBlobToTemp(in[row]);
        bool with_bbox = (cc > 1) ? FlatVector::GetData<bool>(args.data[1])[row] : false;
        int flags = (cc > 2) ? FlatVector::GetData<int32_t>(args.data[2])[row] : 0;
        int precision = (cc > 3) ? FlatVector::GetData<int32_t>(args.data[3])[row] : 15;
        std::string srs;
        const char *srs_cstr = nullptr;
        if (cc > 4) {
            string_t s = FlatVector::GetData<string_t>(args.data[4])[row];
            srs.assign(s.GetData(), s.GetSize());
            srs_cstr = srs.empty() ? nullptr : srs.c_str();
        }
        char *json = temporal_as_mfjson(t, with_bbox, flags, precision, srs_cstr);
        free(t);
        if (!json) {
            out_validity.SetInvalid(row);
            continue;
        }
        out_data[row] = StringVector::AddString(result, json);
        free(json);
    }
    if (row_count == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

void TgeogFromMfjsonExec(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string s(input.GetData(), input.GetSize());
            Temporal *t = tgeogpoint_from_mfjson(s.c_str());
            if (!t) throw InvalidInputException("tgeogpoint_from_mfjson: invalid MFJSON");
            string_t stored = GeogTempToResultBlob(result, t);
            free(t);
            return stored;
        });
}

void TgeogFromTextExec(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string s(input.GetData(), input.GetSize());
            Temporal *t = tgeogpoint_in(s.c_str());
            if (!t) throw InvalidInputException("tgeogpoint_in: invalid text");
            string_t stored = GeogTempToResultBlob(result, t);
            free(t);
            return stored;
        });
}

} // namespace

void TgeogpointType::RegisterRoundtripIO(ExtensionLoader &loader) {
    const auto T = TGEOGPOINT();
    const auto V = LogicalType::VARCHAR;
    const auto B = LogicalType::BLOB;
    const auto BL = LogicalType::BOOLEAN;
    const auto I = LogicalType::INTEGER;

    /* asBinary / asEWKB */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asBinary", {T}, B,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TgeogAsWkbExec(a, s, r, GEOG_WKB_BASE); }));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asEWKB", {T}, B,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TgeogAsWkbExec(a, s, r, WKB_EXTENDED); }));

    /* asHexWKB / asHexEWKB */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asHexWKB", {T}, V,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TgeogAsHexWkbExec(a, s, r, GEOG_WKB_BASE); }));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asHexEWKB", {T}, V,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TgeogAsHexWkbExec(a, s, r, WKB_EXTENDED); }));

    /* asMFJSON: 1..5 arg overloads */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asMFJSON", {T},              V, TgeogAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asMFJSON", {T, BL},           V, TgeogAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asMFJSON", {T, BL, I},        V, TgeogAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asMFJSON", {T, BL, I, I},     V, TgeogAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asMFJSON", {T, BL, I, I, V},  V, TgeogAsMfjsonExec));

    /* tgeogpointFromText / FromEWKT */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tgeogpointFromText", {V}, T, TgeogFromTextExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tgeogpointFromEWKT", {V}, T, TgeogFromTextExec));

    /* tgeogpointFromBinary / FromEWKB — WKB carries type tag */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tgeogpointFromBinary", {B}, T, TgeogFromWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tgeogpointFromEWKB",   {B}, T, TgeogFromWkbExec));

    /* tgeogpointFromHexWKB / FromHexEWKB */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tgeogpointFromHexWKB",  {V}, T, TgeogFromHexWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tgeogpointFromHexEWKB", {V}, T, TgeogFromHexWkbExec));

    /* tgeogpointFromMFJSON */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tgeogpointFromMFJSON", {V}, T, TgeogFromMfjsonExec));
}

} // namespace duckdb
