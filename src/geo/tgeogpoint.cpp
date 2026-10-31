/*****************************************************************************
 *
 * This MobilityDB code is provided under The PostgreSQL License.
 * Copyright (c) 2016-2026, Université libre de Bruxelles and MobilityDB
 * contributors
 *
 * MobilityDB includes portions of PostGIS version 3 source code released
 * under the GNU General Public License (GPLv2 or later).
 * Copyright (c) 2001-2026, PostGIS contributors
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without a written
 * agreement is hereby granted, provided that the above copyright notice and
 * this paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL UNIVERSITE LIBRE DE BRUXELLES BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
 * LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION,
 * EVEN IF UNIVERSITE LIBRE DE BRUXELLES HAS BEEN ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * UNIVERSITE LIBRE DE BRUXELLES SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON
 * AN "AS IS" BASIS, AND UNIVERSITE LIBRE DE BRUXELLES HAS NO OBLIGATIONS TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 *****************************************************************************/

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
#include "mobilityduck/meos_registration.hpp"

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

    const auto varchar_list = LogicalType::LIST(LogicalType::VARCHAR);

    duckdb::RegisterMeosFunction(loader,
        ScalarFunction(
            "asText",
            {LogicalType::LIST(tgeogpoint())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );
    duckdb::RegisterMeosFunction(loader,
        ScalarFunction(
            "asText",
            {LogicalType::LIST(tgeogpoint()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );

    duckdb::RegisterMeosFunction(loader,
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(tgeogpoint())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );
    duckdb::RegisterMeosFunction(loader,
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(tgeogpoint()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );

    // stbox(tgeogpoint) and shiftScaleTime(tgeogpoint, interval, interval) are generated from the
    // catalog (meos_geo_conversion, meos_temporal_transf); MobilityDB declares makeSimple over
    // tgeompoint alone.
}

} // namespace duckdb
