#include "meos_wrapper_simple.hpp"
#include "duckdb_version_compat.hpp"

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

LogicalType TgeompointType::tgeompoint() {
    LogicalType type(LogicalTypeId::BLOB);
    type.SetAlias("tgeompoint");
    return type;
}

void TgeompointType::RegisterType(ExtensionLoader &loader) {
    loader.RegisterType( "tgeompoint", tgeompoint());
}

void TgeompointType::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader, 
        LogicalType::VARCHAR,
        tgeompoint(),
        TgeompointFunctions::Tpoint_in
    );

    RegisterMeosCastFunction(loader, 
        tgeompoint(),
        LogicalType::VARCHAR,
        TemporalFunctions::Temporal_out
    );

    RegisterMeosCastFunction(loader, 
        tgeompoint(),
        StboxType::stbox(),
        TgeompointFunctions::Tspatial_to_stbox_cast
    );

    RegisterMeosCastFunction(loader, 
        tgeompoint(),
        SpanTypes::tstzspan(),
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
            {tgeompoint()},
            LogicalType::VARCHAR,
            TgeompointFunctions::Tspatial_as_text
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asEWKT",
            {tgeompoint()},
            LogicalType::VARCHAR,
            TgeompointFunctions::Tspatial_as_ewkt
        )
    );

    const auto varchar_list = LogicalType::LIST(LogicalType::VARCHAR);

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asText",
            {LogicalType::LIST(tgeompoint())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asText",
            {LogicalType::LIST(tgeompoint()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asText",
            {LogicalType::LIST(MobilityDuckGeometryType())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asText",
            {LogicalType::LIST(MobilityDuckGeometryType()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(tgeompoint())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(tgeompoint()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(MobilityDuckGeometryType())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(MobilityDuckGeometryType()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );

    /* ***************************************************
    * Constructor functions
    ****************************************************/
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompoint",
            {MobilityDuckGeometryType(), LogicalType::TIMESTAMP_TZ},
            tgeompoint(),
            TgeompointFunctions::Tpointinst_constructor
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompoint",
            {MobilityDuckGeometryType(), LogicalType::TIMESTAMP_TZ, LogicalType::INTEGER}, // with SRID
            tgeompoint(),
            TgeompointFunctions::Tpointinst_constructor
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompoint",
            {MobilityDuckGeometryType(), SetTypes::tstzset()},
            tgeompoint(),
            TemporalFunctions::Tsequence_from_base_tstzset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompoint",
            {MobilityDuckGeometryType(), SpanTypes::tstzspan()},
            tgeompoint(),
            TemporalFunctions::Tsequence_from_base_tstzspan
        )
    );

     duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompoint",
            {MobilityDuckGeometryType(), SpanTypes::tstzspan(), LogicalType::VARCHAR},
            tgeompoint(),
            TemporalFunctions::Tsequence_from_base_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompoint",
            {MobilityDuckGeometryType(), SpansetTypes::tstzspanset()},
            tgeompoint(),
            TemporalFunctions::Tsequenceset_from_base_tstzspanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompoint",
            {MobilityDuckGeometryType(), SpansetTypes::tstzspanset(), LogicalType::VARCHAR},
            tgeompoint(),
            TemporalFunctions::Tsequenceset_from_base_tstzspanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompointSeq",
            {LogicalType::LIST(tgeompoint())},
            tgeompoint(),
            // TemporalFunctions::Tsequence_constructor
            TgeompointFunctions::Tgeompoint_sequence_constructor
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompointSeqSet",
            {LogicalType::LIST(tgeompoint())},
            tgeompoint(),
            TemporalFunctions::Tsequenceset_constructor
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox",
            {tgeompoint()},
            StboxType::stbox(),
            TgeompointFunctions::Tspatial_to_stbox
        )
    );
    
    /* ***************************************************
     * Conversion functions
     ****************************************************/
    // timeSpan(tgeompoint) (temporal_to_tstzspan, group meos_temporal_conversion) is
    // generated from the catalog in generated_temporal_udfs.cpp (RETIRED_GROUPS).

    /***************************************************
     * Transformation functions
     ****************************************************/
    
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompointInst",
            {tgeompoint()},
            tgeompoint(),
            TemporalFunctions::Temporal_to_tinstant
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompointSeq",
            {tgeompoint(), LogicalType::VARCHAR},
            tgeompoint(),
            TemporalFunctions::Temporal_to_tsequence
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompointSeq",
            {tgeompoint()},
            tgeompoint(),
            TemporalFunctions::Temporal_to_tsequence
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompointSeqSet",
            {tgeompoint(), LogicalType::VARCHAR},
            tgeompoint(),
            TemporalFunctions::Temporal_to_tsequenceset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tgeompointSeqSet",
            {tgeompoint()},
            tgeompoint(),
            TemporalFunctions::Temporal_to_tsequenceset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "setInterp",
            {tgeompoint(), LogicalType::VARCHAR},
            tgeompoint(),
            TemporalFunctions::Temporal_set_interp
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "appendInstant",
            {tgeompoint(), tgeompoint()},
            tgeompoint(),
            TemporalFunctions::Temporal_append_tinstant
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "appendSequence",
            {tgeompoint(), tgeompoint()},
            tgeompoint(),
            TemporalFunctions::Temporal_append_tsequence
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "merge",
            {tgeompoint(), tgeompoint()},
            tgeompoint(),
            TemporalFunctions::Temporal_merge
        )
     );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "merge",
            {LogicalType::LIST(tgeompoint())},
            tgeompoint(),
            TemporalFunctions::Temporal_merge_array
        )
    );

    /* ***************************************************
    * Accessor functions
    ****************************************************/
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tempSubtype",
            {tgeompoint()},
            LogicalType::VARCHAR,
            TemporalFunctions::Temporal_subtype
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "interp",
            {tgeompoint()},
            LogicalType::VARCHAR,
            TemporalFunctions::Temporal_interp
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "getValue",
            {tgeompoint()},
            MobilityDuckGeometryType(),
            TgeompointFunctions::Tgeompoint_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "getTimestamp",
            {tgeompoint()},
            LogicalType::TIMESTAMP_TZ,
            TemporalFunctions::Tinstant_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "valueSet",
            {tgeompoint()},
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
            {tgeompoint(), LogicalType::BIGINT},
            MobilityDuckGeometryType(),
            TemporalFunctions::Temporal_value_n
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "getTime",
            {tgeompoint()},
            SpansetTypes::tstzspanset(),
            TemporalFunctions::Temporal_time
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "startValue",
            {tgeompoint()},
            MobilityDuckGeometryType(),
            TgeompointFunctions::Tgeompoint_start_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "endValue",
            {tgeompoint()},
            MobilityDuckGeometryType(),
            TgeompointFunctions::Tgeompoint_end_value
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "duration",
            {tgeompoint()},
            LogicalType::INTERVAL,
            TemporalFunctions::Temporal_duration
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "duration",
            {tgeompoint(), LogicalType::BOOLEAN},
            LogicalType::INTERVAL,
            TemporalFunctions::Temporal_duration
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "memSize",
            {tgeompoint()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_mem_size
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "lowerInc",
            {tgeompoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_lower_inc
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "upperInc",
            {tgeompoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_upper_inc
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "numInstants",
            {tgeompoint()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_num_instants
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "startInstant",
            {tgeompoint()},
            tgeompoint(),
            TemporalFunctions::Temporal_start_instant
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "endInstant",
            {tgeompoint()},
            tgeompoint(),
            TemporalFunctions::Temporal_end_instant
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "instantN",
            {tgeompoint(), LogicalType::INTEGER},
            tgeompoint(),
            TemporalFunctions::Temporal_instant_n
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "instants",
            {tgeompoint()},
            LogicalType::LIST(tgeompoint()),
            TemporalFunctions::Temporal_instants
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "numTimestamps",
            {tgeompoint()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_num_timestamps
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "startTimestamp",
            {tgeompoint()},
            LogicalType::TIMESTAMP_TZ,
            TemporalFunctions::Temporal_start_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "endTimestamp",
            {tgeompoint()},
            LogicalType::TIMESTAMP_TZ,
            TemporalFunctions::Temporal_end_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "timestampN",
            {tgeompoint(), LogicalType::INTEGER},
            LogicalType::TIMESTAMP_TZ,
            TemporalFunctions::Temporal_timestamptz_n
        )
    );

    // timestamps(tgeompoint) is generated from the catalog (temporal_timestamps) in generated_temporal_udfs.cpp.

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "numSequences",
            {tgeompoint()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_num_sequences
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "startSequence",
            {tgeompoint()},
            tgeompoint(),
            TemporalFunctions::Temporal_start_sequence
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "endSequence",
            {tgeompoint()},
            tgeompoint(),
            TemporalFunctions::Temporal_end_sequence
        )
    );
    
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "sequenceN",
            {tgeompoint(), LogicalType::INTEGER},
            tgeompoint(),
            TemporalFunctions::Temporal_sequence_n
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "sequences",
            {tgeompoint()},
            LogicalType::LIST(tgeompoint()),
            TemporalFunctions::Temporal_sequences
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "segments",
            {tgeompoint()},
            LogicalType::LIST(tgeompoint()),
            TemporalFunctions::Temporal_segments
        )
    );

    /* ***************************************************
     * Shift and Scale functions
     ****************************************************/
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "shiftTime",
            {tgeompoint(), LogicalType::INTERVAL},
            tgeompoint(),
            TemporalFunctions::Temporal_shift_time
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "scaleTime",
            {tgeompoint(), LogicalType::INTERVAL},
            tgeompoint(),
            TemporalFunctions::Temporal_scale_time
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "shiftScaleTime",
            {tgeompoint(), LogicalType::INTERVAL, LogicalType::INTERVAL},
            tgeompoint(),
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
            {tgeompoint(), MobilityDuckGeometryType()},
            tgeompoint(),
            TgeompointFunctions::Tgeompoint_at_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusValues",
            {tgeompoint(), MobilityDuckGeometryType()},
            tgeompoint(),
            TemporalFunctions::Temporal_minus_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atValues",
            {tgeompoint(), SpatialSetType::geomset()},
            tgeompoint(),
            TemporalFunctions::Temporal_at_values
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusValues",
            {tgeompoint(), SpatialSetType::geomset()},
            tgeompoint(),
            TemporalFunctions::Temporal_minus_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atTime",
            {tgeompoint(), LogicalType::TIMESTAMP_TZ},
            tgeompoint(),
            TemporalFunctions::Temporal_at_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusTime",
            {tgeompoint(), LogicalType::TIMESTAMP_TZ},
            tgeompoint(),
            TemporalFunctions::Temporal_minus_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "valueAtTimestamp",
            {tgeompoint(), LogicalType::TIMESTAMP_TZ},
            MobilityDuckGeometryType(),
            TemporalFunctions::Temporal_value_at_timestamptz
        )
    );
    
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atTime",
            {tgeompoint(), SetTypes::tstzset()},
            tgeompoint(),
            TemporalFunctions::Temporal_at_tstzset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusTime",
            {tgeompoint(), SetTypes::tstzset()},
            tgeompoint(),
            TemporalFunctions::Temporal_minus_tstzset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atTime",
            {tgeompoint(), SpanTypes::tstzspan()},
            tgeompoint(),
            TemporalFunctions::Temporal_at_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusTime",
            {tgeompoint(), SpanTypes::tstzspan()},
            tgeompoint(),
            TemporalFunctions::Temporal_minus_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atTime",
            {tgeompoint(), SpansetTypes::tstzspanset()},
            tgeompoint(),
            TemporalFunctions::Temporal_at_tstzspanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusTime",
            {tgeompoint(), SpansetTypes::tstzspanset()},
            tgeompoint(),
            TemporalFunctions::Temporal_minus_tstzspanset   
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "beforeTimestamp",
            {tgeompoint(), LogicalType::TIMESTAMP_TZ},
            tgeompoint(),
            TemporalFunctions::Temporal_before_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "afterTimestamp",
            {tgeompoint(), LogicalType::TIMESTAMP_TZ},
            tgeompoint(),
            TemporalFunctions::Temporal_after_timestamptz
        )
    );

    /* ***************************************************
     * Modification function
     ****************************************************/
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "insert",
            {tgeompoint(), tgeompoint()},
            tgeompoint(),
            TemporalFunctions::Temporal_update
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "insert",
            {tgeompoint(), tgeompoint(), LogicalType::BOOLEAN},
            tgeompoint(),
            TemporalFunctions::Temporal_update
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "update",
            {tgeompoint(), tgeompoint()},
            tgeompoint(),
            TemporalFunctions::Temporal_update
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "update",
            {tgeompoint(), tgeompoint(), LogicalType::BOOLEAN},
            tgeompoint(),
            TemporalFunctions::Temporal_update
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "deleteTime",
            {tgeompoint(), LogicalType::TIMESTAMP_TZ},
            tgeompoint(),
            TemporalFunctions::Temporal_delete_timestamptz
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "deleteTime",
            {tgeompoint(), LogicalType::TIMESTAMP_TZ, LogicalType::BOOLEAN},
            tgeompoint(),
            TemporalFunctions::Temporal_delete_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "deleteTime",
            {tgeompoint(), SetTypes::tstzset()},
            tgeompoint(),
            TemporalFunctions::Temporal_delete_tstzset
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "deleteTime",
            {tgeompoint(), SetTypes::tstzset(), LogicalType::BOOLEAN},
            tgeompoint(),
            TemporalFunctions::Temporal_delete_tstzset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "deleteTime",
            {tgeompoint(), SpanTypes::tstzspan()},
            tgeompoint(),
            TemporalFunctions::Temporal_delete_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "deleteTime",
            {tgeompoint(), SpanTypes::tstzspan(), LogicalType::BOOLEAN},
            tgeompoint(),
            TemporalFunctions::Temporal_delete_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "deleteTime",
            {tgeompoint(), SpansetTypes::tstzspanset()},
            tgeompoint(),
            TemporalFunctions::Temporal_delete_tstzspanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "deleteTime",
            {tgeompoint(), SpansetTypes::tstzspanset(), LogicalType::BOOLEAN},
            tgeompoint(),
            TemporalFunctions::Temporal_delete_tstzspanset
        )
    );


    /* ***************************************************
     * Stops function
     ****************************************************/
    
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stops",
            {tgeompoint(), LogicalType::DOUBLE, LogicalType::INTERVAL},
            tgeompoint(),
            TgeompointFunctions::Tgeompoint_stops
        )
    );

    /* ***************************************************
     * Comparison functions
     ****************************************************/
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "temporal_eq",
            {tgeompoint(), tgeompoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_eq
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "temporal_ne",
            {tgeompoint(), tgeompoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_ne
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "temporal_lt",
            {tgeompoint(), tgeompoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_lt
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "temporal_le",
            {tgeompoint(), tgeompoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_le
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "temporal_gt",
            {tgeompoint(), tgeompoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_gt
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "temporal_ge",
            {tgeompoint(), tgeompoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_ge
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "temporal_cmp",
            {tgeompoint(), tgeompoint()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_cmp
        )
    );

        duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "=",
            {tgeompoint(), tgeompoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_eq
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<>",
            {tgeompoint(), tgeompoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_ne
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<",
            {tgeompoint(), tgeompoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_lt
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<=",
            {tgeompoint(), tgeompoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_le
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            ">",
            {tgeompoint(), tgeompoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_gt
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            ">=",
            {tgeompoint(), tgeompoint()},
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
            {tgeompoint()},
            TemporalTypes::tfloat(),
            TgeompointFunctions::Tpoint_get_x
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "getY",
            {tgeompoint()},
            TemporalTypes::tfloat(),
            TgeompointFunctions::Tpoint_get_y
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "getZ",
            {tgeompoint()},
            TemporalTypes::tfloat(),
            TgeompointFunctions::Tpoint_get_z
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "length",
            {tgeompoint()},
            LogicalType::DOUBLE,
            TgeompointFunctions::Tpoint_length
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "cumulativeLength",
            {tgeompoint()},
            TemporalTypes::tfloat(),
            TgeompointFunctions::Tpoint_cumulative_length
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "speed",
            {tgeompoint()},
            TemporalTypes::tfloat(),
            TemporalFunctions::Temporal_derivative
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "twCentroid",
            {tgeompoint()},
            MobilityDuckGeometryType(),
            TgeompointFunctions::Tpoint_twcentroid
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "direction",
            {tgeompoint()},
            TemporalTypes::tfloat(),
            TgeompointFunctions::Tpoint_direction
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "azimuth",
            {tgeompoint()},
            TemporalTypes::tfloat(),
            TgeompointFunctions::Tpoint_azimuth
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "angularDifference",
            {tgeompoint()},
            MobilityDuckGeometryType(),
            TgeompointFunctions::Tpoint_angular_difference
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "isSimple",
            {tgeompoint()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Tpoint_is_simple
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "makeSimple",
            {tgeompoint()},
            LogicalType::LIST(tgeompoint()),
            TgeompointFunctions::Tpoint_make_simple
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "trajectory",
            {tgeompoint()},
            MobilityDuckGeometryType(),
            TgeompointFunctions::Tpoint_trajectory
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "trajectory_gs",
            {tgeompoint()},
            LogicalType::BLOB,
            TgeompointFunctions::Tpoint_trajectory_gs
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atGeometry",
            {tgeompoint(), MobilityDuckGeometryType()},
            tgeompoint(),
            TgeompointFunctions::Tgeo_at_geom
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusGeometry",
            {tgeompoint(), MobilityDuckGeometryType()},
            tgeompoint(),
            TgeompointFunctions::Tgeo_minus_geom
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusGeometry",
            {tgeompoint(), MobilityDuckGeometryType(), SpanTypes::floatspan()},
            tgeompoint(),
            TgeompointFunctions::Tgeo_minus_geom
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atStbox",
            {tgeompoint(), StboxType::stbox()},
            tgeompoint(),
            TgeompointFunctions::Tgeo_at_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atStbox",
            {tgeompoint(), StboxType::stbox(), LogicalType::BOOLEAN},
            tgeompoint(),
            TgeompointFunctions::Tgeo_at_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusStbox",
            {tgeompoint(), StboxType::stbox()},
            tgeompoint(),
            TgeompointFunctions::Tgeo_minus_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusStbox",
            {tgeompoint(), StboxType::stbox(), LogicalType::BOOLEAN},
            tgeompoint(),
            TgeompointFunctions::Tgeo_minus_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "transform",
            {tgeompoint(), LogicalType::INTEGER},
            tgeompoint(),
            TgeompointFunctions::Tspatial_transform
        )
    );

    // round(tgeompoint) at both arities is generated from the catalog temporal_round
    // signature (generated_temporal_udfs.cpp); no hand registration remains.


    

    /* ***************************************************
     * Operators (workaround as functions)
     ****************************************************/

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "&&", // overlaps
            {tgeompoint(), StboxType::stbox()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Temporal_overlaps_tgeompoint_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "&&", // overlaps
            {tgeompoint(), SpanTypes::tstzspan()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Temporal_overlaps_tgeompoint_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "@>", // contains
            {tgeompoint(), StboxType::stbox()},
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
            {tgeompoint(), tgeompoint()},
            TemporalTypes::tfloat(),
            TgeompointFunctions::Tdistance_tgeo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "shortestLine",
            {tgeompoint(), tgeompoint()},
            MobilityDuckGeometryType(),
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
        const auto T = tgeompoint();
        const auto G = MobilityDuckGeometryType();

        // ever/always spatial comparisons (meos_temporal_comp_ever) are supplied by
        // the generated surface (geometry-arg overloads via the Temporal+GSERIALIZED path).

        // spatial temporal comparisons tEq/tNe (meos_temporal_comp_temp) are supplied
        // by the generated surface (geometry-arg overloads via the Temporal+GSERIALIZED path).
    }

    /* tdistance named form (mirrors the <-> operator) */
    {
        const auto TG = tgeompoint();
        const auto G  = MobilityDuckGeometryType();
        const auto TF = TemporalTypes::tfloat();
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

/* spaceSplit / spaceTimeSplit accept both literal and LATERAL column
 * inputs through DuckDB's in_out_function plumbing.  The bind callback
 * only declares the return types; the per-row parameters are read at
 * exec time from the input DataChunk.  The local state carries the
 * iteration progress so that the output is produced lazily, bounded by
 * STANDARD_VECTOR_SIZE.  The pattern mirrors
 * duckdb/extension/icu/icu-table-range.cpp. */

struct SpaceSplitBindData : public TableFunctionData {
    bool with_time = false;
};

struct SpaceSplitLocalState : public LocalTableFunctionState {
    /* Row of the input chunk under split, and whether its bins have
     * already been computed into the vectors below.
     * space_ewkb[i] is the raw EWKB serialisation of the i-th spatial bin
     * (GSERIALIZED -> EWKB when the row is loaded, decoded into the result
     * vector's geometry format when emitted, using DuckDB-spatial's
     * wkb_reader).  tpoint holds pre-built tgeompoint-aliased BLOB values.
     * time_bin is populated only by the spaceTimeSplit overload. */
    idx_t current_input_row = 0;
    bool initialized_row = false;
    idx_t out_idx = 0;
    std::vector<std::vector<uint8_t>> space_ewkb;
    std::vector<Value> time_bin;
    std::vector<Value> tpoint;

    void Reset() {
        space_ewkb.clear();
        time_bin.clear();
        tpoint.clear();
        out_idx = 0;
    }
};

GSERIALIZED *DefaultOriginSplit() {
    return geompoint_make3dz(0, 0.0, 0.0, 0.0);
}

unique_ptr<FunctionData> SpaceSplitBindCommon(ClientContext &context,
                                              TableFunctionBindInput &input,
                                              vector<LogicalType> &return_types,
                                              vector<string> &names,
                                              bool with_time) {
    auto bd = make_uniq<SpaceSplitBindData>();
    bd->with_time = with_time;
    if (with_time) {
        return_types = {MobilityDuckGeometryType(), LogicalType::TIMESTAMP_TZ, TgeompointType::tgeompoint()};
        names = {"spaceBin", "timeBin", "tpoint"};
    } else {
        return_types = {MobilityDuckGeometryType(), TgeompointType::tgeompoint()};
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

unique_ptr<LocalTableFunctionState> SpaceSplitLocalInit(ExecutionContext &context,
                                                        TableFunctionInitInput &input,
                                                        GlobalTableFunctionState *) {
    return make_uniq<SpaceSplitLocalState>();
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

/* Read one input row's parameters and pre-compute all of its split bins
 * into the local state.  Called once per input row by the in_out_function
 * below.
 *
 * Input column layout:
 *   spaceSplit:     [0]=tgeompoint, [1]=xsize, [2]=ysize, [3]=zsize,
 *                   [4]=sorigin (geometry, optional),
 *                   [5]=bitmatrix (bool, optional)
 *   spaceTimeSplit: [0]=tgeompoint, [1]=xsize, [2]=ysize, [3]=zsize,
 *                   [4]=duration (interval),
 *                   [5]=sorigin (geometry, optional),
 *                   [6]=torigin (timestamptz, optional),
 *                   [7]=bitmatrix (bool, optional)
 */
void LoadSpaceSplitRow(ClientContext &context, SpaceSplitLocalState &state,
                       DataChunk &input, idx_t row_idx, bool with_time) {
    state.Reset();
    for (idx_t c = 0; c < input.ColumnCount(); c++) {
        input.data[c].Flatten(input.size());
    }
    if (FlatVector::IsNull(input.data[0], row_idx)) {
        return;
    }
    string_t blob = FlatVector::GetData<string_t>(input.data[0])[row_idx];
    double xsize = FlatVector::GetData<double>(input.data[1])[row_idx];
    double ysize = FlatVector::GetData<double>(input.data[2])[row_idx];
    double zsize = FlatVector::GetData<double>(input.data[3])[row_idx];

    idx_t col = 4;
    MeosInterval mi{};
    if (with_time) {
        if (input.ColumnCount() <= col || FlatVector::IsNull(input.data[col], row_idx)) {
            return;
        }
        mi = IntervaltToInterval(FlatVector::GetData<interval_t>(input.data[col])[row_idx]);
        col++;
    }
    GSERIALIZED *origin = nullptr;
    if (input.ColumnCount() > col && !FlatVector::IsNull(input.data[col], row_idx)) {
        string_t origin_blob = FlatVector::GetData<string_t>(input.data[col])[row_idx];
        origin = GeometryToGSerialized(origin_blob, 0);
    }
    col++;
    TimestampTz torigin = 0;
    if (with_time && input.ColumnCount() > col && !FlatVector::IsNull(input.data[col], row_idx)) {
        timestamp_tz_t t = FlatVector::GetData<timestamp_tz_t>(input.data[col])[row_idx];
        torigin = (TimestampTz) DuckDBToMeosTimestamp(t).value;
        col++;
    }
    bool bitmatrix = true;
    if (input.ColumnCount() > col && !FlatVector::IsNull(input.data[col], row_idx)) {
        bitmatrix = FlatVector::GetData<bool>(input.data[col])[row_idx];
    }
    if (!origin) origin = DefaultOriginSplit();

    Temporal *temp = (Temporal *) malloc(blob.GetSize());
    memcpy(temp, blob.GetData(), blob.GetSize());

    int count = 0;
    Temporal **trajs = nullptr;
    GSERIALIZED **bins = nullptr;
    TimestampTz *tbins = nullptr;
    if (with_time) {
        SpaceTimeSplit sts = tgeo_space_time_split(temp, xsize, ysize, zsize, &mi, origin,
                                                   torigin, bitmatrix, true);
        trajs = sts.fragments;
        bins  = sts.space_bins;
        tbins = sts.time_bins;
        count = sts.count;
    } else {
        SpaceSplit ss = tgeo_space_split(temp, xsize, ysize, zsize, origin, bitmatrix, true);
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
        return;
    }

    state.space_ewkb.reserve(count);
    if (with_time) state.time_bin.reserve(count);
    state.tpoint.reserve(count);

    for (int i = 0; i < count; i++) {
        /* Capture the spaceBin as EWKB; the DuckDB-spatial encoding happens
         * when the row is emitted, where an arena allocator scoped to the
         * result vector is available. */
        size_t wkb_sz = 0;
        uint8_t *wkb = geo_as_ewkb(bins[i], nullptr, &wkb_sz);
        if (wkb) {
            state.space_ewkb.emplace_back(wkb, wkb + wkb_sz);
            free(wkb);
        } else {
            state.space_ewkb.emplace_back();
        }
        free(bins[i]);

        if (with_time) {
            timestamp_tz_t t = MeosToDuckDBTimestamp(timestamp_tz_t((int64_t) tbins[i]));
            state.time_bin.emplace_back(Value::TIMESTAMPTZ(t));
        }

        size_t sz = temporal_mem_size(trajs[i]);
        Value tblob = Value::BLOB(reinterpret_cast<const_data_ptr_t>(trajs[i]), sz);
        tblob.Reinterpret(TgeompointType::tgeompoint());
        state.tpoint.push_back(std::move(tblob));
        free(trajs[i]);
    }
    free(trajs);
    free(bins);
    if (tbins) free(tbins);
}

OperatorResultType SpaceSplitInOutCommon(ExecutionContext &context, TableFunctionInput &data_p,
                                          DataChunk &input, DataChunk &output, bool with_time) {
    auto &state = data_p.local_state->Cast<SpaceSplitLocalState>();
    idx_t out_row = 0;

    /* PhysicalTableScan resets `output` (chunk.size() = 0) before each call
     * and treats output.size() == 0 as FINISHED regardless of the returned
     * OperatorResultType, while PhysicalTableInOutFunction (the LATERAL
     * path) honours the return value.  As in
     * duckdb/extension/icu/icu-table-range.cpp, leaving the chunk untouched
     * (size 0) and returning NEED_MORE_INPUT once the input chunk is drained
     * stops the source-scan path and advances the LATERAL path to the next
     * input chunk. */
    while (out_row < STANDARD_VECTOR_SIZE) {
        if (!state.initialized_row) {
            if (state.current_input_row >= input.size()) {
                /* Chunk drained.  Rows emitted in this call are returned as
                 * HAVE_MORE_OUTPUT with state.current_input_row left at
                 * input.size(), so the next invocation falls straight back
                 * into this branch with out_row == 0. */
                if (out_row > 0) {
                    output.SetCardinality(out_row);
                    return OperatorResultType::HAVE_MORE_OUTPUT;
                }
                state.current_input_row = 0;
                return OperatorResultType::NEED_MORE_INPUT;
            }
            LoadSpaceSplitRow(context.client, state, input, state.current_input_row, with_time);
            state.initialized_row = true;
        }
        if (state.out_idx >= state.tpoint.size()) {
            state.current_input_row++;
            state.initialized_row = false;
            continue;
        }
        EmitSpaceBinAt(context.client, output.data[0], out_row, state.space_ewkb[state.out_idx]);
        if (with_time) {
            output.data[1].SetValue(out_row, state.time_bin[state.out_idx]);
            output.data[2].SetValue(out_row, state.tpoint[state.out_idx]);
        } else {
            output.data[1].SetValue(out_row, state.tpoint[state.out_idx]);
        }
        state.out_idx++;
        out_row++;
    }
    output.SetCardinality(out_row);
    return OperatorResultType::HAVE_MORE_OUTPUT;
}

OperatorResultType SpaceSplitInOut(ExecutionContext &context, TableFunctionInput &data_p,
                                    DataChunk &input, DataChunk &output) {
    return SpaceSplitInOutCommon(context, data_p, input, output, /*with_time=*/false);
}

OperatorResultType SpaceTimeSplitInOut(ExecutionContext &context, TableFunctionInput &data_p,
                                        DataChunk &input, DataChunk &output) {
    return SpaceSplitInOutCommon(context, data_p, input, output, /*with_time=*/true);
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
    const auto T = tgeompoint();
    const auto V = LogicalType::VARCHAR;
    const auto B = LogicalType::BLOB;
    const auto BL = LogicalType::BOOLEAN;
    const auto I = LogicalType::INTEGER;

    /* asBinary / asEWKB — base WKB and extended WKB (with SRID) */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asBinary", {T}, B,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TgeoAsWkbExec(a, s, r, WKB_BASE); }));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asEWKB",   {T}, B,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TgeoAsWkbExec(a, s, r, WKB_EXTENDED); }));

    /* asHexEWKB (extended hex-WKB, includes SRID). asHexWKB (base) is generated from the
     * catalog as the inherited Temporal<T> output surface (generated_temporal_udfs.cpp). */
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
    const auto T  = tgeompoint();
    const auto D  = LogicalType::DOUBLE;
    const auto I  = LogicalType::INTERVAL;
    const auto TS = LogicalType::TIMESTAMP_TZ;
    const auto G  = MobilityDuckGeometryType();
    const auto B  = LogicalType::BOOLEAN;

    /* spaceSplit overloads (tgeompoint, xsize, ysize, zsize[, sorigin geom[, bitmatrix bool]]) */
    {
        std::vector<vector<LogicalType>> arg_lists = {
            {T, D, D, D},
            {T, D, D, D, G},
            {T, D, D, D, G, B},
        };
        for (auto &args : arg_lists) {
            TableFunction fn("spaceSplit", args, /*function=*/nullptr, SpaceSplitBind,
                             /*init_global=*/nullptr, SpaceSplitLocalInit);
            fn.in_out_function = SpaceSplitInOut;
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
            TableFunction fn("spaceTimeSplit", args, /*function=*/nullptr, SpaceTimeSplitBind,
                             /*init_global=*/nullptr, SpaceSplitLocalInit);
            fn.in_out_function = SpaceTimeSplitInOut;
            loader.RegisterFunction(fn);
        }
    }
}

void TgeompointType::RegisterAnalyticsViz(ExtensionLoader &loader) {
    const auto T  = tgeompoint();
    const auto B  = StboxType::stbox();
    const auto G  = MobilityDuckGeometryType();
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
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("geoMeasure", {T, TemporalTypes::tfloat()},     G, TgeoGeoMeasureExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("geoMeasure", {T, TemporalTypes::tfloat(), BL}, G, TgeoGeoMeasureExec));
}

} // namespace duckdb
