#include "meos_wrapper_simple.hpp"

#include "common.hpp"
#include "geo/tgeompoint.hpp"
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
#include "geo_util.hpp"

namespace duckdb {

LogicalType TgeompointType::TGEOMPOINT() {
    LogicalType type(LogicalTypeId::BLOB);
    type.SetAlias("TGEOMPOINT");
    return type;
}

void TgeompointType::RegisterType(ExtensionLoader &loader) {
    loader.RegisterType( "TGEOMPOINT", TGEOMPOINT());
}

void TgeompointType::RegisterCastFunctions(ExtensionLoader &loader) {
    loader.RegisterCastFunction(
        LogicalType::VARCHAR,
        TGEOMPOINT(),
        TgeompointFunctions::Tpoint_in
    );

    loader.RegisterCastFunction(
        TGEOMPOINT(),
        LogicalType::VARCHAR,
        TemporalFunctions::Temporal_out
    );

    loader.RegisterCastFunction(
        TGEOMPOINT(),
        StboxType::STBOX(),
        TgeompointFunctions::Tspatial_to_stbox_cast
    );

    loader.RegisterCastFunction(
        TGEOMPOINT(),
        SpanTypes::TSTZSPAN(),
        TgeompointFunctions::Temporal_to_tstzspan_cast
    );
}

void TgeompointType::RegisterScalarFunctions(ExtensionLoader &loader) {

    /* ***************************************************
     * In/out functions
     ****************************************************/

    loader.RegisterFunction(
        ScalarFunction(
            "asText",
            {TGEOMPOINT()},
            LogicalType::VARCHAR,
            TgeompointFunctions::Tspatial_as_text
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "asEWKT",
            {TGEOMPOINT()},
            LogicalType::VARCHAR,
            TgeompointFunctions::Tspatial_as_ewkt
        )
    );

    const auto varchar_list = LogicalType::LIST(LogicalType::VARCHAR);

    loader.RegisterFunction(
        ScalarFunction(
            "asText",
            {LogicalType::LIST(TGEOMPOINT())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "asText",
            {LogicalType::LIST(TGEOMPOINT()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "asText",
            {LogicalType::LIST(GeoTypes::GEOMETRY())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "asText",
            {LogicalType::LIST(GeoTypes::GEOMETRY()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(TGEOMPOINT())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(TGEOMPOINT()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(GeoTypes::GEOMETRY())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(GeoTypes::GEOMETRY()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );

    /* ***************************************************
    * Constructor functions
    ****************************************************/
    loader.RegisterFunction(
        ScalarFunction(
            "TGEOMPOINT",
            {GeoTypes::GEOMETRY(), LogicalType::TIMESTAMP_TZ},
            TGEOMPOINT(),
            TgeompointFunctions::Tpointinst_constructor
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "TGEOMPOINT",
            {GeoTypes::GEOMETRY(), LogicalType::TIMESTAMP_TZ, LogicalType::INTEGER}, // with SRID
            TGEOMPOINT(),
            TgeompointFunctions::Tpointinst_constructor
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tgeompoint",
            {GeoTypes::GEOMETRY(), SetTypes::tstzset()},
            TGEOMPOINT(),
            TemporalFunctions::Tsequence_from_base_tstzset
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tgeompoint",
            {GeoTypes::GEOMETRY(), SpanTypes::TSTZSPAN()},
            TGEOMPOINT(),
            TemporalFunctions::Tsequence_from_base_tstzspan
        )
    );

     loader.RegisterFunction(
        ScalarFunction(
            "tgeompoint",
            {GeoTypes::GEOMETRY(), SpanTypes::TSTZSPAN(), LogicalType::VARCHAR},
            TGEOMPOINT(),
            TemporalFunctions::Tsequence_from_base_tstzspan
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tgeompoint",
            {GeoTypes::GEOMETRY(), SpansetTypes::tstzspanset()},
            TGEOMPOINT(),
            TemporalFunctions::Tsequenceset_from_base_tstzspanset
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tgeompoint",
            {GeoTypes::GEOMETRY(), SpansetTypes::tstzspanset(), LogicalType::VARCHAR},
            TGEOMPOINT(),
            TemporalFunctions::Tsequenceset_from_base_tstzspanset
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tgeompointSeq",
            {LogicalType::LIST(TGEOMPOINT())},
            TGEOMPOINT(),
            // TemporalFunctions::Tsequence_constructor
            TgeompointFunctions::Tgeompoint_sequence_constructor
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tgeompointSeqSet",
            {LogicalType::LIST(TGEOMPOINT())},
            TGEOMPOINT(),
            TemporalFunctions::Tsequenceset_constructor
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox",
            {TGEOMPOINT()},
            StboxType::STBOX(),
            TgeompointFunctions::Tspatial_to_stbox
        )
    );
    
    /* ***************************************************
     * Conversion functions
     ****************************************************/
    loader.RegisterFunction(
        ScalarFunction(
            "timeSpan",
            {TGEOMPOINT()},
            SpanTypes::TSTZSPAN(),
            TgeompointFunctions::Temporal_to_tstzspan
        )
    );

    /***************************************************
     * Transformation functions
     ****************************************************/
    
    loader.RegisterFunction(
        ScalarFunction(
            "tgeompointInst",
            {TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_to_tinstant
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tgeompointSeq",
            {TGEOMPOINT(), LogicalType::VARCHAR},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_to_tsequence
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tgeompointSeq",
            {TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_to_tsequence
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tgeompointSeqSet",
            {TGEOMPOINT(), LogicalType::VARCHAR},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_to_tsequenceset
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tgeompointSeqSet",
            {TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_to_tsequenceset
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "setInterp",
            {TGEOMPOINT(), LogicalType::VARCHAR},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_set_interp
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "appendInstant",
            {TGEOMPOINT(), TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_append_tinstant
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "appendSequence",
            {TGEOMPOINT(), TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_append_tsequence
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "merge",
            {TGEOMPOINT(), TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_merge
        )
     );

    loader.RegisterFunction(
        ScalarFunction(
            "merge",
            {LogicalType::LIST(TGEOMPOINT())},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_merge_array
        )
    );

    /* ***************************************************
    * Accessor functions
    ****************************************************/
    loader.RegisterFunction(
        ScalarFunction(
            "tempSubtype",
            {TGEOMPOINT()},
            LogicalType::VARCHAR,
            TemporalFunctions::Temporal_subtype
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "interp",
            {TGEOMPOINT()},
            LogicalType::VARCHAR,
            TemporalFunctions::Temporal_interp
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "getValue",
            {TGEOMPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::Tgeompoint_value
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "getTimestamp",
            {TGEOMPOINT()},
            LogicalType::TIMESTAMP_TZ,
            TemporalFunctions::Tinstant_timestamptz
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "valueSet",
            {TGEOMPOINT()},
            SpatialSetType::geomset(),
            TemporalFunctions::Temporal_valueset
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "valueN",
            // BIGINT to match the sibling `valueN(<other temporal>, BIGINT)`
            // overload in temporal.cpp and the C function's int64_t template
            // arg (BinaryExecutor<string_t,int64_t,string_t>). Registering
            // INTEGER here made DuckDB 1.4 reject the bind with
            // "Expected INT64, found INT32" (tgeompoint.test:482).
            {TGEOMPOINT(), LogicalType::BIGINT},
            GeoTypes::GEOMETRY(),
            TemporalFunctions::Temporal_value_n
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "getTime",
            {TGEOMPOINT()},
            SpansetTypes::tstzspanset(),
            TemporalFunctions::Temporal_time
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "startValue",
            {TGEOMPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::Tgeompoint_start_value
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "endValue",
            {TGEOMPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::Tgeompoint_end_value
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "duration",
            {TGEOMPOINT()},
            LogicalType::INTERVAL,
            TemporalFunctions::Temporal_duration
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "duration",
            {TGEOMPOINT(), LogicalType::BOOLEAN},
            LogicalType::INTERVAL,
            TemporalFunctions::Temporal_duration
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "memSize",
            {TGEOMPOINT()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_mem_size
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "lowerInc",
            {TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_lower_inc
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "upperInc",
            {TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_upper_inc
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "numInstants",
            {TGEOMPOINT()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_num_instants
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "startInstant",
            {TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_start_instant
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "endInstant",
            {TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_end_instant
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "instantN",
            {TGEOMPOINT(), LogicalType::INTEGER},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_instant_n
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "instants",
            {TGEOMPOINT()},
            LogicalType::LIST(TGEOMPOINT()),
            TemporalFunctions::Temporal_instants
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "numTimestamps",
            {TGEOMPOINT()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_num_timestamps
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "startTimestamp",
            {TGEOMPOINT()},
            LogicalType::TIMESTAMP_TZ,
            TemporalFunctions::Temporal_start_timestamptz
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "endTimestamp",
            {TGEOMPOINT()},
            LogicalType::TIMESTAMP_TZ,
            TemporalFunctions::Temporal_end_timestamptz
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "timestampN",
            {TGEOMPOINT(), LogicalType::INTEGER},
            LogicalType::TIMESTAMP_TZ,
            TemporalFunctions::Temporal_timestamptz_n
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "timestamps",
            {TGEOMPOINT()},
            LogicalType::LIST(LogicalType::TIMESTAMP_TZ),
            TemporalFunctions::Temporal_timestamps
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "numSequences",
            {TGEOMPOINT()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_num_sequences
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "startSequence",
            {TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_start_sequence
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "endSequence",
            {TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_end_sequence
        )
    );
    
    loader.RegisterFunction(
        ScalarFunction(
            "sequenceN",
            {TGEOMPOINT(), LogicalType::INTEGER},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_sequence_n
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "sequences",
            {TGEOMPOINT()},
            LogicalType::LIST(TGEOMPOINT()),
            TemporalFunctions::Temporal_sequences
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "segments",
            {TGEOMPOINT()},
            LogicalType::LIST(TGEOMPOINT()),
            TemporalFunctions::Temporal_segments
        )
    );

    /* ***************************************************
     * Shift and Scale functions
     ****************************************************/
    loader.RegisterFunction(
        ScalarFunction(
            "shiftTime",
            {TGEOMPOINT(), LogicalType::INTERVAL},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_shift_time
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "scaleTime",
            {TGEOMPOINT(), LogicalType::INTERVAL},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_scale_time
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "shiftScaleTime",
            {TGEOMPOINT(), LogicalType::INTERVAL, LogicalType::INTERVAL},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_shift_scale_time
        )
    );

    //TODO: unnest 

    /* ***************************************************
     * Restriction functions
     ****************************************************/

    loader.RegisterFunction(
        ScalarFunction(
            "atValues",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeompoint_at_value
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "minusValues",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_minus_value
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "atValues",
            {TGEOMPOINT(), SpatialSetType::geomset()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_at_values
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "minusValues",
            {TGEOMPOINT(), SpatialSetType::geomset()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_minus_value
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "atTime",
            {TGEOMPOINT(), LogicalType::TIMESTAMP_TZ},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_at_timestamptz
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "minusTime",
            {TGEOMPOINT(), LogicalType::TIMESTAMP_TZ},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_minus_timestamptz
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "valueAtTimestamp",
            {TGEOMPOINT(), LogicalType::TIMESTAMP_TZ},
            GeoTypes::GEOMETRY(),
            TemporalFunctions::Temporal_value_at_timestamptz
        )
    );
    
    loader.RegisterFunction(
        ScalarFunction(
            "atTime",
            {TGEOMPOINT(), SetTypes::tstzset()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_at_tstzset
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "minusTime",
            {TGEOMPOINT(), SetTypes::tstzset()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_minus_tstzset
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "atTime",
            {TGEOMPOINT(), SpanTypes::TSTZSPAN()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_at_tstzspan
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "minusTime",
            {TGEOMPOINT(), SpanTypes::TSTZSPAN()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_minus_tstzspan
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "atTime",
            {TGEOMPOINT(), SpansetTypes::tstzspanset()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_at_tstzspanset
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "minusTime",
            {TGEOMPOINT(), SpansetTypes::tstzspanset()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_minus_tstzspanset   
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "beforeTimestamp",
            {TGEOMPOINT(), LogicalType::TIMESTAMP_TZ},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_before_timestamptz
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "afterTimestamp",
            {TGEOMPOINT(), LogicalType::TIMESTAMP_TZ},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_after_timestamptz
        )
    );

    /* ***************************************************
     * Modification function
     ****************************************************/
    loader.RegisterFunction(
        ScalarFunction(
            "insert",
            {TGEOMPOINT(), TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_update
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "insert",
            {TGEOMPOINT(), TGEOMPOINT(), LogicalType::BOOLEAN},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_update
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "update",
            {TGEOMPOINT(), TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_update
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "update",
            {TGEOMPOINT(), TGEOMPOINT(), LogicalType::BOOLEAN},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_update
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "deleteTime",
            {TGEOMPOINT(), LogicalType::TIMESTAMP_TZ},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_delete_timestamptz
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "deleteTime",
            {TGEOMPOINT(), LogicalType::TIMESTAMP_TZ, LogicalType::BOOLEAN},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_delete_timestamptz
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "deleteTime",
            {TGEOMPOINT(), SetTypes::tstzset()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_delete_tstzset
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "deleteTime",
            {TGEOMPOINT(), SetTypes::tstzset(), LogicalType::BOOLEAN},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_delete_tstzset
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "deleteTime",
            {TGEOMPOINT(), SpanTypes::TSTZSPAN()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_delete_tstzspan
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "deleteTime",
            {TGEOMPOINT(), SpanTypes::TSTZSPAN(), LogicalType::BOOLEAN},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_delete_tstzspan
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "deleteTime",
            {TGEOMPOINT(), SpansetTypes::tstzspanset()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_delete_tstzspanset
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "deleteTime",
            {TGEOMPOINT(), SpansetTypes::tstzspanset(), LogicalType::BOOLEAN},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_delete_tstzspanset
        )
    );


    /* ***************************************************
     * Stops function
     ****************************************************/
    
    loader.RegisterFunction(
        ScalarFunction(
            "stops",
            {TGEOMPOINT(), LogicalType::DOUBLE, LogicalType::INTERVAL},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeompoint_stops
        )
    );

    /* ***************************************************
     * Comparison functions
     ****************************************************/
    loader.RegisterFunction(
        ScalarFunction(
            "temporal_eq",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_eq
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "temporal_ne",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_ne
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "temporal_lt",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_lt
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "temporal_le",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_le
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "temporal_gt",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_gt
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "temporal_ge",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_ge
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "temporal_cmp",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_cmp
        )
    );

        loader.RegisterFunction(
        ScalarFunction(
            "=",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_eq
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "<>",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_ne
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "<",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_lt
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "<=",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_le
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            ">",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_gt
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            ">=",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_ge
        )
    );

    /* ***************************************************
     * Spatial functions
     ****************************************************/
    loader.RegisterFunction(
        ScalarFunction(
            "getX",
            {TGEOMPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tpoint_get_x
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "getY",
            {TGEOMPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tpoint_get_y
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "getZ",
            {TGEOMPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tpoint_get_z
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "length",
            {TGEOMPOINT()},
            LogicalType::DOUBLE,
            TgeompointFunctions::Tpoint_length
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "cumulativeLength",
            {TGEOMPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tpoint_cumulative_length
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "speed",
            {TGEOMPOINT()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Temporal_derivative
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "twCentroid",
            {TGEOMPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::Tpoint_twcentroid
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "direction",
            {TGEOMPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tpoint_direction
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "azimuth",
            {TGEOMPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tpoint_azimuth
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "angularDifference",
            {TGEOMPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::Tpoint_angular_difference
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "isSimple",
            {TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Tpoint_is_simple
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "makeSimple",
            {TGEOMPOINT()},
            LogicalType::LIST(TGEOMPOINT()),
            TgeompointFunctions::Tpoint_make_simple
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "trajectory",
            {TGEOMPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::Tpoint_trajectory
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "trajectory_gs",
            {TGEOMPOINT()},
            LogicalType::BLOB,
            TgeompointFunctions::Tpoint_trajectory_gs
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "atGeometry",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeo_at_geom
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "minusGeometry",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeo_minus_geom
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "minusGeometry",
            {TGEOMPOINT(), GeoTypes::GEOMETRY(), SpanTypes::FLOATSPAN()},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeo_minus_geom
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "atStbox",
            {TGEOMPOINT(), StboxType::STBOX()},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeo_at_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "atStbox",
            {TGEOMPOINT(), StboxType::STBOX(), LogicalType::BOOLEAN},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeo_at_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "minusStbox",
            {TGEOMPOINT(), StboxType::STBOX()},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeo_minus_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "minusStbox",
            {TGEOMPOINT(), StboxType::STBOX(), LogicalType::BOOLEAN},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeo_minus_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "transform",
            {TGEOMPOINT(), LogicalType::INTEGER},
            TGEOMPOINT(),
            TgeompointFunctions::Tspatial_transform
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "round",
            {TGEOMPOINT(), LogicalType::INTEGER},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_round
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "round",
            {TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_round
        )
    );

    /* ***************************************************
     * Spatial relationships
     ****************************************************/
    loader.RegisterFunction(
        ScalarFunction(
            "eContains",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Econtains_geo_tgeo
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "aContains",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Acontains_geo_tgeo
        )
    );
    
     loader.RegisterFunction(
        ScalarFunction(
            "eDisjoint",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Edisjoint_geo_tgeo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "eDisjoint",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Edisjoint_tgeo_geo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "eDisjoint",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Edisjoint_tgeo_tgeo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "aDisjoint",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Adisjoint_geo_tgeo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "aDisjoint",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Adisjoint_tgeo_geo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "aDisjoint",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Adisjoint_tgeo_tgeo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "eIntersects",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Eintersects_tgeo_geo  
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "eIntersects",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Eintersects_geo_tgeo  
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "eIntersects",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Eintersects_tgeo_tgeo  
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "aIntersects",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Aintersects_tgeo_geo  
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "aIntersects",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Aintersects_geo_tgeo  
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "aIntersects",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Aintersects_tgeo_tgeo  
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "eTouches",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Etouches_geo_tpoint
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "eTouches",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Etouches_tpoint_geo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "aTouches",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Atouches_geo_tpoint
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "aTouches",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Atouches_tpoint_geo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "eDwithin",
            {TGEOMPOINT(), TGEOMPOINT(), LogicalType::DOUBLE},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Edwithin_tgeo_tgeo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "eDwithin",
            {GeoTypes::GEOMETRY(), TGEOMPOINT(), LogicalType::DOUBLE},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Edwithin_geo_tgeo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "eDwithin",
            {TGEOMPOINT(), GeoTypes::GEOMETRY(), LogicalType::DOUBLE},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Edwithin_tgeo_geo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "aDwithin",
            {GeoTypes::GEOMETRY(), TGEOMPOINT(), LogicalType::DOUBLE},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Adwithin_geo_tgeo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "aDwithin",
            {TGEOMPOINT(), GeoTypes::GEOMETRY(), LogicalType::DOUBLE},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Adwithin_tgeo_geo
        )
    );

     loader.RegisterFunction(
        ScalarFunction(
            "aDwithin",
            {TGEOMPOINT(), TGEOMPOINT(), LogicalType::DOUBLE},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Adwithin_tgeo_tgeo
        )
    );

    /* ***************************************************
     * Temporal-spatial relationships
     ****************************************************/
    loader.RegisterFunction(
        ScalarFunction(
            "tContains",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tcontains_geo_tgeo
        )
    );
    
    loader.RegisterFunction(
        ScalarFunction(
            "tContains",
            {GeoTypes::GEOMETRY(), TGEOMPOINT(), LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tcontains_geo_tgeo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tDisjoint",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdisjoint_tgeo_geo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tDisjoint",
            {TGEOMPOINT(), GeoTypes::GEOMETRY(), LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdisjoint_tgeo_geo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tDisjoint",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdisjoint_geo_tgeo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tDisjoint",
            {GeoTypes::GEOMETRY(), TGEOMPOINT(), LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdisjoint_geo_tgeo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tDisjoint",
            {TGEOMPOINT(), TGEOMPOINT(), LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdisjoint_tgeo_tgeo  
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tDisjoint",
            {TGEOMPOINT(), TGEOMPOINT()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdisjoint_tgeo_tgeo  
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tIntersects",
            {GeoTypes::GEOMETRY(), TGEOMPOINT(), LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tintersects_geo_tgeo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tIntersects",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tintersects_geo_tgeo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tIntersects",
            {TGEOMPOINT(), GeoTypes::GEOMETRY(), LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tintersects_tgeo_geo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tIntersects",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tintersects_tgeo_geo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tIntersects",
            {TGEOMPOINT(), TGEOMPOINT(), LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tintersects_tgeo_tgeo
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "tIntersects",
            {TGEOMPOINT(), TGEOMPOINT()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tintersects_tgeo_tgeo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tTouches",
            {GeoTypes::GEOMETRY(), TGEOMPOINT(), LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Ttouches_geo_tgeo
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "tTouches",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Ttouches_geo_tgeo
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "tTouches",
            {TGEOMPOINT(), GeoTypes::GEOMETRY(), LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Ttouches_tgeo_geo
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "tTouches",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Ttouches_tgeo_geo
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "tDwithin",
            {GeoTypes::GEOMETRY(), TGEOMPOINT(), LogicalType::DOUBLE},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdwithin_geo_tgeo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tDwithin",
            {GeoTypes::GEOMETRY(), TGEOMPOINT(), LogicalType::DOUBLE, LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdwithin_geo_tgeo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tDwithin",
            {TGEOMPOINT(), GeoTypes::GEOMETRY(), LogicalType::DOUBLE, LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdwithin_tgeo_geo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tDwithin",
            {TGEOMPOINT(), GeoTypes::GEOMETRY(), LogicalType::DOUBLE},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdwithin_tgeo_geo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tDwithin",
            {TGEOMPOINT(), TGEOMPOINT(), LogicalType::DOUBLE},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdwithin_tgeo_tgeo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tDwithin",
            {TGEOMPOINT(), TGEOMPOINT(), LogicalType::DOUBLE, LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdwithin_tgeo_tgeo
        )
    );
    

    /* ***************************************************
     * Operators (workaround as functions)
     ****************************************************/

    loader.RegisterFunction(
        ScalarFunction(
            "&&", // overlaps
            {TGEOMPOINT(), StboxType::STBOX()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Temporal_overlaps_tgeompoint_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "&&", // overlaps
            {TGEOMPOINT(), SpanTypes::TSTZSPAN()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Temporal_overlaps_tgeompoint_tstzspan
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "@>", // contains
            {TGEOMPOINT(), StboxType::STBOX()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Temporal_contains_tgeompoint_stbox
        )
    );

     /* ***************************************************
     * Distance functions
     ****************************************************/

    loader.RegisterFunction(
        ScalarFunction(
            "<->",
            {TGEOMPOINT(), TGEOMPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tdistance_tgeo_tgeo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "shortestLine",
            {TGEOMPOINT(), TGEOMPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::ShortestLine_tgeo_tgeo
        )
    );


    // ExtensionUtil::RegisterFunction(
    //     instance,
    //     ScalarFunction(
    //         "gs_as_text",
    //         {LogicalType::BLOB},
    //         LogicalType::VARCHAR,
    //         TgeompointFunctions::gs_as_text
    //     )
    // );

    loader.RegisterFunction(
        ScalarFunction(
            "collect_gs",
            {LogicalType::LIST(LogicalType::BLOB)},
            LogicalType::BLOB,
            TgeompointFunctions::collect_gs
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "distance_gs",
            {LogicalType::BLOB, LogicalType::BLOB},
            LogicalType::DOUBLE,
            TgeompointFunctions::distance_geo_geo
        )
    );
}

/* ***************************************************
 * asMVTGeom + geoMeasure for tgeompoint
 *
 *   asMVTGeom(tgeompoint, stbox bounds[, extent int[, buffer int[, clip bool]]])
 *     RETURNS STRUCT(geom geometry, times bigint[])
 *
 *   geoMeasure(tgeompoint, tfloat measure[, segmentize boolean])
 *     RETURNS geometry
 ****************************************************/

namespace {

inline Temporal *BlobToTempMVT(string_t b) {
    size_t sz = b.GetSize();
    uint8_t *copy = (uint8_t *)malloc(sz);
    memcpy(copy, b.GetData(), sz);
    return reinterpret_cast<Temporal *>(copy);
}

inline STBox *BlobToStboxMVT(string_t b) {
    size_t sz = b.GetSize();
    uint8_t *copy = (uint8_t *)malloc(sz);
    memcpy(copy, b.GetData(), sz);
    return reinterpret_cast<STBox *>(copy);
}

void TgeoAsMVTGeomExec(DataChunk &args, ExpressionState &state, Vector &result) {
    const idx_t row_count = args.size();
    for (idx_t i = 0; i < args.ColumnCount(); i++) args.data[i].Flatten(row_count);
    const idx_t cc = args.ColumnCount();

    auto in_temp   = FlatVector::GetData<string_t>(args.data[0]);
    auto in_bounds = FlatVector::GetData<string_t>(args.data[1]);
    auto &v0 = FlatVector::Validity(args.data[0]);
    auto &v1 = FlatVector::Validity(args.data[1]);
    auto &out_validity = FlatVector::Validity(result);

    auto &struct_children = StructVector::GetEntries(result);
    auto &geom_col = *struct_children[0];
    auto &times_col = *struct_children[1];
    auto times_entries = FlatVector::GetData<list_entry_t>(times_col);
    auto &times_child = ListVector::GetEntry(times_col);

    idx_t total_times = 0;
    for (idx_t row = 0; row < row_count; row++) {
        if (!v0.RowIsValid(row) || !v1.RowIsValid(row)) {
            out_validity.SetInvalid(row);
            times_entries[row] = list_entry_t{total_times, 0};
            continue;
        }
        Temporal *t = BlobToTempMVT(in_temp[row]);
        STBox *bx = BlobToStboxMVT(in_bounds[row]);
        int32_t extent = 4096;
        int32_t buffer = 256;
        bool clip = true;
        if (cc > 2) extent = FlatVector::GetData<int32_t>(args.data[2])[row];
        if (cc > 3) buffer = FlatVector::GetData<int32_t>(args.data[3])[row];
        if (cc > 4) clip   = FlatVector::GetData<bool>(args.data[4])[row];

        GSERIALIZED *geom = nullptr;
        int64 *times = nullptr;
        int count = 0;
        bool found = tpoint_as_mvtgeom(t, bx, extent, buffer, clip, &geom, &times, &count);
        free(t); free(bx);
        if (!found || !geom) {
            out_validity.SetInvalid(row);
            times_entries[row] = list_entry_t{total_times, 0};
            if (geom) free(geom);
            if (times) free(times);
            continue;
        }
        /* Encode geom into the geometry struct child */
        ArenaAllocator arena(BufferAllocator::Get(state.GetContext()));
        string_t enc = GSerializedToGeometry(geom, arena, geom_col);
        FlatVector::GetData<string_t>(geom_col)[row] =
            StringVector::AddStringOrBlob(geom_col, enc);
        free(geom);
        /* Encode times[] into the bigint[] struct child */
        if (count > 0 && times) {
            ListVector::Reserve(times_col, total_times + count);
            ListVector::SetListSize(times_col, total_times + count);
            times_entries[row] = list_entry_t{total_times, static_cast<uint64_t>(count)};
            auto times_data = FlatVector::GetData<int64_t>(times_child);
            for (int k = 0; k < count; k++) {
                times_data[total_times + k] = (int64_t) times[k];
            }
            total_times += count;
        } else {
            times_entries[row] = list_entry_t{total_times, 0};
        }
        if (times) free(times);
    }
    if (row_count == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

void TgeoGeoMeasureExec(DataChunk &args, ExpressionState &state, Vector &result) {
    const idx_t row_count = args.size();
    for (idx_t i = 0; i < args.ColumnCount(); i++) args.data[i].Flatten(row_count);
    const idx_t cc = args.ColumnCount();
    auto in_temp = FlatVector::GetData<string_t>(args.data[0]);
    auto in_meas = FlatVector::GetData<string_t>(args.data[1]);
    auto &v0 = FlatVector::Validity(args.data[0]);
    auto &v1 = FlatVector::Validity(args.data[1]);
    auto out_data = FlatVector::GetData<string_t>(result);
    auto &out_validity = FlatVector::Validity(result);

    for (idx_t row = 0; row < row_count; row++) {
        if (!v0.RowIsValid(row) || !v1.RowIsValid(row)) {
            out_validity.SetInvalid(row);
            continue;
        }
        Temporal *t = BlobToTempMVT(in_temp[row]);
        Temporal *m = BlobToTempMVT(in_meas[row]);
        bool segmentize = (cc > 2) ? FlatVector::GetData<bool>(args.data[2])[row] : false;
        GSERIALIZED *geom = nullptr;
        bool ok = tpoint_tfloat_to_geomeas(t, m, segmentize, &geom);
        free(t); free(m);
        if (!ok || !geom) {
            out_validity.SetInvalid(row);
            if (geom) free(geom);
            continue;
        }
        ArenaAllocator arena(BufferAllocator::Get(state.GetContext()));
        string_t enc = GSerializedToGeometry(geom, arena, result);
        out_data[row] = StringVector::AddStringOrBlob(result, enc);
        free(geom);
    }
    if (row_count == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

} // namespace

void TgeompointType::RegisterAnalyticsViz(ExtensionLoader &loader) {
    const auto T  = TGEOMPOINT();
    const auto B  = StboxType::STBOX();
    const auto G  = GeoTypes::GEOMETRY();
    const auto I  = LogicalType::INTEGER;
    const auto BL = LogicalType::BOOLEAN;

    /* asMVTGeom returns STRUCT(geom GEOMETRY, times BIGINT[]) */
    const auto MVT_OUT = LogicalType::STRUCT({
        {"geom", G},
        {"times", LogicalType::LIST(LogicalType::BIGINT)},
    });
    loader.RegisterFunction(ScalarFunction("asMVTGeom", {T, B},                MVT_OUT, TgeoAsMVTGeomExec));
    loader.RegisterFunction(ScalarFunction("asMVTGeom", {T, B, I},             MVT_OUT, TgeoAsMVTGeomExec));
    loader.RegisterFunction(ScalarFunction("asMVTGeom", {T, B, I, I},          MVT_OUT, TgeoAsMVTGeomExec));
    loader.RegisterFunction(ScalarFunction("asMVTGeom", {T, B, I, I, BL},      MVT_OUT, TgeoAsMVTGeomExec));

    /* geoMeasure(tgeompoint, tfloat[, segmentize]) -> geometry */
    loader.RegisterFunction(ScalarFunction("geoMeasure", {T, TemporalTypes::TFLOAT()},     G, TgeoGeoMeasureExec));
    loader.RegisterFunction(ScalarFunction("geoMeasure", {T, TemporalTypes::TFLOAT(), BL}, G, TgeoGeoMeasureExec));
}

} // namespace duckdb