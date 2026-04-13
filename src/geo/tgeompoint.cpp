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
#include "duckdb/main/extension_util.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "spatial/spatial_types.hpp"

namespace duckdb {

LogicalType TgeompointType::TGEOMPOINT() {
    LogicalType type(LogicalTypeId::BLOB);
    type.SetAlias("TGEOMPOINT");
    return type;
}

void TgeompointType::RegisterType(DatabaseInstance &instance) {
    ExtensionUtil::RegisterType(instance, "TGEOMPOINT", TGEOMPOINT());
}

void TgeompointType::RegisterCastFunctions(DatabaseInstance &instance) {
    ExtensionUtil::RegisterCastFunction(
        instance,
        LogicalType::VARCHAR,
        TGEOMPOINT(),
        TgeompointFunctions::Tpoint_in
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        TGEOMPOINT(),
        LogicalType::VARCHAR,
        TemporalFunctions::Temporal_out
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        TGEOMPOINT(),
        StboxType::STBOX(),
        TgeompointFunctions::Tspatial_to_stbox_cast
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        TGEOMPOINT(),
        SpanTypes::TSTZSPAN(),
        TgeompointFunctions::Temporal_to_tstzspan_cast
    );
}

void TgeompointType::RegisterScalarFunctions(DatabaseInstance &instance) {

    /* ***************************************************
     * In/out functions
     ****************************************************/

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "asText",
            {TGEOMPOINT()},
            LogicalType::VARCHAR,
            TgeompointFunctions::Tspatial_as_text
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "asEWKT",
            {TGEOMPOINT()},
            LogicalType::VARCHAR,
            TgeompointFunctions::Tspatial_as_ewkt
        )
    );

    const auto varchar_list = LogicalType::LIST(LogicalType::VARCHAR);

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "asText",
            {LogicalType::LIST(TGEOMPOINT())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "asText",
            {LogicalType::LIST(TGEOMPOINT()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "asText",
            {LogicalType::LIST(GeoTypes::GEOMETRY())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "asText",
            {LogicalType::LIST(GeoTypes::GEOMETRY()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(TGEOMPOINT())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(TGEOMPOINT()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(GeoTypes::GEOMETRY())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
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
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "TGEOMPOINT",
            {GeoTypes::GEOMETRY(), LogicalType::TIMESTAMP_TZ},
            TGEOMPOINT(),
            TgeompointFunctions::Tpointinst_constructor
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "TGEOMPOINT",
            {GeoTypes::GEOMETRY(), LogicalType::TIMESTAMP_TZ, LogicalType::INTEGER}, // with SRID
            TGEOMPOINT(),
            TgeompointFunctions::Tpointinst_constructor
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tgeompoint",
            {GeoTypes::GEOMETRY(), SetTypes::tstzset()},
            TGEOMPOINT(),
            TemporalFunctions::Tsequence_from_base_tstzset
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tgeompoint",
            {GeoTypes::GEOMETRY(), SpanTypes::TSTZSPAN()},
            TGEOMPOINT(),
            TemporalFunctions::Tsequence_from_base_tstzspan
        )
    );

     ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tgeompoint",
            {GeoTypes::GEOMETRY(), SpanTypes::TSTZSPAN(), LogicalType::VARCHAR},
            TGEOMPOINT(),
            TemporalFunctions::Tsequence_from_base_tstzspan
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tgeompoint",
            {GeoTypes::GEOMETRY(), SpansetTypes::tstzspanset()},
            TGEOMPOINT(),
            TemporalFunctions::Tsequenceset_from_base_tstzspanset
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tgeompoint",
            {GeoTypes::GEOMETRY(), SpansetTypes::tstzspanset(), LogicalType::VARCHAR},
            TGEOMPOINT(),
            TemporalFunctions::Tsequenceset_from_base_tstzspanset
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tgeompointSeq",
            {LogicalType::LIST(TGEOMPOINT())},
            TGEOMPOINT(),
            // TemporalFunctions::Tsequence_constructor
            TgeompointFunctions::Tgeompoint_sequence_constructor
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tgeompointSeqSet",
            {LogicalType::LIST(TGEOMPOINT())},
            TGEOMPOINT(),
            TemporalFunctions::Tsequenceset_constructor
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
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
    ExtensionUtil::RegisterFunction(
        instance,
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
    
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tgeompointInst",
            {TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_to_tinstant
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tgeompointSeq",
            {TGEOMPOINT(), LogicalType::VARCHAR},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_to_tsequence
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tgeompointSeq",
            {TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_to_tsequence
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tgeompointSeqSet",
            {TGEOMPOINT(), LogicalType::VARCHAR},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_to_tsequenceset
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tgeompointSeqSet",
            {TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_to_tsequenceset
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "setInterp",
            {TGEOMPOINT(), LogicalType::VARCHAR},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_set_interp
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "appendInstant",
            {TGEOMPOINT(), TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_append_tinstant
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "appendSequence",
            {TGEOMPOINT(), TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_append_tsequence
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "merge",
            {TGEOMPOINT(), TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_merge
        )
     );

    ExtensionUtil::RegisterFunction(
        instance,
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
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tempSubtype",
            {TGEOMPOINT()},
            LogicalType::VARCHAR,
            TemporalFunctions::Temporal_subtype
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "interp",
            {TGEOMPOINT()},
            LogicalType::VARCHAR,
            TemporalFunctions::Temporal_interp
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "getValue",
            {TGEOMPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::Tgeompoint_value
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "getTimestamp",
            {TGEOMPOINT()},
            LogicalType::TIMESTAMP_TZ,
            TemporalFunctions::Tinstant_timestamptz
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "valueSet",
            {TGEOMPOINT()},
            SpatialSetType::geomset(),
            TemporalFunctions::Temporal_valueset
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "valueN",
            {TGEOMPOINT(), LogicalType::INTEGER},
            GeoTypes::GEOMETRY(),
            TemporalFunctions::Temporal_value_n
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "getTime",
            {TGEOMPOINT()},
            SpansetTypes::tstzspanset(),
            TemporalFunctions::Temporal_time
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "startValue",
            {TGEOMPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::Tgeompoint_start_value
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "endValue",
            {TGEOMPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::Tgeompoint_end_value
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "duration",
            {TGEOMPOINT(), LogicalType::BOOLEAN},
            LogicalType::INTERVAL,
            TemporalFunctions::Temporal_duration
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "memSize",
            {TGEOMPOINT()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_mem_size
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "lowerInc",
            {TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_lower_inc
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "upperInc",
            {TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_upper_inc
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "numInstants",
            {TGEOMPOINT()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_num_instants
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "startInstant",
            {TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_start_instant
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "endInstant",
            {TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_end_instant
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "instantN",
            {TGEOMPOINT(), LogicalType::INTEGER},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_instant_n
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "instants",
            {TGEOMPOINT()},
            LogicalType::LIST(TGEOMPOINT()),
            TemporalFunctions::Temporal_instants
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "numTimestamps",
            {TGEOMPOINT()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_num_timestamps
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "startTimestamp",
            {TGEOMPOINT()},
            LogicalType::TIMESTAMP_TZ,
            TemporalFunctions::Temporal_start_timestamptz
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "endTimestamp",
            {TGEOMPOINT()},
            LogicalType::TIMESTAMP_TZ,
            TemporalFunctions::Temporal_end_timestamptz
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "timestampN",
            {TGEOMPOINT(), LogicalType::INTEGER},
            LogicalType::TIMESTAMP_TZ,
            TemporalFunctions::Temporal_timestamptz_n
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "timestamps",
            {TGEOMPOINT()},
            LogicalType::LIST(LogicalType::TIMESTAMP_TZ),
            TemporalFunctions::Temporal_timestamps
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "numSequences",
            {TGEOMPOINT()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_num_sequences
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "startSequence",
            {TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_start_sequence
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "endSequence",
            {TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_end_sequence
        )
    );
    
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "sequenceN",
            {TGEOMPOINT(), LogicalType::INTEGER},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_sequence_n
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "sequences",
            {TGEOMPOINT()},
            LogicalType::LIST(TGEOMPOINT()),
            TemporalFunctions::Temporal_sequences
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
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
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "shiftTime",
            {TGEOMPOINT(), LogicalType::INTERVAL},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_shift_time
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "scaleTime",
            {TGEOMPOINT(), LogicalType::INTERVAL},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_scale_time
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
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

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "atValues",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeompoint_at_value
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "minusValues",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_minus_value
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "atValues",
            {TGEOMPOINT(), SpatialSetType::geomset()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_at_values
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "minusValues",
            {TGEOMPOINT(), SpatialSetType::geomset()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_minus_value
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "atTime",
            {TGEOMPOINT(), LogicalType::TIMESTAMP_TZ},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_at_timestamptz
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "minusTime",
            {TGEOMPOINT(), LogicalType::TIMESTAMP_TZ},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_minus_timestamptz
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "valueAtTimestamp",
            {TGEOMPOINT(), LogicalType::TIMESTAMP_TZ},
            GeoTypes::GEOMETRY(),
            TemporalFunctions::Temporal_value_at_timestamptz
        )
    );
    
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "atTime",
            {TGEOMPOINT(), SetTypes::tstzset()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_at_tstzset
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "minusTime",
            {TGEOMPOINT(), SetTypes::tstzset()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_minus_tstzset
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "atTime",
            {TGEOMPOINT(), SpanTypes::TSTZSPAN()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_at_tstzspan
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "minusTime",
            {TGEOMPOINT(), SpanTypes::TSTZSPAN()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_minus_tstzspan
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "atTime",
            {TGEOMPOINT(), SpansetTypes::tstzspanset()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_at_tstzspanset
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "minusTime",
            {TGEOMPOINT(), SpansetTypes::tstzspanset()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_minus_tstzspanset   
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "beforeTimestamp",
            {TGEOMPOINT(), LogicalType::TIMESTAMP_TZ},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_before_timestamptz
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
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
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "insert",
            {TGEOMPOINT(), TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_update
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "insert",
            {TGEOMPOINT(), TGEOMPOINT(), LogicalType::BOOLEAN},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_update
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "update",
            {TGEOMPOINT(), TGEOMPOINT()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_update
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "update",
            {TGEOMPOINT(), TGEOMPOINT(), LogicalType::BOOLEAN},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_update
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "deleteTime",
            {TGEOMPOINT(), LogicalType::TIMESTAMP_TZ},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_delete_timestamptz
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "deleteTime",
            {TGEOMPOINT(), LogicalType::TIMESTAMP_TZ, LogicalType::BOOLEAN},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_delete_timestamptz
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "deleteTime",
            {TGEOMPOINT(), SetTypes::tstzset()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_delete_tstzset
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "deleteTime",
            {TGEOMPOINT(), SetTypes::tstzset(), LogicalType::BOOLEAN},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_delete_tstzset
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "deleteTime",
            {TGEOMPOINT(), SpanTypes::TSTZSPAN()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_delete_tstzspan
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "deleteTime",
            {TGEOMPOINT(), SpanTypes::TSTZSPAN(), LogicalType::BOOLEAN},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_delete_tstzspan
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "deleteTime",
            {TGEOMPOINT(), SpansetTypes::tstzspanset()},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_delete_tstzspanset
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
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
    
    ExtensionUtil::RegisterFunction(
        instance,
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
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "temporal_eq",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_eq
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "temporal_ne",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_ne
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "temporal_lt",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_lt
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "temporal_le",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_le
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "temporal_gt",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_gt
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "temporal_ge",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_ge
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "temporal_cmp",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_cmp
        )
    );

        ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "=",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_eq
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "<>",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_ne
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "<",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_lt
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "<=",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_le
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            ">",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_gt
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
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
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "getX",
            {TGEOMPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tpoint_get_x
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "getY",
            {TGEOMPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tpoint_get_y
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "getZ",
            {TGEOMPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tpoint_get_z
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "length",
            {TGEOMPOINT()},
            LogicalType::DOUBLE,
            TgeompointFunctions::Tpoint_length
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "cumulativeLength",
            {TGEOMPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tpoint_cumulative_length
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "speed",
            {TGEOMPOINT()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Temporal_derivative
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "twCentroid",
            {TGEOMPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::Tpoint_twcentroid
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "direction",
            {TGEOMPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tpoint_direction
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "azimuth",
            {TGEOMPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tpoint_azimuth
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "angularDifference",
            {TGEOMPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::Tpoint_angular_difference
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "isSimple",
            {TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Tpoint_is_simple
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "makeSimple",
            {TGEOMPOINT()},
            LogicalType::LIST(TGEOMPOINT()),
            TgeompointFunctions::Tpoint_make_simple
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "trajectory",
            {TGEOMPOINT()},
            GeoTypes::GEOMETRY(),
            TgeompointFunctions::Tpoint_trajectory
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "trajectory_gs",
            {TGEOMPOINT()},
            LogicalType::BLOB,
            TgeompointFunctions::Tpoint_trajectory_gs
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "atGeometry",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeo_at_geom
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "minusGeometry",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeo_minus_geom
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "minusGeometry",
            {TGEOMPOINT(), GeoTypes::GEOMETRY(), SpanTypes::FLOATSPAN()},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeo_minus_geom
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "atStbox",
            {TGEOMPOINT(), StboxType::STBOX()},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeo_at_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "atStbox",
            {TGEOMPOINT(), StboxType::STBOX(), LogicalType::BOOLEAN},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeo_at_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "minusStbox",
            {TGEOMPOINT(), StboxType::STBOX()},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeo_minus_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "minusStbox",
            {TGEOMPOINT(), StboxType::STBOX(), LogicalType::BOOLEAN},
            TGEOMPOINT(),
            TgeompointFunctions::Tgeo_minus_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "transform",
            {TGEOMPOINT(), LogicalType::INTEGER},
            TGEOMPOINT(),
            TgeompointFunctions::Tspatial_transform
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "round",
            {TGEOMPOINT(), LogicalType::INTEGER},
            TGEOMPOINT(),
            TemporalFunctions::Temporal_round
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
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
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "eContains",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Econtains_geo_tgeo
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "aContains",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Acontains_geo_tgeo
        )
    );
    
     ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "eDisjoint",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Edisjoint_geo_tgeo
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "eDisjoint",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Edisjoint_tgeo_geo
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "eDisjoint",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Edisjoint_tgeo_tgeo
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "aDisjoint",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Adisjoint_geo_tgeo
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "aDisjoint",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Adisjoint_tgeo_geo
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "aDisjoint",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Adisjoint_tgeo_tgeo
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "eIntersects",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Eintersects_tgeo_geo  
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "eIntersects",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Eintersects_geo_tgeo  
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "eIntersects",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Eintersects_tgeo_tgeo  
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "aIntersects",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Aintersects_tgeo_geo  
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "aIntersects",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Aintersects_geo_tgeo  
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "aIntersects",
            {TGEOMPOINT(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Aintersects_tgeo_tgeo  
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "eTouches",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Etouches_geo_tpoint
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "eTouches",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Etouches_tpoint_geo
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "aTouches",
            {GeoTypes::GEOMETRY(), TGEOMPOINT()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Atouches_geo_tpoint
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "aTouches",
            {TGEOMPOINT(), GeoTypes::GEOMETRY()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Atouches_tpoint_geo
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "eDwithin",
            {TGEOMPOINT(), TGEOMPOINT(), LogicalType::DOUBLE},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Edwithin_tgeo_tgeo
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "eDwithin",
            {GeoTypes::GEOMETRY(), TGEOMPOINT(), LogicalType::DOUBLE},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Edwithin_geo_tgeo
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "eDwithin",
            {TGEOMPOINT(), GeoTypes::GEOMETRY(), LogicalType::DOUBLE},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Edwithin_tgeo_geo
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "aDwithin",
            {GeoTypes::GEOMETRY(), TGEOMPOINT(), LogicalType::DOUBLE},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Adwithin_geo_tgeo
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "aDwithin",
            {TGEOMPOINT(), GeoTypes::GEOMETRY(), LogicalType::DOUBLE},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Adwithin_tgeo_geo
        )
    );

     ExtensionUtil::RegisterFunction(
        instance,
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

    ExtensionUtil::RegisterFunction(
        instance,
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

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "&&", // overlaps
            {TGEOMPOINT(), StboxType::STBOX()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Temporal_overlaps_tgeompoint_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "&&", // overlaps
            {TGEOMPOINT(), SpanTypes::TSTZSPAN()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Temporal_overlaps_tgeompoint_tstzspan
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
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

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "<->",
            {TGEOMPOINT(), TGEOMPOINT()},
            TemporalTypes::TFLOAT(),
            TgeompointFunctions::Tdistance_tgeo_tgeo
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
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

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "collect_gs",
            {LogicalType::LIST(LogicalType::BLOB)},
            LogicalType::BLOB,
            TgeompointFunctions::collect_gs
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "distance_gs",
            {LogicalType::BLOB, LogicalType::BLOB},
            LogicalType::DOUBLE,
            TgeompointFunctions::distance_geo_geo
        )
    );
}

} // namespace duckdb