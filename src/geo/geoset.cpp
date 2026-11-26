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

#include "geo/geoset.hpp"
#include "duckdb_version_compat.hpp"
#include "tydef.hpp"
#include "geo_util.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "mobilityduck/meos_registration.hpp"
#include "temporal/set_functions.hpp"

extern "C" {    
    #include "meos.h"    
    #include "meos_internal.h"   
    #include "meos_geo.h"   
    #include "meos_internal_geo.h"     
}

namespace duckdb {

LogicalType SpatialSetType::geomset() {
    auto type = LogicalType(LogicalTypeId::BLOB);     
    type.SetAlias("geomset");
    return type;
}

LogicalType SpatialSetType::geogset() {
    auto type = LogicalType(LogicalTypeId::BLOB);     
    type.SetAlias("geogset");
    return type;
}

void SpatialSetType::RegisterTypes(ExtensionLoader &loader){
    loader.RegisterType( "geomset", geomset());
    loader.RegisterType( "geogset", geogset());
}

void SpatialSetType::RegisterCastFunctions(ExtensionLoader &loader) {        
    // A geometry set writes its text as every spatial set does: its WKT, through
    // spatialset_as_text
}

} // namespace duckdb
