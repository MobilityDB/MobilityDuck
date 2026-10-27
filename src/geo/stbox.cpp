#include "meos_wrapper_simple.hpp"
#include "duckdb_version_compat.hpp"

#include "common.hpp"
#include "geo/stbox.hpp"
#include "geo/stbox_functions.hpp"
#include "geo/tgeompoint.hpp"

#include "duckdb/common/types/blob.hpp"
#include "duckdb/function/function.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include <scoped_allocator>
#include "mobilityduck/meos_registration.hpp"

namespace duckdb {

LogicalType StboxType::stbox() {
    LogicalType type(LogicalTypeId::BLOB);
    type.SetAlias("stbox");
    return type;
}

void StboxType::RegisterType(ExtensionLoader &loader) {
    loader.RegisterType( "stbox", stbox());
}

void StboxType::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader, 
        LogicalType::VARCHAR,
        stbox(),
        StboxFunctions::Stbox_in_cast
    );

    RegisterMeosCastFunction(loader, 
        stbox(),
        LogicalType::VARCHAR,
        StboxFunctions::Stbox_out
    );

    RegisterMeosCastFunction(loader, 
        MobilityDuckGeometryType(),
        stbox(),
        StboxFunctions::Geo_to_stbox_cast
    );

    RegisterMeosCastFunction(loader, 
        LogicalType::TIMESTAMP_TZ,
        stbox(),
        StboxFunctions::Timestamptz_to_stbox_cast
    );

    RegisterMeosCastFunction(loader, 
        SetTypes::tstzset(),
        stbox(),
        StboxFunctions::Tstzset_to_stbox_cast
    );

    RegisterMeosCastFunction(loader, 
        SpanTypes::tstzspan(),
        stbox(),
        StboxFunctions::Tstzspan_to_stbox_cast
    );

    RegisterMeosCastFunction(loader, 
        SpansetTypes::tstzspanset(),
        stbox(),
        StboxFunctions::Tstzspanset_to_stbox_cast
    );
}

void StboxType::RegisterScalarFunctions(ExtensionLoader &loader) {
    /* Tspatial × {stbox, tspatial} topological predicates — the operators
     * (~=/@>/<@/&&/-|-) and the canonical bare named functions (same/contains/
     * contained/overlaps/adjacent, the names deployed by MobilityDB's .in.sql) —
     * are generated from the MEOS-API catalog; see
     * src/generated/generated_temporal_udfs.cpp. (reg_names also registers the
     * catalog-only *_bbox @sqlfn backing tag as an alias; that is internal, not a
     * user-facing canonical name.) */
}

} // namespace duckdb
