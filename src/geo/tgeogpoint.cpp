#include "meos_wrapper_simple.hpp"
#include "duckdb_version_compat.hpp"

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

#include "duckdb/common/extension_type_info.hpp"
#include <regex>
#include <string>
#include "geo_util.hpp"
#include "time_util.hpp"

extern "C" {
    #include <meos_internal.h>
    #include <meos_internal_geo.h>
}

namespace duckdb {

LogicalType TgeogpointType::tgeogpoint() {
    LogicalType type(LogicalTypeId::BLOB);
    type.SetAlias("tgeogpoint");
    return type;
}

void TgeogpointType::RegisterType(ExtensionLoader &loader) {
    loader.RegisterType("tgeogpoint", tgeogpoint());
}

void TgeogpointType::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader, 
        LogicalType::VARCHAR,
        tgeogpoint(),
        TgeogpointFunctions::Tpoint_in
    );

    RegisterMeosCastFunction(loader, 
        tgeogpoint(),
        LogicalType::VARCHAR,
        TemporalFunctions::Temporal_out
    );

    RegisterMeosCastFunction(loader, 
        tgeogpoint(),
        StboxType::stbox(),
        TgeompointFunctions::Tspatial_to_stbox_cast
    );

    RegisterMeosCastFunction(loader, 
        tgeogpoint(),
        SpanTypes::tstzspan(),
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
            {tgeogpoint()},
            LogicalType::VARCHAR,
            TgeompointFunctions::Tspatial_as_text
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "asEWKT",
            {tgeogpoint()},
            LogicalType::VARCHAR,
            TgeompointFunctions::Tspatial_as_ewkt
        )
    );

    const auto varchar_list = LogicalType::LIST(LogicalType::VARCHAR);

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "asText",
            {LogicalType::LIST(tgeogpoint())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "asText",
            {LogicalType::LIST(tgeogpoint()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(tgeogpoint())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(tgeogpoint()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );

    /* ***************************************************
    * Constructor functions
    ****************************************************/
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpoint",
            {MobilityDuckGeometryType(), LogicalType::TIMESTAMP_TZ},
            tgeogpoint(),
            TgeogpointFunctions::Tpointinst_constructor
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpoint",
            {MobilityDuckGeometryType(), LogicalType::TIMESTAMP_TZ, LogicalType::INTEGER},
            tgeogpoint(),
            TgeogpointFunctions::Tpointinst_constructor
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpoint",
            {MobilityDuckGeometryType(), SetTypes::tstzset()},
            tgeogpoint(),
            TemporalFunctions::Tsequence_from_base_tstzset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpoint",
            {MobilityDuckGeometryType(), SpanTypes::tstzspan()},
            tgeogpoint(),
            TemporalFunctions::Tsequence_from_base_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpoint",
            {MobilityDuckGeometryType(), SpanTypes::tstzspan(), LogicalType::VARCHAR},
            tgeogpoint(),
            TemporalFunctions::Tsequence_from_base_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpoint",
            {MobilityDuckGeometryType(), SpansetTypes::tstzspanset()},
            tgeogpoint(),
            TemporalFunctions::Tsequenceset_from_base_tstzspanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpoint",
            {MobilityDuckGeometryType(), SpansetTypes::tstzspanset(), LogicalType::VARCHAR},
            tgeogpoint(),
            TemporalFunctions::Tsequenceset_from_base_tstzspanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpointSeq",
            {LogicalType::LIST(tgeogpoint())},
            tgeogpoint(),
            TgeompointFunctions::Tgeompoint_sequence_constructor
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpointSeqSet",
            {LogicalType::LIST(tgeogpoint())},
            tgeogpoint(),
            TemporalFunctions::Tsequenceset_constructor
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "stbox",
            {tgeogpoint()},
            StboxType::stbox(),
            TgeompointFunctions::Tspatial_to_stbox
        )
    );

    /* ***************************************************
     * Conversion functions
     ****************************************************/
    // timeSpan(tgeogpoint) (temporal_to_tstzspan, group meos_temporal_conversion) is
    // generated from the catalog in generated_temporal_udfs.cpp (RETIRED_GROUPS).

    /***************************************************
     * Transformation functions
     ****************************************************/

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpointInst",
            {tgeogpoint()},
            tgeogpoint(),
            TemporalFunctions::Temporal_to_tinstant
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpointSeq",
            {tgeogpoint(), LogicalType::VARCHAR},
            tgeogpoint(),
            TemporalFunctions::Temporal_to_tsequence
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpointSeq",
            {tgeogpoint()},
            tgeogpoint(),
            TemporalFunctions::Temporal_to_tsequence
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpointSeqSet",
            {tgeogpoint(), LogicalType::VARCHAR},
            tgeogpoint(),
            TemporalFunctions::Temporal_to_tsequenceset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tgeogpointSeqSet",
            {tgeogpoint()},
            tgeogpoint(),
            TemporalFunctions::Temporal_to_tsequenceset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "setInterp",
            {tgeogpoint(), LogicalType::VARCHAR},
            tgeogpoint(),
            TemporalFunctions::Temporal_set_interp
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "appendInstant",
            {tgeogpoint(), tgeogpoint()},
            tgeogpoint(),
            TemporalFunctions::Temporal_append_tinstant
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "appendSequence",
            {tgeogpoint(), tgeogpoint()},
            tgeogpoint(),
            TemporalFunctions::Temporal_append_tsequence
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "merge",
            {tgeogpoint(), tgeogpoint()},
            tgeogpoint(),
            TemporalFunctions::Temporal_merge
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "merge",
            {LogicalType::LIST(tgeogpoint())},
            tgeogpoint(),
            TemporalFunctions::Temporal_merge_array
        )
    );

    /* ***************************************************
    * Accessor functions
    ****************************************************/
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "tempSubtype",
            {tgeogpoint()},
            LogicalType::VARCHAR,
            TemporalFunctions::Temporal_subtype
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "interp",
            {tgeogpoint()},
            LogicalType::VARCHAR,
            TemporalFunctions::Temporal_interp
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "getValue",
            {tgeogpoint()},
            MobilityDuckGeometryType(),
            TgeompointFunctions::Tgeompoint_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "getTimestamp",
            {tgeogpoint()},
            LogicalType::TIMESTAMP_TZ,
            TemporalFunctions::Tinstant_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "valueSet",
            {tgeogpoint()},
            SpatialSetType::geomset(),
            TemporalFunctions::Temporal_valueset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "valueN",
            {tgeogpoint(), LogicalType::BIGINT},
            MobilityDuckGeometryType(),
            TemporalFunctions::Temporal_value_n
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "getTime",
            {tgeogpoint()},
            SpansetTypes::tstzspanset(),
            TemporalFunctions::Temporal_time
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "startValue",
            {tgeogpoint()},
            MobilityDuckGeometryType(),
            TgeompointFunctions::Tgeompoint_start_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "endValue",
            {tgeogpoint()},
            MobilityDuckGeometryType(),
            TgeompointFunctions::Tgeompoint_end_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "duration",
            {tgeogpoint()},
            LogicalType::INTERVAL,
            TemporalFunctions::Temporal_duration
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "duration",
            {tgeogpoint(), LogicalType::BOOLEAN},
            LogicalType::INTERVAL,
            TemporalFunctions::Temporal_duration
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "memSize",
            {tgeogpoint()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_mem_size
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "lowerInc",
            {tgeogpoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_lower_inc
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "upperInc",
            {tgeogpoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_upper_inc
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "numInstants",
            {tgeogpoint()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_num_instants
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "startInstant",
            {tgeogpoint()},
            tgeogpoint(),
            TemporalFunctions::Temporal_start_instant
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "endInstant",
            {tgeogpoint()},
            tgeogpoint(),
            TemporalFunctions::Temporal_end_instant
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "instantN",
            {tgeogpoint(), LogicalType::INTEGER},
            tgeogpoint(),
            TemporalFunctions::Temporal_instant_n
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "instants",
            {tgeogpoint()},
            LogicalType::LIST(tgeogpoint()),
            TemporalFunctions::Temporal_instants
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "numTimestamps",
            {tgeogpoint()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_num_timestamps
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "startTimestamp",
            {tgeogpoint()},
            LogicalType::TIMESTAMP_TZ,
            TemporalFunctions::Temporal_start_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "endTimestamp",
            {tgeogpoint()},
            LogicalType::TIMESTAMP_TZ,
            TemporalFunctions::Temporal_end_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "timestampN",
            {tgeogpoint(), LogicalType::INTEGER},
            LogicalType::TIMESTAMP_TZ,
            TemporalFunctions::Temporal_timestamptz_n
        )
    );

    // timestamps(tgeogpoint) is generated from the catalog (temporal_timestamps) in generated_temporal_udfs.cpp.

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "numSequences",
            {tgeogpoint()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_num_sequences
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "startSequence",
            {tgeogpoint()},
            tgeogpoint(),
            TemporalFunctions::Temporal_start_sequence
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "endSequence",
            {tgeogpoint()},
            tgeogpoint(),
            TemporalFunctions::Temporal_end_sequence
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "sequenceN",
            {tgeogpoint(), LogicalType::INTEGER},
            tgeogpoint(),
            TemporalFunctions::Temporal_sequence_n
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "sequences",
            {tgeogpoint()},
            LogicalType::LIST(tgeogpoint()),
            TemporalFunctions::Temporal_sequences
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "segments",
            {tgeogpoint()},
            LogicalType::LIST(tgeogpoint()),
            TemporalFunctions::Temporal_segments
        )
    );

    /* ***************************************************
     * Shift and Scale functions
     ****************************************************/
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "shiftTime",
            {tgeogpoint(), LogicalType::INTERVAL},
            tgeogpoint(),
            TemporalFunctions::Temporal_shift_time
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "scaleTime",
            {tgeogpoint(), LogicalType::INTERVAL},
            tgeogpoint(),
            TemporalFunctions::Temporal_scale_time
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "shiftScaleTime",
            {tgeogpoint(), LogicalType::INTERVAL, LogicalType::INTERVAL},
            tgeogpoint(),
            TemporalFunctions::Temporal_shift_scale_time
        )
    );

    /* ***************************************************
     * Restriction functions
     ****************************************************/

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "atValues",
            {tgeogpoint(), MobilityDuckGeometryType()},
            tgeogpoint(),
            TgeompointFunctions::Tgeompoint_at_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "minusValues",
            {tgeogpoint(), MobilityDuckGeometryType()},
            tgeogpoint(),
            TemporalFunctions::Temporal_minus_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "atValues",
            {tgeogpoint(), SpatialSetType::geomset()},
            tgeogpoint(),
            TemporalFunctions::Temporal_at_values
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "minusValues",
            {tgeogpoint(), SpatialSetType::geomset()},
            tgeogpoint(),
            TemporalFunctions::Temporal_minus_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "atTime",
            {tgeogpoint(), LogicalType::TIMESTAMP_TZ},
            tgeogpoint(),
            TemporalFunctions::Temporal_at_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "minusTime",
            {tgeogpoint(), LogicalType::TIMESTAMP_TZ},
            tgeogpoint(),
            TemporalFunctions::Temporal_minus_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "valueAtTimestamp",
            {tgeogpoint(), LogicalType::TIMESTAMP_TZ},
            MobilityDuckGeometryType(),
            TemporalFunctions::Temporal_value_at_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "atTime",
            {tgeogpoint(), SetTypes::tstzset()},
            tgeogpoint(),
            TemporalFunctions::Temporal_at_tstzset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "minusTime",
            {tgeogpoint(), SetTypes::tstzset()},
            tgeogpoint(),
            TemporalFunctions::Temporal_minus_tstzset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "atTime",
            {tgeogpoint(), SpanTypes::tstzspan()},
            tgeogpoint(),
            TemporalFunctions::Temporal_at_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "minusTime",
            {tgeogpoint(), SpanTypes::tstzspan()},
            tgeogpoint(),
            TemporalFunctions::Temporal_minus_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "atTime",
            {tgeogpoint(), SpansetTypes::tstzspanset()},
            tgeogpoint(),
            TemporalFunctions::Temporal_at_tstzspanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "minusTime",
            {tgeogpoint(), SpansetTypes::tstzspanset()},
            tgeogpoint(),
            TemporalFunctions::Temporal_minus_tstzspanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "beforeTimestamp",
            {tgeogpoint(), LogicalType::TIMESTAMP_TZ},
            tgeogpoint(),
            TemporalFunctions::Temporal_before_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "afterTimestamp",
            {tgeogpoint(), LogicalType::TIMESTAMP_TZ},
            tgeogpoint(),
            TemporalFunctions::Temporal_after_timestamptz
        )
    );

    /* ***************************************************
     * Modification function
     ****************************************************/
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "insert",
            {tgeogpoint(), tgeogpoint()},
            tgeogpoint(),
            TemporalFunctions::Temporal_update
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "insert",
            {tgeogpoint(), tgeogpoint(), LogicalType::BOOLEAN},
            tgeogpoint(),
            TemporalFunctions::Temporal_update
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "update",
            {tgeogpoint(), tgeogpoint()},
            tgeogpoint(),
            TemporalFunctions::Temporal_update
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "update",
            {tgeogpoint(), tgeogpoint(), LogicalType::BOOLEAN},
            tgeogpoint(),
            TemporalFunctions::Temporal_update
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "deleteTime",
            {tgeogpoint(), LogicalType::TIMESTAMP_TZ},
            tgeogpoint(),
            TemporalFunctions::Temporal_delete_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "deleteTime",
            {tgeogpoint(), LogicalType::TIMESTAMP_TZ, LogicalType::BOOLEAN},
            tgeogpoint(),
            TemporalFunctions::Temporal_delete_timestamptz
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "deleteTime",
            {tgeogpoint(), SetTypes::tstzset()},
            tgeogpoint(),
            TemporalFunctions::Temporal_delete_tstzset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "deleteTime",
            {tgeogpoint(), SetTypes::tstzset(), LogicalType::BOOLEAN},
            tgeogpoint(),
            TemporalFunctions::Temporal_delete_tstzset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "deleteTime",
            {tgeogpoint(), SpanTypes::tstzspan()},
            tgeogpoint(),
            TemporalFunctions::Temporal_delete_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "deleteTime",
            {tgeogpoint(), SpanTypes::tstzspan(), LogicalType::BOOLEAN},
            tgeogpoint(),
            TemporalFunctions::Temporal_delete_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "deleteTime",
            {tgeogpoint(), SpansetTypes::tstzspanset()},
            tgeogpoint(),
            TemporalFunctions::Temporal_delete_tstzspanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "deleteTime",
            {tgeogpoint(), SpansetTypes::tstzspanset(), LogicalType::BOOLEAN},
            tgeogpoint(),
            TemporalFunctions::Temporal_delete_tstzspanset
        )
    );

    /* ***************************************************
     * Stops function
     ****************************************************/

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "stops",
            {tgeogpoint(), LogicalType::DOUBLE, LogicalType::INTERVAL},
            tgeogpoint(),
            TgeompointFunctions::Tgeompoint_stops
        )
    );

    /* ***************************************************
     * Comparison functions
     ****************************************************/
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "temporal_eq",
            {tgeogpoint(), tgeogpoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_eq
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "temporal_ne",
            {tgeogpoint(), tgeogpoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_ne
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "temporal_lt",
            {tgeogpoint(), tgeogpoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_lt
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "temporal_le",
            {tgeogpoint(), tgeogpoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_le
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "temporal_gt",
            {tgeogpoint(), tgeogpoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_gt
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "temporal_ge",
            {tgeogpoint(), tgeogpoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_ge
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "temporal_cmp",
            {tgeogpoint(), tgeogpoint()},
            LogicalType::INTEGER,
            TemporalFunctions::Temporal_cmp
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "=",
            {tgeogpoint(), tgeogpoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_eq
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "<>",
            {tgeogpoint(), tgeogpoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_ne
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "<",
            {tgeogpoint(), tgeogpoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_lt
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "<=",
            {tgeogpoint(), tgeogpoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_le
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            ">",
            {tgeogpoint(), tgeogpoint()},
            LogicalType::BOOLEAN,
            TemporalFunctions::Temporal_gt
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            ">=",
            {tgeogpoint(), tgeogpoint()},
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
            {tgeogpoint()},
            TemporalTypes::tfloat(),
            TgeompointFunctions::Tpoint_get_x
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "getY",
            {tgeogpoint()},
            TemporalTypes::tfloat(),
            TgeompointFunctions::Tpoint_get_y
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "getZ",
            {tgeogpoint()},
            TemporalTypes::tfloat(),
            TgeompointFunctions::Tpoint_get_z
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "length",
            {tgeogpoint()},
            LogicalType::DOUBLE,
            TgeompointFunctions::Tpoint_length
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "cumulativeLength",
            {tgeogpoint()},
            TemporalTypes::tfloat(),
            TgeompointFunctions::Tpoint_cumulative_length
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "speed",
            {tgeogpoint()},
            TemporalTypes::tfloat(),
            TemporalFunctions::Temporal_derivative
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "twCentroid",
            {tgeogpoint()},
            MobilityDuckGeometryType(),
            TgeompointFunctions::Tpoint_twcentroid
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "direction",
            {tgeogpoint()},
            TemporalTypes::tfloat(),
            TgeompointFunctions::Tpoint_direction
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "azimuth",
            {tgeogpoint()},
            TemporalTypes::tfloat(),
            TgeompointFunctions::Tpoint_azimuth
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "angularDifference",
            {tgeogpoint()},
            MobilityDuckGeometryType(),
            TgeompointFunctions::Tpoint_angular_difference
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "isSimple",
            {tgeogpoint()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Tpoint_is_simple
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "makeSimple",
            {tgeogpoint()},
            LogicalType::LIST(tgeogpoint()),
            TgeompointFunctions::Tpoint_make_simple
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "trajectory",
            {tgeogpoint()},
            MobilityDuckGeometryType(),
            TgeompointFunctions::Tpoint_trajectory
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "trajectory_gs",
            {tgeogpoint()},
            LogicalType::BLOB,
            TgeompointFunctions::Tpoint_trajectory_gs
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "atGeometry",
            {tgeogpoint(), MobilityDuckGeometryType()},
            tgeogpoint(),
            TgeompointFunctions::Tgeo_at_geom
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "minusGeometry",
            {tgeogpoint(), MobilityDuckGeometryType()},
            tgeogpoint(),
            TgeompointFunctions::Tgeo_minus_geom
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "minusGeometry",
            {tgeogpoint(), MobilityDuckGeometryType(), SpanTypes::floatspan()},
            tgeogpoint(),
            TgeompointFunctions::Tgeo_minus_geom
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "atStbox",
            {tgeogpoint(), StboxType::stbox()},
            tgeogpoint(),
            TgeompointFunctions::Tgeo_at_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "atStbox",
            {tgeogpoint(), StboxType::stbox(), LogicalType::BOOLEAN},
            tgeogpoint(),
            TgeompointFunctions::Tgeo_at_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "minusStbox",
            {tgeogpoint(), StboxType::stbox()},
            tgeogpoint(),
            TgeompointFunctions::Tgeo_minus_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "minusStbox",
            {tgeogpoint(), StboxType::stbox(), LogicalType::BOOLEAN},
            tgeogpoint(),
            TgeompointFunctions::Tgeo_minus_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "transform",
            {tgeogpoint(), LogicalType::INTEGER},
            tgeogpoint(),
            TgeompointFunctions::Tspatial_transform
        )
    );

    // round(tgeogpoint) at both arities is generated from the catalog temporal_round
    // signature (generated_temporal_udfs.cpp); no hand registration remains.


    /* ***************************************************
     * Operators (workaround as functions)
     ****************************************************/

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "&&",
            {tgeogpoint(), StboxType::stbox()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Temporal_overlaps_tgeompoint_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "&&",
            {tgeogpoint(), SpanTypes::tstzspan()},
            LogicalType::BOOLEAN,
            TgeompointFunctions::Temporal_overlaps_tgeompoint_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "@>",
            {tgeogpoint(), StboxType::stbox()},
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
            {tgeogpoint(), tgeogpoint()},
            TemporalTypes::tfloat(),
            TgeompointFunctions::Tdistance_tgeo_tgeo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "shortestLine",
            {tgeogpoint(), tgeogpoint()},
            MobilityDuckGeometryType(),
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
    const auto T = tgeogpoint();
    const auto V = LogicalType::VARCHAR;
    const auto B = LogicalType::BLOB;
    const auto BL = LogicalType::BOOLEAN;
    const auto I = LogicalType::INTEGER;

    /* asBinary / asEWKB */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asBinary", {T}, B,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TgeogAsWkbExec(a, s, r, GEOG_WKB_BASE); }));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asEWKB", {T}, B,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TgeogAsWkbExec(a, s, r, WKB_EXTENDED); }));

    /* asHexEWKB (extended hex-WKB, includes SRID). asHexWKB (base) is generated from the
     * catalog as the inherited Temporal<T> output surface (generated_temporal_udfs.cpp). */
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

// ============================================================
// TGeogpointType (new full-parity surface)
// ============================================================

LogicalType TGeogpointType::tgeogpoint() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("tgeogpoint");
    return type;
}

/*
 * Constructors
*/

inline void Tgeogpoint_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_geom_vec = args.data[0];
    
    UnaryExecutor::Execute<string_t, string_t>(
        input_geom_vec, result, count, 
        [&](string_t input_geom_str) -> string_t {
            std::string input = input_geom_str.GetString();
            
            Temporal *tinst = tgeogpoint_in(input.c_str());
            if (!tinst) {
                throw InvalidInputException("Invalid tgeogpoint input: " + input);
            }
            
            size_t data_size = temporal_mem_size(tinst);
            
            uint8_t *data_buffer = (uint8_t*)malloc(data_size);
            if (!data_buffer) {
                free(tinst);  
                throw InvalidInputException("Failed to allocate memory for tgeogpoint data");
            }
            
            memcpy(data_buffer, tinst, data_size);
            
            string_t data_string_t(reinterpret_cast<const char*>(data_buffer), data_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, data_string_t);
            
            free(data_buffer);
            free(tinst);  
            
            return stored_data;
        });
    
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

inline void Tgeogpointinst_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &value_vec = args.data[0];
    auto &t_vec = args.data[1];

    BinaryExecutor::Execute<string_t, timestamp_tz_t, string_t>(
        value_vec, t_vec, result, count,
        [&](string_t value_str, timestamp_tz_t t) -> string_t {
            std::string value = value_str.GetString();
            
            GSERIALIZED *gs = geom_in(value.c_str(), -1); // -1 for no typmod constraint

            if (gs == NULL) {
                throw InvalidInputException("Invalid geometry format: " + value);
            }

            GSERIALIZED *gs_geog = geom_to_geog(gs);
            free(gs);
            if (gs_geog == NULL) {
                throw InvalidInputException("Failed to convert geometry to geography: " + value);
            }

            timestamp_tz_t meos_timestamp = DuckDBToMeosTimestamp(t);
            TInstant *inst = tpointinst_make(gs_geog, static_cast<TimestampTz>(meos_timestamp.value));
            free(gs_geog);

            if (inst == NULL) {
                throw InvalidInputException("Failed to create TInstant");
            }

            size_t data_size = temporal_mem_size((Temporal*)inst);

            uint8_t *data_buffer = (uint8_t *)malloc(data_size);

            if (!data_buffer){
                free(inst);
                throw InvalidInputException("Failed to allocate memory to geometry data");
            }
            memcpy(data_buffer, inst, data_size);

            string_t data_string_t(reinterpret_cast<const char*>(data_buffer),data_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, data_string_t);
            
            free(data_buffer);
            free(inst);  
            
            return stored_data;

        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}


inline void Tgeogpoint_sequence_from_tstzspan(DataChunk &args, ExpressionState &state, Vector &result) {
    const char* default_interp = "step";
    auto count = args.size();
    auto arg_count = args.ColumnCount();

    auto &input_geom_vec = args.data[0];
    auto &span_vec = args.data[1];
    
    // Check if interpolation parameter is provided
    Vector *interp_vec = nullptr;
    if (arg_count > 2) {
        interp_vec = &args.data[2];
    }

    BinaryExecutor::Execute<string_t, string_t, string_t>(
        input_geom_vec, span_vec, result, count,
        [&](string_t input_geom_str, string_t span_str)-> string_t{
            std::string geom_value = input_geom_str.GetString();

            GSERIALIZED *gs = geom_in(geom_value.c_str(), -1);
            
            if(gs == NULL){
                throw InvalidInputException("Invalid geometry format: "+ geom_value);
            }
            
            std::string input = span_str.GetString();
            
            Span *span_cmp = reinterpret_cast<Span*>(const_cast<char*>(input.c_str()));

            // Use default interpolation or provided value
            interpType interp = interptype_from_string(default_interp);
            if (interp_vec) {
                std::string interp_string = default_interp; 
                interp = interptype_from_string(interp_string.c_str());
            }

            TSequence *seq = tsequence_from_base_tstzspan(Datum(gs), T_TGEOGPOINT, span_cmp, interp);

            if (seq == NULL) {
                free(gs);
                throw InvalidInputException("Failed to create TSequence");
            }

            size_t seq_size = temporal_mem_size((Temporal*)seq);

            uint8_t *seq_buffer = (uint8_t *)malloc(seq_size);
            if (!seq_buffer) {
                free(seq);
                free(gs);
                throw InvalidInputException("Failed to allocate memory for sequence data");
            }

            memcpy(seq_buffer, seq, seq_size);

            string_t seq_string_t((char*) seq_buffer, seq_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, seq_string_t);

            free(seq_buffer);
            free(seq);
            free(gs);

            return stored_data;

        });

    if (count == 1){
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

TInstant **temparr_extract_gp(Vector &tgeogpoint_arr_vec, list_entry_t list_entry, int *count) {
    auto &child_vector = ListVector::GetEntry(tgeogpoint_arr_vec);
    auto list_size = list_entry.length;
    auto list_offset = list_entry.offset;
    
    if (list_size == 0) {
        *count = 0;
        return nullptr;
    }
    
    *count = list_size;
    
    TInstant **instants = (TInstant**)malloc(sizeof(TInstant*) * list_size);
    if (!instants) {
        *count = 0;
        return nullptr;
    }
    
    for (idx_t i = 0; i < list_size; i++) {
        auto element_idx = list_offset + i;
        string_t tgeom_blob = FlatVector::GetData<string_t>(child_vector)[element_idx];
        
        const uint8_t *data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
        size_t data_size = tgeom_blob.GetSize();
        
        if (data_size < sizeof(void*)) {
            for (idx_t j = 0; j < i; j++) {
                if (instants[j]) free(instants[j]);
            }
            free(instants);
            *count = 0;
            return nullptr;
        }
        
        uint8_t *data_copy = (uint8_t*)malloc(data_size);
        if (!data_copy) {
            for (idx_t j = 0; j < i; j++) {
                if (instants[j]) free(instants[j]);
            }
            free(instants);
            *count = 0;
            return nullptr;
        }
        memcpy(data_copy, data, data_size);
        
        Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
        if (!temp) {
            free(data_copy);
            for (idx_t j = 0; j < i; j++) {
                if (instants[j]) free(instants[j]);
            }
            free(instants);
            *count = 0;
            return nullptr;
        }
        
        instants[i] = (TInstant*)temp;
    }
    
    return instants;
}

inline void Tgeogpoint_sequence_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    // Default values
    const char* default_interp = "linear";
    bool default_lower_inc = true;
    bool default_upper_inc = true;
    
    auto count = args.size();
    auto arg_count = args.ColumnCount();
    
    
    auto &tgeogpoint_arr_vec = args.data[0];    
    tgeogpoint_arr_vec.Flatten(count);
    
    Vector *interp_vec = nullptr;
    Vector *lower_vec = nullptr;
    Vector *upper_vec = nullptr;
    
    if (arg_count > 1) {
        interp_vec = &args.data[1];
        interp_vec->Flatten(count);
    }
    if (arg_count > 2) {
        lower_vec = &args.data[2];
        lower_vec->Flatten(count);
    }
    if (arg_count > 3) {
        upper_vec = &args.data[3];
        upper_vec->Flatten(count);
    }
    
    result.Flatten(count);
    
    auto tgeogpoint_data = FlatVector::GetData<list_entry_t>(tgeogpoint_arr_vec);
    auto result_data = FlatVector::GetData<string_t>(result);
    
    // Get validity masks
    auto &tgeogpoint_validity = FlatVector::Validity(tgeogpoint_arr_vec);
    auto &result_validity = FlatVector::Validity(result);
    
    for (idx_t i = 0; i < count; i++) {
        if (!tgeogpoint_validity.RowIsValid(i)) {
            result_validity.SetInvalid(i);
            continue;
        }
        
        try {
            list_entry_t list_entry = tgeogpoint_data[i];
            
            // Handle interp parameter with default
            std::string interp_str = default_interp;
            if (interp_vec) {
                auto interp_data = FlatVector::GetData<string_t>(*interp_vec);
                auto &interp_validity = FlatVector::Validity(*interp_vec);
                if (interp_validity.RowIsValid(i)) {
                    interp_str = interp_data[i].GetString();
                }
            }
            interpType interp = interptype_from_string(interp_str.c_str());
            
            bool lower_inc = default_lower_inc;
            bool upper_inc = default_upper_inc;
            
            if (lower_vec) {
                auto lower_data = FlatVector::GetData<bool>(*lower_vec);
                auto &lower_validity = FlatVector::Validity(*lower_vec);
                if (lower_validity.RowIsValid(i)) {
                    lower_inc = lower_data[i];
                }
            }

            if (upper_vec) {
                auto upper_data = FlatVector::GetData<bool>(*upper_vec);
                auto &upper_validity = FlatVector::Validity(*upper_vec);
                if (upper_validity.RowIsValid(i)) {
                    upper_inc = upper_data[i];
                }
            }
            
            // Extract array elements
            int element_count;
            TInstant **instants = temparr_extract_gp(tgeogpoint_arr_vec, list_entry, &element_count);
            
            if (!instants || element_count == 0) {
                result_validity.SetInvalid(i);
                continue;
            }
            
            TSequence *sequence_result = tsequence_make((TInstant **) instants, element_count, 
                                                    lower_inc, upper_inc, interp, true);
            
            if (!sequence_result) {
                for (int j = 0; j < element_count; j++) {
                    if (instants[j]) {
                        free(instants[j]);
                    }
                }
                free(instants);
                result_validity.SetInvalid(i);
                continue;
            }
            
            size_t data_size = temporal_mem_size(reinterpret_cast<Temporal*>(sequence_result));
            uint8_t *data_buffer = (uint8_t*)malloc(data_size);
            if (!data_buffer) {
                free(sequence_result);
                for (int j = 0; j < element_count; j++) {
                    if (instants[j]) {
                        free(instants[j]);
                    }
                }
                free(instants);
                result_validity.SetInvalid(i);
                continue;
            }
            
            memcpy(data_buffer, sequence_result, data_size);
            
            string_t data_string_t(reinterpret_cast<const char*>(data_buffer), data_size);
            result_data[i] = StringVector::AddStringOrBlob(result, data_string_t);
            
            free(data_buffer);
            free(sequence_result);
            for (int j = 0; j < element_count; j++) {
                if (instants[j]) {
                    free(instants[j]);
                }
            }
            free(instants);
            
        } catch (const std::exception& e) {
            result_validity.SetInvalid(i);
        }
    }
    
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}




/*
 * Conversions
*/

inline void Temporal_to_tstzspan(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_geom_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_geom_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));
            
            if (!temp) {
                throw InvalidInputException("Invalid tgeogpoint data: null pointer");
            }

            Span *timespan = temporal_to_tstzspan(temp);
            
            if (!timespan) {
                throw InvalidInputException("Failed to extract timespan from tgeogpoint");
            }
            
            size_t span_size = sizeof(Span);
            
            uint8_t *span_buffer = (uint8_t*)malloc(span_size);
            if (!span_buffer) {
                free(timespan);
                throw InvalidInputException("Failed to allocate memory for timespan data");
            }
            
            memcpy(span_buffer, timespan, span_size);
            
            string_t span_string_t(reinterpret_cast<const char*>(span_buffer), span_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
            
            free(span_buffer);
            free(timespan);
            
            return stored_data;
        }
    );

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/*
 * Transformations
*/

inline void Temporal_to_tinstant(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_geom_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_geom_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));
            
            if (!temp) {
                throw InvalidInputException("Invalid tgeogpoint data: null pointer");
            }

            TInstant *inst = temporal_as_tinstant(temp);
            if (!inst) {
                throw InvalidInputException("Failed to convert tgeogpoint to TInstant");
            }
            
            size_t inst_size = temporal_mem_size((Temporal*)inst);
            
            uint8_t *inst_buffer = (uint8_t*)malloc(inst_size);
            if (!inst_buffer) {
                free(inst);
                throw InvalidInputException("Failed to allocate memory for TInstant data");
            }
            
            memcpy(inst_buffer, inst, inst_size);
            
            string_t inst_string_t(reinterpret_cast<const char*>(inst_buffer), inst_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, inst_string_t);
            
            free(inst_buffer);
            free(inst);
            
            return stored_data;
        }
    );

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}


inline void Temporal_set_interp(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &tgeom_vec = args.data[0];
    auto &interp_vec = args.data[1];

    tgeom_vec.Flatten(count);
    interp_vec.Flatten(count);

    BinaryExecutor::Execute<string_t, string_t, string_t>(
        tgeom_vec, interp_vec, result, count,
        [&](string_t tgeom_str_t, string_t interp_str_t) -> string_t {
          
            std::string input = tgeom_str_t.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));
            if (!temp) {
                throw InvalidInputException("Invalid tgeogpoint data: null pointer");
            }

            std::string interp_str = interp_str_t.GetString();
            interpType new_interp = interptype_from_string(interp_str.c_str());
            
            Temporal *result_temp = temporal_set_interp(temp, new_interp);
            if (!result_temp) {
                throw InvalidInputException("Failed to set interpolation");
            }
            
            // Serialize result back to binary
            size_t result_size = temporal_mem_size(result_temp);
            uint8_t *result_buffer = (uint8_t*)malloc(result_size);
            if (!result_buffer) {
                free(result_temp);
                throw InvalidInputException("Failed to allocate memory for result");
            }
            
            memcpy(result_buffer, result_temp, result_size);
            string_t result_string_t(reinterpret_cast<const char*>(result_buffer), result_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, result_string_t);
            
            free(result_buffer);
            free(result_temp);
            
            return stored_data;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}


inline void Temporal_merge(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &tgeom1_vec = args.data[0];
    auto &tgeom2_vec = args.data[1];

    tgeom1_vec.Flatten(count);
    tgeom2_vec.Flatten(count);

    BinaryExecutor::Execute<string_t, string_t, string_t>(
        tgeom1_vec, tgeom2_vec, result, count,
        [&](string_t tgeom1_str_t, string_t tgeom2_str_t) -> string_t {
            std::string tgeom1 = tgeom1_str_t.GetString();

            Temporal *temp1 = reinterpret_cast<Temporal*>(const_cast<char*>(tgeom1.c_str()));
            if (!temp1) {
                throw InvalidInputException("Invalid tgeogpoint data: null pointer");
            }

            std::string tgeom2 = tgeom2_str_t.GetString();

            Temporal *temp2 = reinterpret_cast<Temporal*>(const_cast<char*>(tgeom2.c_str()));
            if (!temp2) {
                throw InvalidInputException("Invalid tgeogpoint data: null pointer");
            }
            
            Temporal *result_temp = temporal_merge(temp1, temp2);
            if (!result_temp) {
                throw InvalidInputException("Failed to merge temporal geometries");
            }
            
            // Serialize result back to binary
            size_t result_size = temporal_mem_size(result_temp);
            uint8_t *result_buffer = (uint8_t*)malloc(result_size);
            if (!result_buffer) {
                free(result_temp);
                throw InvalidInputException("Failed to allocate memory for result");
            }
            
            memcpy(result_buffer, result_temp, result_size);
            string_t result_string_t(reinterpret_cast<const char*>(result_buffer), result_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, result_string_t);
            
            free(result_buffer);
            free(result_temp);
            
            return stored_data;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}


/*
 * Accessor Functions
*/

inline void Temporal_subtype(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &tgeom_vec = args.data[0];

    tgeom_vec.Flatten(count);

    UnaryExecutor::Execute<string_t, string_t>(
        tgeom_vec, result, count,
        [&](string_t tgeom_str_t) -> string_t {
            std::string input = tgeom_str_t.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));
            if (!temp) {
                throw InvalidInputException("Invalid tgeogpoint data: null pointer");
            }
            
            const char *subtype_str = temporal_subtype(temp);
            if (!subtype_str) {
                throw InvalidInputException("Failed to get temporal subtype");
            }

            std::string result_str(subtype_str);
            string_t stored_result = StringVector::AddString(result, result_str);
            
            return stored_result;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}




inline void Temporal_interp(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &tgeom_vec = args.data[0];

    tgeom_vec.Flatten(count);

    UnaryExecutor::Execute<string_t, string_t>(
        tgeom_vec, result, count,
        [&](string_t tgeom_str_t) -> string_t {

            std::string input = tgeom_str_t.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));
            if (!temp) {
                throw InvalidInputException("Invalid tgeogpoint data: null pointer");
            }

            
            const char *interp_str = temporal_interp(temp);
            if (!interp_str) {
                throw InvalidInputException("Failed to get temporal interpolation");
            }

            std::string result_str(interp_str);
            string_t stored_result = StringVector::AddString(result, result_str);
            
            return stored_result;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

inline void Temporal_mem_size(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &tgeom_vec = args.data[0];

    tgeom_vec.Flatten(count);

    UnaryExecutor::Execute<string_t, int32_t>(
        tgeom_vec, result, count,
        [&](string_t tgeom_str_t) -> int32_t {
           std::string input = tgeom_str_t.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));
            if (!temp) {
                throw InvalidInputException("Invalid tgeogpoint data: null pointer");
            }
            
            size_t mem_size = temporal_mem_size(temp);
            
            
            return static_cast<int32_t>(mem_size);
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

inline void Tinstant_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_vec = args.data[0];
    
    // Direct geometry to geometry conversion, no string conversion needed
    UnaryExecutor::Execute<string_t, string_t>(
        input_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();

            TInstant *tinst = reinterpret_cast<TInstant*>(const_cast<char*>(input.c_str()));
            
            Datum geo = tinstant_value(tinst);
            
            GSERIALIZED *geom = DatumGetGserializedP(geo);
            
            string_t geometry_blob = GSerializedToGeometry(geom, state, result);
            string_t stored_result = StringVector::AddStringOrBlob(result, geometry_blob);

            free(geom);
            
            return stored_result;
        });
    
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}



inline void Temporal_start_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_vec = args.data[0];
    
    // Direct geometry to geometry conversion, no string conversion needed
    UnaryExecutor::Execute<string_t, string_t>(
        input_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();
            
            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));
            
            Datum start_datum = temporal_start_value(temp);
            
            GSERIALIZED *start_geom = DatumGetGserializedP(start_datum);
            string_t geometry_blob = GSerializedToGeometry(start_geom, state, result);
            string_t stored_result = StringVector::AddStringOrBlob(result, geometry_blob);

            free(start_geom);
            
            return stored_result;
        });
    
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}


inline void Temporal_end_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_vec = args.data[0];
    
    // Direct geometry to geometry conversion, no string conversion needed
    UnaryExecutor::Execute<string_t, string_t>(
        input_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();
            
            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));
            
            Datum start_datum = temporal_end_value(temp);
            
            GSERIALIZED *start_geom = DatumGetGserializedP(start_datum);
            string_t geometry_blob = GSerializedToGeometry(start_geom, state, result);
            size_t ewkb_size;
            string_t stored_result = StringVector::AddStringOrBlob(result, geometry_blob);

            free(start_geom);
            
            return stored_result;
        });
    
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}


inline void Temporal_lower_inc(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();

            Temporal* temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));

            bool lower_inc = temporal_lower_inc(temp);

            std::string result_str = lower_inc ? "true" : "false";
            string_t stored_result = StringVector::AddString(result, result_str);
            return stored_result;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

inline void Temporal_upper_inc(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();

            Temporal* temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));

            bool upper_inc = temporal_upper_inc(temp);

            std::string result_str = upper_inc ? "true" : "false";
            string_t stored_result = StringVector::AddString(result, result_str);
            return stored_result;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

inline void Temporal_start_instant(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));

            TInstant *start_inst = temporal_start_instant(temp);

            if (!start_inst) {
                throw InvalidInputException("Failed to get start_inst from temporal object");
            }

            size_t result_size = temporal_mem_size((Temporal*)start_inst);
            if (result_size == 0) {
                throw InvalidInputException("Invalid result size from temporal object");
            }

            uint8_t *result_buffer = (uint8_t*)malloc(result_size);
            if (!result_buffer) {
                free(start_inst);
                throw InvalidInputException("Failed to allocate memory for result");
            }
            
            memcpy(result_buffer, start_inst, result_size);
            string_t result_string_t(reinterpret_cast<const char*>(result_buffer), result_size);
            string_t stored_result = StringVector::AddStringOrBlob(result, result_string_t);
            
            free(result_buffer);
            free(start_inst);
            return stored_result;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

inline void Temporal_end_instant(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));

            TInstant *end_inst = temporal_end_instant(temp);

            if (!end_inst) {
                throw InvalidInputException("Failed to get end_inst from temporal object");
            }

            size_t result_size = temporal_mem_size((Temporal*)end_inst);
            if (result_size == 0) {
                throw InvalidInputException("Invalid result size from temporal object");
            }

            uint8_t *result_buffer = (uint8_t*)malloc(result_size);
            if (!result_buffer) {
                free(end_inst);
                throw InvalidInputException("Failed to allocate memory for result");
            }
            
            memcpy(result_buffer, end_inst, result_size);
            string_t result_string_t(reinterpret_cast<const char*>(result_buffer), result_size);
            string_t stored_result = StringVector::AddStringOrBlob(result, result_string_t);
            
            free(result_buffer);
            free(end_inst);
            return stored_result;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}




inline void Temporal_instant_n(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &tgeom_vec = args.data[0];
    auto &n_vec = args.data[1];
    
    BinaryExecutor::Execute<string_t, int32_t, string_t>(
        tgeom_vec, n_vec, result, count,
        [&](string_t tgeom_str, int32_t n) -> string_t {
            std::string tgeom = tgeom_str.GetString();
            
            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(tgeom.c_str()));
            
            TInstant *inst_n = temporal_instant_n(temp, n);
            if (!inst_n) {
                throw InvalidInputException("Failed to get instant n from temporal object");
            }
            
            size_t result_size = temporal_mem_size((Temporal*)inst_n);
            if (result_size == 0) {
                throw InvalidInputException("Invalid result size from temporal object");
            }
            
            uint8_t *result_buffer = (uint8_t*)malloc(result_size);
            if (!result_buffer) {
                free(inst_n);
                throw InvalidInputException("Failed to allocate memory for result");
            }
            
            memcpy(result_buffer, inst_n, result_size);
            string_t result_string_t(reinterpret_cast<const char*>(result_buffer), result_size);
            string_t stored_result = StringVector::AddStringOrBlob(result, result_string_t);
            
            free(result_buffer);
            free(inst_n);
            return stored_result;
        });
    
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}


inline void Tinstant_timestamptz(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_geom_vec = args.data[0];

    UnaryExecutor::Execute<string_t, timestamp_tz_t>(
        input_geom_vec, result, count,
        [&](string_t input_geom_str) -> timestamp_tz_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_geom_str.GetData());
            size_t data_size = input_geom_str.GetSize();

            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid tgeogpoint data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for tgeogpoint deserialization");
            }
            memcpy(data_copy, data, data_size);

            TInstant *temp = reinterpret_cast<TInstant*>(data_copy);
            
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid tgeogpoint data: null pointer");
            }

            TimestampTz meos_t = temp->t;
            
            timestamp_tz_t meos_timestamp{meos_t};
            timestamp_tz_t duckdb_t = MeosToDuckDBTimestamp(meos_timestamp);
            
            free(data_copy);
            
            return duckdb_t;
        }
    );

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

inline void ExecuteTGeometrySeq(DataChunk &args, ExpressionState &state, Vector &result) {
    const char* default_interp = "step";
    auto count = args.size();
    auto &tgeogpoint_vec = args.data[0];
    
    Vector interp_vec(LogicalType::VARCHAR, count);
    if (args.data.size() > 1) {
        interp_vec.Reference(args.data[1]);
    } else {
        for (idx_t i = 0; i < count; i++) {
            FlatVector::GetData<string_t>(interp_vec)[i] = StringVector::AddString(interp_vec, default_interp);
        }
    }
    
    BinaryExecutor::Execute<string_t, string_t, string_t>(
        tgeogpoint_vec, interp_vec, result, count,
        [&](string_t tgeom_blob, string_t interp_str) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
            size_t data_size = tgeom_blob.GetSize();
            
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid tgeogpoint data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for tgeogpoint deserialization");
            }
            memcpy(data_copy, data, data_size);

            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid tgeogpoint data: null pointer");
            }
            
            std::string interp_string = interp_str.GetString();
            if (interp_string.empty()) {
                interp_string = default_interp;
            }
            
            interpType interp = interptype_from_string(interp_string.c_str());
            TSequence *seq = temporal_as_tsequence(temp, interp);
            
            if (!seq) {
                free(data_copy);
                throw InvalidInputException("Failed to create TSequence");
            }
            
            size_t seq_data_size = temporal_mem_size(reinterpret_cast<Temporal*>(seq));
            uint8_t *seq_data_buffer = (uint8_t*)malloc(seq_data_size);
            if (!seq_data_buffer) {
                free(data_copy);
                free(seq);
                throw InvalidInputException("Failed to allocate memory for TSequence data");
            }
            
            memcpy(seq_data_buffer, seq, seq_data_size);
            
            string_t seq_data_string_t(reinterpret_cast<const char*>(seq_data_buffer), seq_data_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, seq_data_string_t);
            
            free(seq_data_buffer);
            free(data_copy);
            free(seq);
            
            return stored_data;
        });
    
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}




void TGeogpointType::RegisterScalarFunctions(ExtensionLoader &loader) {

    auto tgeogpoint_function = ScalarFunction(
        "tgeogpoint", 
        {LogicalType::VARCHAR}, 
        TGeogpointType::tgeogpoint(),
        Tgeogpoint_constructor
    );
    loader.RegisterFunction( tgeogpoint_function);
        
    auto tgeogpoint_from_timestamp_function = ScalarFunction(
        "tgeogpoint",
        {LogicalType::VARCHAR, LogicalType::TIMESTAMP_TZ}, 
        TGeogpointType::tgeogpoint(), 
        Tgeogpointinst_constructor);
    loader.RegisterFunction( tgeogpoint_from_timestamp_function);

     auto tgeogpoint_from_tstzspan_function = ScalarFunction(
        "tgeogpoint", 
        {LogicalType::VARCHAR, SpanTypes::tstzspan(), LogicalType::VARCHAR}, 
        TGeogpointType::tgeogpoint(),  
        Tgeogpoint_sequence_from_tstzspan
    );
    loader.RegisterFunction( tgeogpoint_from_tstzspan_function);

    auto tgeogpoint_from_tstzspan_default = ScalarFunction(
        "tgeogpoint", 
        {LogicalType::VARCHAR, SpanTypes::tstzspan()}, 
        TGeogpointType::tgeogpoint(),  
        Tgeogpoint_sequence_from_tstzspan
    );
    loader.RegisterFunction( tgeogpoint_from_tstzspan_default);

     auto tgeogpointseqarr_1param= ScalarFunction(
        "tgeogpointSeq", 
        {LogicalType::LIST(TGeogpointType::tgeogpoint())},
        TGeogpointType::tgeogpoint(),
        Tgeogpoint_sequence_constructor
    );
    loader.RegisterFunction( tgeogpointseqarr_1param);

    auto tgeogpointseqarr_2params = ScalarFunction(
        "tgeogpointSeq", 
        {LogicalType::LIST(TGeogpointType::tgeogpoint()), LogicalType::VARCHAR},
        TGeogpointType::tgeogpoint(),
        Tgeogpoint_sequence_constructor
    );
    loader.RegisterFunction( tgeogpointseqarr_2params);

    auto tgeogpointseqarr_3params = ScalarFunction(
        "tgeogpointSeq", 
        {LogicalType::LIST(TGeogpointType::tgeogpoint()), LogicalType::VARCHAR, LogicalType::BOOLEAN},
        TGeogpointType::tgeogpoint(),
        Tgeogpoint_sequence_constructor
    );
    loader.RegisterFunction( tgeogpointseqarr_3params);

    auto tgeogpointseqarr_4params = ScalarFunction(
        "tgeogpointSeq", 
        {LogicalType::LIST(TGeogpointType::tgeogpoint()), LogicalType::VARCHAR, LogicalType::BOOLEAN, LogicalType::BOOLEAN},
        TGeogpointType::tgeogpoint(),
        Tgeogpoint_sequence_constructor
    );
    loader.RegisterFunction( tgeogpointseqarr_4params);

    // timeSpan(tgeogpoint) is generated (meos_temporal_conversion, RETIRED_GROUPS).

    auto tgeogpoint_to_tinstant_function = ScalarFunction(
        "tgeogpointInst",
        {TGeogpointType::tgeogpoint()},
        TGeogpointType::tgeogpoint(),  
        Temporal_to_tinstant);
    loader.RegisterFunction( tgeogpoint_to_tinstant_function);


    auto setInterp_function = ScalarFunction(
        "setInterp",
        {TGeogpointType::tgeogpoint(), LogicalType::VARCHAR},
        TGeogpointType::tgeogpoint(),
        Temporal_set_interp
    );
    loader.RegisterFunction( setInterp_function);


    auto merge_function = ScalarFunction(
        "merge",
        {TGeogpointType::tgeogpoint(), TGeogpointType::tgeogpoint()},
        TGeogpointType::tgeogpoint(),
        Temporal_merge
    );
    loader.RegisterFunction( merge_function);

    auto tempSubtype_function = ScalarFunction(
        "tempSubtype",
        {TGeogpointType::tgeogpoint()},
        LogicalType::VARCHAR,
        Temporal_subtype
    );
    loader.RegisterFunction( tempSubtype_function);

    auto interp_function = ScalarFunction(
        "interp",
        {TGeogpointType::tgeogpoint()},
        LogicalType::VARCHAR,
        Temporal_interp
    );
    loader.RegisterFunction( interp_function);

    auto memSize_function = ScalarFunction(
        "memSize",
        {TGeogpointType::tgeogpoint()},
        LogicalType::INTEGER,
        Temporal_mem_size
    );
    loader.RegisterFunction( memSize_function);

    auto getValue_function = ScalarFunction(
        "getValue",
        {TGeogpointType::tgeogpoint()},
        MobilityDuckGeometryType(),
        Tinstant_value
    );
    loader.RegisterFunction( getValue_function);
    

    auto tgeogpoint_start_value_function = ScalarFunction(
        "startValue", 
        {TGeogpointType::tgeogpoint()},
        MobilityDuckGeometryType(),
        Temporal_start_value
    );
    loader.RegisterFunction( tgeogpoint_start_value_function);

    auto tgeogpoint_end_value_function = ScalarFunction(
        "endValue", 
        {TGeogpointType::tgeogpoint()},
        MobilityDuckGeometryType(),
        Temporal_end_value
    );
    loader.RegisterFunction( tgeogpoint_end_value_function);

    auto startInstant_function = ScalarFunction(
        "startInstant",
        {TGeogpointType::tgeogpoint()},
        TGeogpointType::tgeogpoint(), 
        Temporal_start_instant
    );
    loader.RegisterFunction( startInstant_function);

    auto endInstant_function = ScalarFunction(
        "endInstant",
        {TGeogpointType::tgeogpoint()},
        TGeogpointType::tgeogpoint(), 
        Temporal_end_instant
    );
    loader.RegisterFunction( endInstant_function);

    auto instantN_function = ScalarFunction(
        "instantN",
        {TGeogpointType::tgeogpoint(), LogicalType::INTEGER},
        TGeogpointType::tgeogpoint(),  
        Temporal_instant_n
    );
    loader.RegisterFunction( instantN_function);


    auto tgeogpoint_gettimestamptz_function = ScalarFunction(
        "getTimestamp",
        {TGeogpointType::tgeogpoint()},
        LogicalType::TIMESTAMP_TZ,
        Tinstant_timestamptz);
    loader.RegisterFunction( tgeogpoint_gettimestamptz_function);

    // ===================================================================
    // Foundational tgeogpoint surface — accessors, time/value-restrict,
    // modifiers, and comparison. The MEOS C functions delegated to here
    // are subtype-agnostic (they take Temporal *), so we reuse the same
    // generic handlers wired for tgeompoint in temporal_functions.cpp.
    // ===================================================================

    const LogicalType TGEOM = TGeogpointType::tgeogpoint();
    const LogicalType GEOM  = MobilityDuckGeometryType();
    const LogicalType TSTZ  = LogicalType::TIMESTAMP_TZ;
    const LogicalType IVAL  = LogicalType::INTERVAL;
    const LogicalType BIGI  = LogicalType::BIGINT;

    // ---- Accessors ----
    loader.RegisterFunction(ScalarFunction(
        "valueSet", {TGEOM}, SpatialSetType::geomset(),
        TemporalFunctions::Temporal_valueset));
    loader.RegisterFunction(ScalarFunction(
        "valueN", {TGEOM, BIGI}, GEOM,
        TemporalFunctions::Temporal_value_n));
    loader.RegisterFunction(ScalarFunction(
        "valueAtTimestamp", {TGEOM, TSTZ}, GEOM,
        TemporalFunctions::Temporal_value_at_timestamptz));
    loader.RegisterFunction(ScalarFunction(
        "getTime", {TGEOM}, SpansetTypes::tstzspanset(),
        TemporalFunctions::Temporal_time));
    loader.RegisterFunction(ScalarFunction(
        "duration", {TGEOM}, IVAL,
        TemporalFunctions::Temporal_duration));
    loader.RegisterFunction(ScalarFunction(
        "duration", {TGEOM, LogicalType::BOOLEAN}, IVAL,
        TemporalFunctions::Temporal_duration));
    loader.RegisterFunction(ScalarFunction(
        "lowerInc", {TGEOM}, LogicalType::BOOLEAN,
        TemporalFunctions::Temporal_lower_inc));
    loader.RegisterFunction(ScalarFunction(
        "upperInc", {TGEOM}, LogicalType::BOOLEAN,
        TemporalFunctions::Temporal_upper_inc));
    loader.RegisterFunction(ScalarFunction(
        "numInstants", {TGEOM}, LogicalType::INTEGER,
        TemporalFunctions::Temporal_num_instants));
    loader.RegisterFunction(ScalarFunction(
        "instants", {TGEOM}, LogicalType::LIST(TGEOM),
        TemporalFunctions::Temporal_instants));
    loader.RegisterFunction(ScalarFunction(
        "numSequences", {TGEOM}, LogicalType::INTEGER,
        TemporalFunctions::Temporal_num_sequences));
    loader.RegisterFunction(ScalarFunction(
        "sequences", {TGEOM}, LogicalType::LIST(TGEOM),
        TemporalFunctions::Temporal_sequences));
    loader.RegisterFunction(ScalarFunction(
        "startSequence", {TGEOM}, TGEOM,
        TemporalFunctions::Temporal_start_sequence));
    loader.RegisterFunction(ScalarFunction(
        "endSequence", {TGEOM}, TGEOM,
        TemporalFunctions::Temporal_end_sequence));
    loader.RegisterFunction(ScalarFunction(
        "sequenceN", {TGEOM, LogicalType::INTEGER}, TGEOM,
        TemporalFunctions::Temporal_sequence_n));
    loader.RegisterFunction(ScalarFunction(
        "numTimestamps", {TGEOM}, LogicalType::INTEGER,
        TemporalFunctions::Temporal_num_timestamps));
    // timestamps(tgeogpoint) is generated from the catalog (temporal_timestamps) in generated_temporal_udfs.cpp.
    loader.RegisterFunction(ScalarFunction(
        "startTimestamp", {TGEOM}, TSTZ,
        TemporalFunctions::Temporal_start_timestamptz));
    loader.RegisterFunction(ScalarFunction(
        "endTimestamp", {TGEOM}, TSTZ,
        TemporalFunctions::Temporal_end_timestamptz));
    loader.RegisterFunction(ScalarFunction(
        "timestampN", {TGEOM, LogicalType::INTEGER}, TSTZ,
        TemporalFunctions::Temporal_timestamptz_n));
    loader.RegisterFunction(ScalarFunction(
        "segments", {TGEOM}, LogicalType::LIST(TGEOM),
        TemporalFunctions::Temporal_segments));

    // ---- Time-domain restrict / minus ----
    for (const auto &t : std::vector<std::pair<LogicalType, scalar_function_t>>{
             {TSTZ,                       TemporalFunctions::Temporal_at_timestamptz},
             {SetTypes::tstzset(),        TemporalFunctions::Temporal_at_tstzset},
             {SpanTypes::tstzspan(),      TemporalFunctions::Temporal_at_tstzspan},
             {SpansetTypes::tstzspanset(), TemporalFunctions::Temporal_at_tstzspanset}}) {
        loader.RegisterFunction(ScalarFunction(
            "atTime", {TGEOM, t.first}, TGEOM, t.second));
    }
    for (const auto &t : std::vector<std::pair<LogicalType, scalar_function_t>>{
             {TSTZ,                       TemporalFunctions::Temporal_minus_timestamptz},
             {SetTypes::tstzset(),        TemporalFunctions::Temporal_minus_tstzset},
             {SpanTypes::tstzspan(),      TemporalFunctions::Temporal_minus_tstzspan},
             {SpansetTypes::tstzspanset(), TemporalFunctions::Temporal_minus_tstzspanset}}) {
        loader.RegisterFunction(ScalarFunction(
            "minusTime", {TGEOM, t.first}, TGEOM, t.second));
    }

    // beforeTimestamp / afterTimestamp accept timestamptz and tstzspan
    loader.RegisterFunction(ScalarFunction(
        "beforeTimestamp", {TGEOM, TSTZ}, TGEOM,
        TemporalFunctions::Temporal_before_timestamptz));
    loader.RegisterFunction(ScalarFunction(
        "afterTimestamp", {TGEOM, TSTZ}, TGEOM,
        TemporalFunctions::Temporal_after_timestamptz));

    // ---- Value restrict ----
    // atValues / minusValues over a single geometry funnel through
    // tgeompoint-side handlers (subtype-agnostic — they call MEOS's
    // tgeo_at_geom etc. under the hood) and over a geomset go through
    // the temporal-functions multi-value handlers.
    loader.RegisterFunction(ScalarFunction(
        "atValues", {TGEOM, GEOM}, TGEOM,
        TgeompointFunctions::Tgeompoint_at_value));
    loader.RegisterFunction(ScalarFunction(
        "atValues", {TGEOM, SpatialSetType::geomset()}, TGEOM,
        TemporalFunctions::Temporal_at_values));
    loader.RegisterFunction(ScalarFunction(
        "minusValues", {TGEOM, GEOM}, TGEOM,
        TemporalFunctions::Temporal_minus_value));
    loader.RegisterFunction(ScalarFunction(
        "minusValues", {TGEOM, SpatialSetType::geomset()}, TGEOM,
        TemporalFunctions::Temporal_minus_value));

    // ---- Spatial restrict (atGeometry / minusGeometry / atStbox / minusStbox)
    // Reuse tgeompoint's Tgeo_* handlers — they operate on Temporal * and
    // delegate to the subtype-agnostic MEOS `tgeo_at_geom` etc.
    loader.RegisterFunction(ScalarFunction(
        "atGeometry", {TGEOM, GEOM}, TGEOM,
        TgeompointFunctions::Tgeo_at_geom));
    loader.RegisterFunction(ScalarFunction(
        "minusGeometry", {TGEOM, GEOM}, TGEOM,
        TgeompointFunctions::Tgeo_minus_geom));
    loader.RegisterFunction(ScalarFunction(
        "minusStbox", {TGEOM, StboxType::stbox()}, TGEOM,
        TgeompointFunctions::Tgeo_minus_stbox));

    // ---- Modifiers (shift / scale / shiftScale / append / insert / update /
    // delete / round) ----
    loader.RegisterFunction(ScalarFunction(
        "shiftTime", {TGEOM, IVAL}, TGEOM,
        TemporalFunctions::Temporal_shift_time));
    loader.RegisterFunction(ScalarFunction(
        "scaleTime", {TGEOM, IVAL}, TGEOM,
        TemporalFunctions::Temporal_scale_time));
    loader.RegisterFunction(ScalarFunction(
        "shiftScaleTime", {TGEOM, IVAL, IVAL}, TGEOM,
        TemporalFunctions::Temporal_shift_scale_time));
    loader.RegisterFunction(ScalarFunction(
        "appendInstant", {TGEOM, TGEOM}, TGEOM,
        TemporalFunctions::Temporal_append_tinstant));
    loader.RegisterFunction(ScalarFunction(
        "appendSequence", {TGEOM, TGEOM}, TGEOM,
        TemporalFunctions::Temporal_append_tsequence));
    loader.RegisterFunction(ScalarFunction(
        "insert", {TGEOM, TGEOM}, TGEOM,
        TemporalFunctions::Temporal_insert));
    loader.RegisterFunction(ScalarFunction(
        "insert", {TGEOM, TGEOM, LogicalType::BOOLEAN}, TGEOM,
        TemporalFunctions::Temporal_insert));
    loader.RegisterFunction(ScalarFunction(
        "update", {TGEOM, TGEOM}, TGEOM,
        TemporalFunctions::Temporal_update));
    loader.RegisterFunction(ScalarFunction(
        "update", {TGEOM, TGEOM, LogicalType::BOOLEAN}, TGEOM,
        TemporalFunctions::Temporal_update));
    loader.RegisterFunction(ScalarFunction(
        "deleteTime", {TGEOM, TSTZ}, TGEOM,
        TemporalFunctions::Temporal_delete_timestamptz));
    loader.RegisterFunction(ScalarFunction(
        "deleteTime", {TGEOM, TSTZ, LogicalType::BOOLEAN}, TGEOM,
        TemporalFunctions::Temporal_delete_timestamptz));
    loader.RegisterFunction(ScalarFunction(
        "deleteTime", {TGEOM, SetTypes::tstzset()}, TGEOM,
        TemporalFunctions::Temporal_delete_tstzset));
    loader.RegisterFunction(ScalarFunction(
        "deleteTime", {TGEOM, SetTypes::tstzset(), LogicalType::BOOLEAN}, TGEOM,
        TemporalFunctions::Temporal_delete_tstzset));
    loader.RegisterFunction(ScalarFunction(
        "deleteTime", {TGEOM, SpanTypes::tstzspan()}, TGEOM,
        TemporalFunctions::Temporal_delete_tstzspan));
    loader.RegisterFunction(ScalarFunction(
        "deleteTime", {TGEOM, SpanTypes::tstzspan(), LogicalType::BOOLEAN}, TGEOM,
        TemporalFunctions::Temporal_delete_tstzspan));
    loader.RegisterFunction(ScalarFunction(
        "deleteTime", {TGEOM, SpansetTypes::tstzspanset()}, TGEOM,
        TemporalFunctions::Temporal_delete_tstzspanset));
    loader.RegisterFunction(ScalarFunction(
        "deleteTime", {TGEOM, SpansetTypes::tstzspanset(), LogicalType::BOOLEAN}, TGEOM,
        TemporalFunctions::Temporal_delete_tstzspanset));

    // ---- Comparison (named functions + operators) ----
    struct CmpEntry {
        const char *name;
        scalar_function_t fn;
    };
    const std::vector<CmpEntry> named_cmps = {
        {"temporal_eq", TemporalFunctions::Temporal_eq},
        {"temporal_ne", TemporalFunctions::Temporal_ne},
        {"temporal_lt", TemporalFunctions::Temporal_lt},
        {"temporal_le", TemporalFunctions::Temporal_le},
        {"temporal_gt", TemporalFunctions::Temporal_gt},
        {"temporal_ge", TemporalFunctions::Temporal_ge},
    };
    for (const auto &c : named_cmps) {
        loader.RegisterFunction(ScalarFunction(
            c.name, {TGEOM, TGEOM}, LogicalType::BOOLEAN, c.fn));
    }
    loader.RegisterFunction(ScalarFunction(
        "temporal_cmp", {TGEOM, TGEOM}, LogicalType::INTEGER,
        TemporalFunctions::Temporal_cmp));

    // Operator forms — mirror the registrations tgeompoint.cpp does.
    const std::vector<CmpEntry> op_cmps = {
        {"=",  TemporalFunctions::Temporal_eq},
        {"<>", TemporalFunctions::Temporal_ne},
        {"<",  TemporalFunctions::Temporal_lt},
        {"<=", TemporalFunctions::Temporal_le},
        {">",  TemporalFunctions::Temporal_gt},
        {">=", TemporalFunctions::Temporal_ge},
    };
    for (const auto &c : op_cmps) {
        loader.RegisterFunction(ScalarFunction(
            c.name, {TGEOM, TGEOM}, LogicalType::BOOLEAN, c.fn));
    }
}

void TGeogpointType::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType( "tgeogpoint", TGeogpointType::tgeogpoint());
}


} // namespace duckdb
