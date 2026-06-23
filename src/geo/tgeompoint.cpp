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
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "spatial/spatial_types.hpp"
#include "mobilityduck/meos_exec_serial.hpp"
#include "geo_util.hpp"
#include "time_util.hpp"

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
    RegisterMeosCastFunction(loader, 
        LogicalType::VARCHAR,
        TGEOMPOINT(),
        TgeompointFunctions::Tpoint_in
    );

    RegisterMeosCastFunction(loader, 
        TGEOMPOINT(),
        LogicalType::VARCHAR,
        TemporalFunctions::Temporal_out
    );

    RegisterMeosCastFunction(loader, 
        TGEOMPOINT(),
        StboxType::STBOX(),
        TgeompointFunctions::Tspatial_to_stbox_cast
    );

    RegisterMeosCastFunction(loader, 
        TGEOMPOINT(),
        SpanTypes::TSTZSPAN(),
        TgeompointFunctions::Temporal_to_tstzspan_cast
    );
}

void TgeompointType::RegisterScalarFunctions(ExtensionLoader &loader) {

    /* ***************************************************
     * In/out functions
     ****************************************************/

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asText",
            {TGEOMPOINT()},
            LogicalType::VARCHAR,
            TgeompointFunctions::Tspatial_as_text
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asEWKT",
            {TGEOMPOINT()},
            LogicalType::VARCHAR,
            TgeompointFunctions::Tspatial_as_ewkt
        )
    );

    const auto varchar_list = LogicalType::LIST(LogicalType::VARCHAR);

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asText",
            {LogicalType::LIST(TGEOMPOINT())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asText",
            {LogicalType::LIST(TGEOMPOINT()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asText",
            {LogicalType::LIST(GeoTypes::GEOMETRY())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asText",
            {LogicalType::LIST(GeoTypes::GEOMETRY()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(TGEOMPOINT())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(TGEOMPOINT()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(GeoTypes::GEOMETRY())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
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
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "TGEOMPOINT",
            {GeoTypes::GEOMETRY(), LogicalType::TIMESTAMP_TZ},
            TGEOMPOINT(),
            TgeompointFunctions::Tpointinst_constructor
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "TGEOMPOINT",
            {GeoTypes::GEOMETRY(), LogicalType::TIMESTAMP_TZ, LogicalType::INTEGER}, // with SRID
            TGEOMPOINT(),
            TgeompointFunctions::Tpointinst_constructor
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompoint",
            {GeoTypes::GEOMETRY(), SetTypes::tstzset()},
            TGEOMPOINT(),
            TemporalFunctions::Tsequence_from_base_tstzset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompoint",
            {GeoTypes::GEOMETRY(), SpanTypes::TSTZSPAN()},
            TGEOMPOINT(),
            TemporalFunctions::Tsequence_from_base_tstzspan
        )
    );

     duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompoint",
            {GeoTypes::GEOMETRY(), SpanTypes::TSTZSPAN(), LogicalType::VARCHAR},
            TGEOMPOINT(),
            TemporalFunctions::Tsequence_from_base_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompoint",
            {GeoTypes::GEOMETRY(), SpansetTypes::tstzspanset()},
            TGEOMPOINT(),
            TemporalFunctions::Tsequenceset_from_base_tstzspanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompoint",
            {GeoTypes::GEOMETRY(), SpansetTypes::tstzspanset(), LogicalType::VARCHAR},
            TGEOMPOINT(),
            TemporalFunctions::Tsequenceset_from_base_tstzspanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompointSeq",
            {LogicalType::LIST(TGEOMPOINT())},
            TGEOMPOINT(),
            // TemporalFunctions::Tsequence_constructor
            TgeompointFunctions::Tgeompoint_sequence_constructor
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompointSeqSet",
            {LogicalType::LIST(TGEOMPOINT())},
            TGEOMPOINT(),
            TemporalFunctions::Tsequenceset_constructor
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
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
    duckdb::RegisterSerializedScalarFunction(loader, 
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
    
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompointInst",
            {TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_to_tinstant
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompointSeq",
            {TGEOMPOINT(), LogicalType::VARCHAR},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_to_tsequence
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompointSeq",
            {TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_to_tsequence
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompointSeqSet",
            {TGEOMPOINT(), LogicalType::VARCHAR},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_to_tsequenceset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompointSeqSet",
            {TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_to_tsequenceset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "setInterp",
            {TGEOMPOINT(), LogicalType::VARCHAR},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_set_interp
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "appendInstant",
            {TGEOMPOINT(), TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_append_tinstant
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "appendSequence",
            {TGEOMPOINT(), TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_append_tsequence
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "merge",
            {TGEOMPOINT(), TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_merge
        )
     );

    duckdb::RegisterSerializedScalarFunction(loader, 
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
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tempSubtype",
            {TGEOMPOINT()},
            LogicalType::VARCHAR,
            TemporalFunctions::Temporal_subtype
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "interp",
            {TGEOMPOINT()},
            LogicalType::VARCHAR,
            TemporalFunctions::Temporal_interp
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "getValue",
            {TGEOMPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::Tgeompoint_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "getTimestamp",
            {TGEOMPOINT()},
            LogicalType::TIMESTAMP_TZ,
            TemporalFunctions::Tinstant_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "valueSet",
            {TGEOMPOINT()},
            SpatialSetType::geomset(),
            TemporalFunctions::Temporal_valueset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
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

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "getTime",
            {TGEOMPOINT()},
            SpansetTypes::tstzspanset(),
            TemporalFunctions::Temporal_time
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "startValue",
            {TGEOMPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::Tgeompoint_start_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "endValue",
            {TGEOMPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::Tgeompoint_end_value
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "duration",
            {TGEOMPOINT()},
            LogicalType::INTERVAL,
            TemporalFunctions::Temporal_duration
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "duration",
            {TGEOMPOINT(), LogicalType::BOOLEAN},
            LogicalType::INTERVAL,
            TemporalFunctions::Temporal_duration
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "memSize",
            {TGEOMPOINT()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_mem_size
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "lowerInc",
            {TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_lower_inc
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "upperInc",
            {TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_upper_inc
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "numInstants",
            {TGEOMPOINT()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_num_instants
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "startInstant",
            {TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_start_instant
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "endInstant",
            {TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_end_instant
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "instantN",
            {TGEOMPOINT(), LogicalType::INTEGER},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_instant_n
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "instants",
            {TGEOMPOINT()},
            LogicalType::LIST(TGEOMPOINT()),
            TemporalFunctions::Temporal_instants
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "numTimestamps",
            {TGEOMPOINT()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_num_timestamps
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "startTimestamp",
            {TGEOMPOINT()},
            LogicalType::TIMESTAMP_TZ,
            TemporalFunctions::Temporal_start_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "endTimestamp",
            {TGEOMPOINT()},
            LogicalType::TIMESTAMP_TZ,
            TemporalFunctions::Temporal_end_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "timestampN",
            {TGEOMPOINT(), LogicalType::INTEGER},
            LogicalType::TIMESTAMP_TZ,
            TemporalFunctions::Temporal_timestamptz_n
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "timestamps",
            {TGEOMPOINT()},
            LogicalType::LIST(LogicalType::TIMESTAMP_TZ),
            TemporalFunctions::Temporal_timestamps
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "numSequences",
            {TGEOMPOINT()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_num_sequences
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "startSequence",
            {TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_start_sequence
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "endSequence",
            {TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_end_sequence
        )
    );
    
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "sequenceN",
            {TGEOMPOINT(), LogicalType::INTEGER},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_sequence_n
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "sequences",
            {TGEOMPOINT()},
            LogicalType::LIST(TGEOMPOINT()),
            TemporalFunctions::Temporal_sequences
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
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
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "shiftTime",
            {TGEOMPOINT(), LogicalType::INTERVAL},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_shift_time
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "scaleTime",
            {TGEOMPOINT(), LogicalType::INTERVAL},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_scale_time
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
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

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atValues",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeompoint_at_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusValues",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_minus_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atValues",
            {TGEOMPOINT(), SpatialSetType::geomset()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_at_values
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusValues",
            {TGEOMPOINT(), SpatialSetType::geomset()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_minus_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atTime",
            {TGEOMPOINT(), LogicalType::TIMESTAMP_TZ},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_at_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusTime",
            {TGEOMPOINT(), LogicalType::TIMESTAMP_TZ},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_minus_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "valueAtTimestamp",
            {TGEOMPOINT(), LogicalType::TIMESTAMP_TZ},
            GeoTypes::GEOMETRY(),
            TemporalFunctions::Temporal_value_at_timestamptz
        )
    );
    
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atTime",
            {TGEOMPOINT(), SetTypes::tstzset()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_at_tstzset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusTime",
            {TGEOMPOINT(), SetTypes::tstzset()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_minus_tstzset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atTime",
            {TGEOMPOINT(), SpanTypes::TSTZSPAN()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_at_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusTime",
            {TGEOMPOINT(), SpanTypes::TSTZSPAN()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_minus_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atTime",
            {TGEOMPOINT(), SpansetTypes::tstzspanset()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_at_tstzspanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusTime",
            {TGEOMPOINT(), SpansetTypes::tstzspanset()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_minus_tstzspanset   
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "beforeTimestamp",
            {TGEOMPOINT(), LogicalType::TIMESTAMP_TZ},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_before_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
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
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "insert",
            {TGEOMPOINT(), TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_update
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "insert",
            {TGEOMPOINT(), TGEOMPOINT(), LogicalType::BOOLEAN},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_update
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "update",
            {TGEOMPOINT(), TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_update
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "update",
            {TGEOMPOINT(), TGEOMPOINT(), LogicalType::BOOLEAN},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_update
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "deleteTime",
            {TGEOMPOINT(), LogicalType::TIMESTAMP_TZ},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_delete_timestamptz
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "deleteTime",
            {TGEOMPOINT(), LogicalType::TIMESTAMP_TZ, LogicalType::BOOLEAN},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_delete_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "deleteTime",
            {TGEOMPOINT(), SetTypes::tstzset()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_delete_tstzset
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "deleteTime",
            {TGEOMPOINT(), SetTypes::tstzset(), LogicalType::BOOLEAN},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_delete_tstzset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "deleteTime",
            {TGEOMPOINT(), SpanTypes::TSTZSPAN()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_delete_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "deleteTime",
            {TGEOMPOINT(), SpanTypes::TSTZSPAN(), LogicalType::BOOLEAN},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_delete_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "deleteTime",
            {TGEOMPOINT(), SpansetTypes::tstzspanset()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_delete_tstzspanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
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
    
    duckdb::RegisterSerializedScalarFunction(loader, 
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
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "temporal_eq",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_eq
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "temporal_ne",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_ne
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "temporal_lt",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_lt
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "temporal_le",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_le
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "temporal_gt",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_gt
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "temporal_ge",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_ge
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "temporal_cmp",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_cmp
        )
    );

        duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "=",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_eq
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<>",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_ne
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_lt
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<=",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_le
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            ">",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_gt
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
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
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "getX",
            {TGEOMPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tpoint_get_x
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "getY",
            {TGEOMPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tpoint_get_y
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "getZ",
            {TGEOMPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tpoint_get_z
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "length",
            {TGEOMPOINT()},
            LogicalType::DOUBLE,
            TgeompointFunctions::Tpoint_length
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "cumulativeLength",
            {TGEOMPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tpoint_cumulative_length
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "speed",
            {TGEOMPOINT()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Temporal_derivative
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "twCentroid",
            {TGEOMPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::Tpoint_twcentroid
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "direction",
            {TGEOMPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tpoint_direction
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "azimuth",
            {TGEOMPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tpoint_azimuth
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "angularDifference",
            {TGEOMPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::Tpoint_angular_difference
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "isSimple",
            {TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Tpoint_is_simple
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "makeSimple",
            {TGEOMPOINT()},
            LogicalType::LIST(TGEOMPOINT()),
            TgeompointFunctions::Tpoint_make_simple
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "trajectory",
            {TGEOMPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::Tpoint_trajectory
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "trajectory_gs",
            {TGEOMPOINT()},
            LogicalType::BLOB,
            TgeompointFunctions::Tpoint_trajectory_gs
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atGeometry",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeo_at_geom
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusGeometry",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeo_minus_geom
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusGeometry",
            {TGEOMPOINT(), GeoTypes::GEOMETRY(), SpanTypes::FLOATSPAN()},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeo_minus_geom
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atStbox",
            {TGEOMPOINT(), StboxType::STBOX()},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeo_at_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atStbox",
            {TGEOMPOINT(), StboxType::STBOX(), LogicalType::BOOLEAN},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeo_at_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusStbox",
            {TGEOMPOINT(), StboxType::STBOX()},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeo_minus_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusStbox",
            {TGEOMPOINT(), StboxType::STBOX(), LogicalType::BOOLEAN},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeo_minus_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "transform",
            {TGEOMPOINT(), LogicalType::INTEGER},
            TGEOMPOINT(),
            TgeompointFunctions::Tspatial_transform
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "round",
            {TGEOMPOINT(), LogicalType::INTEGER},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_round
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
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
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "eContains",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Econtains_geo_tgeo
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "aContains",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Acontains_geo_tgeo
        )
    );
    
     duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "eDisjoint",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Edisjoint_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "eDisjoint",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Edisjoint_tgeo_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "eDisjoint",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Edisjoint_tgeo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "aDisjoint",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Adisjoint_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "aDisjoint",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Adisjoint_tgeo_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "aDisjoint",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Adisjoint_tgeo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "eIntersects",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Eintersects_tgeo_geo  
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "eIntersects",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Eintersects_geo_tgeo  
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "eIntersects",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Eintersects_tgeo_tgeo  
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "aIntersects",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Aintersects_tgeo_geo  
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "aIntersects",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Aintersects_geo_tgeo  
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "aIntersects",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Aintersects_tgeo_tgeo  
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "eTouches",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Etouches_geo_tpoint
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "eTouches",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Etouches_tpoint_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "aTouches",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Atouches_geo_tpoint
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "aTouches",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Atouches_tpoint_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "eDwithin",
            {TGEOMPOINT(), TGEOMPOINT(), LogicalType::DOUBLE},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Edwithin_tgeo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "eDwithin",
            {GeoTypes::GEOMETRY(), TGEOMPOINT(), LogicalType::DOUBLE},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Edwithin_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "eDwithin",
            {TGEOMPOINT(), GeoTypes::GEOMETRY(), LogicalType::DOUBLE},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Edwithin_tgeo_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "aDwithin",
            {GeoTypes::GEOMETRY(), TGEOMPOINT(), LogicalType::DOUBLE},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Adwithin_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "aDwithin",
            {TGEOMPOINT(), GeoTypes::GEOMETRY(), LogicalType::DOUBLE},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Adwithin_tgeo_geo
        )
    );

     duckdb::RegisterSerializedScalarFunction(loader, 
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
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tContains",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tcontains_geo_tgeo
        )
    );
    
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tDisjoint",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdisjoint_tgeo_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tDisjoint",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdisjoint_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tDisjoint",
            {TGEOMPOINT(), TGEOMPOINT()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdisjoint_tgeo_tgeo  
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tIntersects",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tintersects_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tIntersects",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tintersects_tgeo_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tIntersects",
            {TGEOMPOINT(), TGEOMPOINT()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tintersects_tgeo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tTouches",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Ttouches_geo_tgeo
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tTouches",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Ttouches_tgeo_geo
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tDwithin",
            {GeoTypes::GEOMETRY(), TGEOMPOINT(), LogicalType::DOUBLE},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdwithin_geo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tDwithin",
            {TGEOMPOINT(), GeoTypes::GEOMETRY(), LogicalType::DOUBLE},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdwithin_tgeo_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tDwithin",
            {TGEOMPOINT(), TGEOMPOINT(), LogicalType::DOUBLE},
            TemporalTypes::TBOOL(),
            TgeompointFunctions::Tdwithin_tgeo_tgeo
        )
    );

    

    /* ***************************************************
     * Operators (workaround as functions)
     ****************************************************/

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "&&", // overlaps
            {TGEOMPOINT(), StboxType::STBOX()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Temporal_overlaps_tgeompoint_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "&&", // overlaps
            {TGEOMPOINT(), SpanTypes::TSTZSPAN()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Temporal_overlaps_tgeompoint_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
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

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<->",
            {TGEOMPOINT(), TGEOMPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tdistance_tgeo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
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

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "collect_gs",
            {LogicalType::LIST(LogicalType::BLOB)},
            LogicalType::BLOB,
            TgeompointFunctions::collect_gs
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "distance_gs",
            {LogicalType::BLOB, LogicalType::BLOB},
            LogicalType::DOUBLE,
            TgeompointFunctions::distance_geo_geo
        )
    );

    /* ***************************************************
     * Spatial comparison predicates — ever/always + temporal_t*
     * on tgeompoint × {geometry, tgeompoint}
     ****************************************************/
    {
        const auto T = TGEOMPOINT();
        const auto G = GeoTypes::GEOMETRY();

#define REG_SPATIAL_EA(NAME, FN)                                                                                                                  \
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {G, T}, LogicalType::BOOLEAN, TgeompointFunctions::FN##_geo_tgeo)); \
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {T, G}, LogicalType::BOOLEAN, TgeompointFunctions::FN##_tgeo_geo)); \
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {T, T}, LogicalType::BOOLEAN, TgeompointFunctions::FN##_tgeo_tgeo));

        REG_SPATIAL_EA("ever_eq",   Ever_eq)
        REG_SPATIAL_EA("always_eq", Always_eq)
        REG_SPATIAL_EA("ever_ne",   Ever_ne)
        REG_SPATIAL_EA("always_ne", Always_ne)
#undef REG_SPATIAL_EA

#define REG_SPATIAL_TCMP(NAME, FN)                                                                                                                \
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {G, T}, TemporalTypes::TBOOL(), TgeompointFunctions::FN##_geo_tgeo)); \
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {T, G}, TemporalTypes::TBOOL(), TgeompointFunctions::FN##_tgeo_geo)); \
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {T, T}, TemporalTypes::TBOOL(), TgeompointFunctions::FN##_tgeo_tgeo));

        REG_SPATIAL_TCMP("temporal_teq", Teq)
        REG_SPATIAL_TCMP("temporal_tne", Tne)
        // Portable-SQL aliases (MobilityDB names): tgeo_teq / tgeo_tne
        REG_SPATIAL_TCMP("tgeo_teq", Teq)
        REG_SPATIAL_TCMP("tgeo_tne", Tne)
#undef REG_SPATIAL_TCMP
    }

    /* tdistance named form (mirrors the <-> operator) */
    {
        const auto TG = TGEOMPOINT();
        const auto G  = GeoTypes::GEOMETRY();
        const auto TF = TemporalTypes::TFLOAT();
        const auto D  = LogicalType::DOUBLE;

        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tdistance", {TG, TG}, TF, TgeompointFunctions::Tdistance_named));

        /* nearestApproachInstant */
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("nearestApproachInstant", {TG, G}, TG, TgeompointFunctions::Nai_tgeo_geo));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("nearestApproachInstant", {G, TG}, TG, TgeompointFunctions::Nai_geo_tgeo));

        /* nearestApproachDistance */
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("nearestApproachDistance", {TG, G}, D, TgeompointFunctions::Nad_tgeo_geo));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("nearestApproachDistance", {G, TG}, D, TgeompointFunctions::Nad_geo_tgeo));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("nearestApproachDistance", {TG, TG}, D, TgeompointFunctions::Nad_tgeo_tgeo));

        /* nad — alias */
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("nad", {TG, G}, D, TgeompointFunctions::Nad_tgeo_geo));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("nad", {G, TG}, D, TgeompointFunctions::Nad_geo_tgeo));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("nad", {TG, TG}, D, TgeompointFunctions::Nad_tgeo_tgeo));

        /* affine (12-arg and 6-arg) */
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("affine",
            {TG, D, D, D, D, D, D, D, D, D, D, D, D}, TG,
            TgeompointFunctions::Tgeo_affine_12));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("affine",
            {TG, D, D, D, D, D, D}, TG,
            TgeompointFunctions::Tgeo_affine_6));

        /* translate */
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("translate", {TG, D, D, D}, TG, TgeompointFunctions::Tgeo_translate_3d));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("translate", {TG, D, D},    TG, TgeompointFunctions::Tgeo_translate_2d));

        /* rotate */
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("rotate", {TG, D},       TG, TgeompointFunctions::Tgeo_rotate_angle));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("rotate", {TG, D, D, D}, TG, TgeompointFunctions::Tgeo_rotate_angle_cx_cy));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("rotate", {TG, D, G},    TG, TgeompointFunctions::Tgeo_rotate_geom));

        /* rotateZ / rotateX / rotateY */
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("rotateZ", {TG, D}, TG, TgeompointFunctions::Tgeo_rotate_angle));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("rotateX", {TG, D}, TG, TgeompointFunctions::Tgeo_rotateX));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("rotateY", {TG, D}, TG, TgeompointFunctions::Tgeo_rotateY));

        /* transscale */
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("transscale", {TG, D, D, D, D}, TG, TgeompointFunctions::Tgeo_transscale));

        /* scale */
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("scale", {TG, G},    TG, TgeompointFunctions::Tgeo_scale_geom));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("scale", {TG, G, G}, TG, TgeompointFunctions::Tgeo_scale_geom_origin));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("scale", {TG, D, D},    TG, TgeompointFunctions::Tgeo_scale_xy));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("scale", {TG, D, D, D}, TG, TgeompointFunctions::Tgeo_scale_xyz));
    }
}

/* ***************************************************
 * Round-trip I/O for tgeompoint: asEWKB / asHexWKB / asHexEWKB /
 * asMFJSON and the matching tgeompointFromText / FromBinary / FromEWKB /
 * FromHexWKB / FromHexEWKB / FromMFJSON constructors.
 *
 * spaceSplit / spaceTimeSplit — set-returning splitters that bucket a
 * tgeompoint trajectory into spatial (and optionally temporal) bins and
 * return one row per bin, mirroring MobilityDB's
 *
 *   RETURNS TABLE(spaceBin geometry, [timeBin timestamptz,] tpoint tgeompoint)
 *
 * asMVTGeom + geoMeasure for tgeompoint
 *
 *   asMVTGeom(tgeompoint, stbox bounds[, extent int[, buffer int[, clip bool]]])
 *     RETURNS STRUCT(geom geometry, times bigint[])
 *
 *   geoMeasure(tgeompoint, tfloat measure[, segmentize boolean])
 *     RETURNS geometry
 ****************************************************/

namespace {

/* MEOS WKB variant flag from meos_geo.h: 0 = base, 0x04 = extended (with SRID). */
constexpr uint8_t WKB_BASE = 0x00;
/* WKB_EXTENDED is provided by meos_geo.h as #define WKB_EXTENDED 0x04 */

inline Temporal *BlobToTemp(string_t b) {
    size_t sz = b.GetSize();
    uint8_t *copy = (uint8_t *)malloc(sz);
    memcpy(copy, b.GetData(), sz);
    return reinterpret_cast<Temporal *>(copy);
}

inline Temporal *BlobToTempMVT(string_t b) {
    size_t sz = b.GetSize();
    uint8_t *copy = (uint8_t *)malloc(sz);
    memcpy(copy, b.GetData(), sz);
    return reinterpret_cast<Temporal *>(copy);
}

string_t TempToResultBlob(Vector &result, Temporal *t) {
    size_t sz = temporal_mem_size(t);
    string_t blob(reinterpret_cast<const char *>(t), sz);
    return StringVector::AddStringOrBlob(result, blob);
}

void TgeoAsWkbExec(DataChunk &args, ExpressionState &state, Vector &result, uint8_t variant) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            Temporal *t = BlobToTemp(input);
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

void TgeoAsHexWkbExec(DataChunk &args, ExpressionState &state, Vector &result, uint8_t variant) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            Temporal *t = BlobToTemp(input);
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

void TgeoFromWkbExec(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            if (input.GetSize() == 0) throw InvalidInputException("Empty WKB input");
            uint8_t *wkb = (uint8_t *)malloc(input.GetSize());
            memcpy(wkb, input.GetData(), input.GetSize());
            Temporal *t = temporal_from_wkb(wkb, input.GetSize());
            free(wkb);
            if (!t) throw InvalidInputException("temporal_from_wkb: invalid WKB");
            string_t stored = TempToResultBlob(result, t);
            free(t);
            return stored;
        });
}

void TgeoFromHexWkbExec(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string hex(input.GetData(), input.GetSize());
            Temporal *t = temporal_from_hexwkb(hex.c_str());
            if (!t) throw InvalidInputException("temporal_from_hexwkb: invalid hex-WKB");
            string_t stored = TempToResultBlob(result, t);
            free(t);
            return stored;
        });
}

void TgeoAsMfjsonExec(DataChunk &args, ExpressionState &state, Vector &result) {
    /* asMFJSON(tgeompoint[, with_bbox[, flags[, precision[, srs]]]]).
     * MobilityDB defaults: with_bbox=false, flags=0, precision=15, srs=NULL. */
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
        Temporal *t = BlobToTemp(in[row]);
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

        MvtGeom mvt = tpoint_as_mvtgeom(t, bx, extent, buffer, clip);
        free(t); free(bx);
        GSERIALIZED *geom = mvt.geom;
        int64 *times = mvt.times;
        int count = mvt.count;
        if (!geom) {
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

void TgeoFromMfjsonExec(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string s(input.GetData(), input.GetSize());
            Temporal *t = tgeompoint_from_mfjson(s.c_str());
            if (!t) throw InvalidInputException("tgeompoint_from_mfjson: invalid MFJSON");
            string_t stored = TempToResultBlob(result, t);
            free(t);
            return stored;
        });
}

void TgeoFromTextExec(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string s(input.GetData(), input.GetSize());
            Temporal *t = tgeompoint_in(s.c_str());
            if (!t) throw InvalidInputException("tgeompoint_in: invalid text");
            string_t stored = TempToResultBlob(result, t);
            free(t);
            return stored;
        });
}

} // anonymous namespace for round-trip WKB

namespace {

struct SpaceSplitBindData : public TableFunctionData {
    string_t blob;
    double xsize, ysize, zsize;
    string_t sorigin_blob;   // empty -> default Point(0 0 0)
    bool has_sorigin;
    bool bitmatrix;
    bool has_duration;
    interval_t duration;
    bool has_torigin;
    timestamp_tz_t torigin;
};

struct SpaceSplitGlobalState : public GlobalTableFunctionState {
    idx_t idx = 0;
    /* space_bins[i] is the raw EWKB serialisation of the i-th spatial bin
     * (GSERIALIZED -> EWKB at Init time, decoded into the result vector's
     * geometry format at Exec time using DuckDB-spatial's wkb_reader).
     * tpoint blobs are pre-built TGEOMPOINT-aliased BLOB values.
     * time_bin is populated only by the spaceTimeSplit overload. */
    std::vector<std::vector<uint8_t>> space_ewkb;
    std::vector<Value> time_bin;
    std::vector<Value> tpoint;
};

GSERIALIZED *DefaultOriginSplit() {
    return geompoint_make3dz(0, 0.0, 0.0, 0.0);
}

unique_ptr<FunctionData> SpaceSplitBindCommon(ClientContext &context,
                                              TableFunctionBindInput &input,
                                              vector<LogicalType> &return_types,
                                              vector<string> &names,
                                              bool with_time) {
    if (input.inputs.empty() || input.inputs[0].IsNull()) {
        throw BinderException("spaceSplit: tgeompoint input must be non-null");
    }

    auto bd = make_uniq<SpaceSplitBindData>();
    bd->blob = StringValue::Get(input.inputs[0]);
    bd->xsize = input.inputs[1].GetValue<double>();
    bd->ysize = input.inputs[2].GetValue<double>();
    bd->zsize = input.inputs[3].GetValue<double>();
    bd->has_sorigin = false;
    bd->bitmatrix = true;
    bd->has_duration = with_time;
    bd->has_torigin = false;

    idx_t opt_idx = 4;
    if (with_time) {
        bd->duration = input.inputs[opt_idx++].GetValue<interval_t>();
    }
    if (input.inputs.size() > opt_idx) {
        Value v = input.inputs[opt_idx++];
        if (!v.IsNull()) {
            bd->sorigin_blob = StringValue::Get(v);
            bd->has_sorigin = true;
        }
    }
    if (with_time && input.inputs.size() > opt_idx) {
        Value v = input.inputs[opt_idx++];
        if (!v.IsNull()) {
            bd->torigin = v.GetValue<timestamp_tz_t>();
            bd->has_torigin = true;
        }
    }
    if (input.inputs.size() > opt_idx) {
        Value v = input.inputs[opt_idx++];
        if (!v.IsNull()) bd->bitmatrix = v.GetValue<bool>();
    }

    if (with_time) {
        return_types = {GeoTypes::GEOMETRY(), LogicalType::TIMESTAMP_TZ, TgeompointType::TGEOMPOINT()};
        names = {"spaceBin", "timeBin", "tpoint"};
    } else {
        return_types = {GeoTypes::GEOMETRY(), TgeompointType::TGEOMPOINT()};
        names = {"spaceBin", "tpoint"};
    }
    return std::move(bd);
}

unique_ptr<FunctionData> SpaceSplitBind(ClientContext &context, TableFunctionBindInput &input,
                                        vector<LogicalType> &return_types, vector<string> &names) {
    return SpaceSplitBindCommon(context, input, return_types, names, /*with_time=*/false);
}

unique_ptr<FunctionData> SpaceTimeSplitBind(ClientContext &context, TableFunctionBindInput &input,
                                            vector<LogicalType> &return_types, vector<string> &names) {
    return SpaceSplitBindCommon(context, input, return_types, names, /*with_time=*/true);
}

unique_ptr<GlobalTableFunctionState> SpaceSplitInitCommon(ClientContext &context,
                                                          TableFunctionInitInput &input,
                                                          bool with_time) {
    auto &bind = input.bind_data->Cast<SpaceSplitBindData>();
    auto state = make_uniq<SpaceSplitGlobalState>();

    /* Materialise input temporal */
    size_t in_size = bind.blob.GetSize();
    Temporal *temp = (Temporal *)malloc(in_size);
    memcpy(temp, bind.blob.GetData(), in_size);

    /* Build origin geometry */
    GSERIALIZED *origin = nullptr;
    if (bind.has_sorigin) {
        origin = GeometryToGSerialized(bind.sorigin_blob, 0);
    }
    if (!origin) origin = DefaultOriginSplit();

    int count = 0;
    Temporal **trajs = nullptr;
    GSERIALIZED **bins = nullptr;
    TimestampTz *tbins = nullptr;
    if (with_time) {
        MeosInterval mi = IntervaltToInterval(bind.duration);
        TimestampTz torigin = 0;
        if (bind.has_torigin) {
            torigin = (TimestampTz) DuckDBToMeosTimestamp(bind.torigin).value;
        }
        SpaceTimeSplit sts = tgeo_space_time_split(temp, bind.xsize, bind.ysize, bind.zsize,
                                                   &mi, origin, torigin, bind.bitmatrix, true);
        trajs = sts.fragments;
        bins  = sts.space_bins;
        tbins = sts.time_bins;
        count = sts.count;
    } else {
        SpaceSplit ss = tgeo_space_split(temp, bind.xsize, bind.ysize, bind.zsize,
                                         origin, bind.bitmatrix, true);
        trajs = ss.fragments;
        bins  = ss.bins;
        count = ss.count;
    }
    free(temp);
    free(origin);

    if (!trajs || count <= 0) {
        if (trajs) free(trajs);
        if (bins) free(bins);
        if (tbins) free(tbins);
        return std::move(state);
    }

    state->space_ewkb.reserve(count);
    if (with_time) state->time_bin.reserve(count);
    state->tpoint.reserve(count);

    for (int i = 0; i < count; i++) {
        /* Capture the spaceBin as EWKB; defer DuckDB-spatial encoding to Exec
         * (where we have an arena allocator scoped to the result vector). */
        size_t wkb_sz = 0;
        uint8_t *wkb = geo_as_ewkb(bins[i], nullptr, &wkb_sz);
        if (wkb) {
            state->space_ewkb.emplace_back(wkb, wkb + wkb_sz);
            free(wkb);
        } else {
            state->space_ewkb.emplace_back();
        }
        free(bins[i]);

        if (with_time) {
            timestamp_tz_t t = MeosToDuckDBTimestamp(timestamp_tz_t((int64_t) tbins[i]));
            state->time_bin.emplace_back(Value::TIMESTAMPTZ(t));
        }

        size_t sz = temporal_mem_size(trajs[i]);
        Value tblob = Value::BLOB(reinterpret_cast<const_data_ptr_t>(trajs[i]), sz);
        tblob.Reinterpret(TgeompointType::TGEOMPOINT());
        state->tpoint.push_back(std::move(tblob));
        free(trajs[i]);
    }
    free(trajs);
    free(bins);
    if (tbins) free(tbins);
    return std::move(state);
}

unique_ptr<GlobalTableFunctionState> SpaceSplitInit(ClientContext &context,
                                                    TableFunctionInitInput &input) {
    return SpaceSplitInitCommon(context, input, false);
}

unique_ptr<GlobalTableFunctionState> SpaceTimeSplitInit(ClientContext &context,
                                                        TableFunctionInitInput &input) {
    return SpaceSplitInitCommon(context, input, true);
}

void EmitSpaceBinAt(ClientContext &context, Vector &col, idx_t row,
                    const std::vector<uint8_t> &ewkb) {
    if (ewkb.empty()) {
        FlatVector::SetNull(col, row, true);
        return;
    }
    GSERIALIZED *gs = geo_from_ewkb(ewkb.data(), ewkb.size(), 0);
    if (!gs) {
        FlatVector::SetNull(col, row, true);
        return;
    }
    ArenaAllocator arena(BufferAllocator::Get(context));
    string_t enc = GSerializedToGeometry(gs, arena, col);
    auto out_data = FlatVector::GetData<string_t>(col);
    out_data[row] = StringVector::AddStringOrBlob(col, enc);
    free(gs);
}

void SpaceSplitExec(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
    auto &state = input.global_state->Cast<SpaceSplitGlobalState>();
    idx_t remaining = state.tpoint.size() - state.idx;
    idx_t emit = MinValue<idx_t>(STANDARD_VECTOR_SIZE, remaining);
    auto &space_col = output.data[0];
    auto &tpoint_col = output.data[1];
    for (idx_t i = 0; i < emit; i++) {
        EmitSpaceBinAt(context, space_col, i, state.space_ewkb[state.idx]);
        tpoint_col.SetValue(i, state.tpoint[state.idx]);
        state.idx++;
    }
    output.SetCardinality(emit);
}

void SpaceTimeSplitExec(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
    auto &state = input.global_state->Cast<SpaceSplitGlobalState>();
    idx_t remaining = state.tpoint.size() - state.idx;
    idx_t emit = MinValue<idx_t>(STANDARD_VECTOR_SIZE, remaining);
    auto &space_col = output.data[0];
    auto &time_col = output.data[1];
    auto &tpoint_col = output.data[2];
    for (idx_t i = 0; i < emit; i++) {
        EmitSpaceBinAt(context, space_col, i, state.space_ewkb[state.idx]);
        time_col.SetValue(i, state.time_bin[state.idx]);
        tpoint_col.SetValue(i, state.tpoint[state.idx]);
        state.idx++;
    }
    output.SetCardinality(emit);
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

void TgeompointType::RegisterRoundtripIO(ExtensionLoader &loader) {
    const auto T = TGEOMPOINT();
    const auto V = LogicalType::VARCHAR;
    const auto B = LogicalType::BLOB;
    const auto BL = LogicalType::BOOLEAN;
    const auto I = LogicalType::INTEGER;

    /* asBinary / asEWKB — base WKB and extended WKB (with SRID) */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asBinary", {T}, B,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TgeoAsWkbExec(a, s, r, WKB_BASE); }));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asEWKB",   {T}, B,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TgeoAsWkbExec(a, s, r, WKB_EXTENDED); }));

    /* asHexWKB / asHexEWKB */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asHexWKB",  {T}, V,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TgeoAsHexWkbExec(a, s, r, WKB_BASE); }));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asHexEWKB", {T}, V,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TgeoAsHexWkbExec(a, s, r, WKB_EXTENDED); }));

    /* asMFJSON: 1..5 arg overloads */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asMFJSON", {T},                     V, TgeoAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asMFJSON", {T, BL},                 V, TgeoAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asMFJSON", {T, BL, I},              V, TgeoAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asMFJSON", {T, BL, I, I},           V, TgeoAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asMFJSON", {T, BL, I, I, V},        V, TgeoAsMfjsonExec));

    /* tgeompointFromText / FromEWKT — both route to tgeompoint_in (auto-detects EWKT prefix) */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tgeompointFromText",  {V}, T, TgeoFromTextExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tgeompointFromEWKT",  {V}, T, TgeoFromTextExec));

    /* tgeompointFromBinary / FromEWKB — temporal_from_wkb auto-detects format */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tgeompointFromBinary", {B}, T, TgeoFromWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tgeompointFromEWKB",   {B}, T, TgeoFromWkbExec));

    /* tgeompointFromHexWKB / FromHexEWKB — temporal_from_hexwkb auto-detects */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tgeompointFromHexWKB",  {V}, T, TgeoFromHexWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tgeompointFromHexEWKB", {V}, T, TgeoFromHexWkbExec));

    /* tgeompointFromMFJSON */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tgeompointFromMFJSON", {V}, T, TgeoFromMfjsonExec));
}

void TgeompointType::RegisterTpointSplit(ExtensionLoader &loader) {
    const auto T  = TGEOMPOINT();
    const auto D  = LogicalType::DOUBLE;
    const auto I  = LogicalType::INTERVAL;
    const auto TS = LogicalType::TIMESTAMP_TZ;
    const auto G  = GeoTypes::GEOMETRY();
    const auto B  = LogicalType::BOOLEAN;

    /* spaceSplit overloads (tgeompoint, xsize, ysize, zsize[, sorigin geom[, bitmatrix bool]]) */
    {
        std::vector<vector<LogicalType>> arg_lists = {
            {T, D, D, D},
            {T, D, D, D, G},
            {T, D, D, D, G, B},
        };
        for (auto &args : arg_lists) {
            TableFunction fn("spaceSplit", args, SpaceSplitExec, SpaceSplitBind, SpaceSplitInit);
            loader.RegisterFunction(fn);
        }
    }

    /* spaceTimeSplit overloads (tgeompoint, xsize, ysize, zsize, duration[, sorigin[, torigin[, bitmatrix]]]) */
    {
        std::vector<vector<LogicalType>> arg_lists = {
            {T, D, D, D, I},
            {T, D, D, D, I, G},
            {T, D, D, D, I, G, TS},
            {T, D, D, D, I, G, TS, B},
        };
        for (auto &args : arg_lists) {
            TableFunction fn("spaceTimeSplit", args, SpaceTimeSplitExec, SpaceTimeSplitBind, SpaceTimeSplitInit);
            loader.RegisterFunction(fn);
        }
    }
}

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
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asMVTGeom", {T, B},                MVT_OUT, TgeoAsMVTGeomExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asMVTGeom", {T, B, I},             MVT_OUT, TgeoAsMVTGeomExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asMVTGeom", {T, B, I, I},          MVT_OUT, TgeoAsMVTGeomExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asMVTGeom", {T, B, I, I, BL},      MVT_OUT, TgeoAsMVTGeomExec));

    /* geoMeasure(tgeompoint, tfloat[, segmentize]) -> geometry */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("geoMeasure", {T, TemporalTypes::TFLOAT()},     G, TgeoGeoMeasureExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("geoMeasure", {T, TemporalTypes::TFLOAT(), BL}, G, TgeoGeoMeasureExec));
}

} // namespace duckdb
