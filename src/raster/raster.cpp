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

/* MobilityDuck binding for MEOS's raster (`meos_raster.h`).
 *
 * RASTER is stored as a BLOB holding the MEOS WKB serialisation of the raster,
 * so a raster survives a Parquet round trip and matches the hex-encoded text
 * form of the VARCHAR casts. Only the type registration and its text casts
 * are hand-written here, as for RAQUET in raquet.cpp; the operation surface
 * is generated from the MEOS-API catalog into src/generated/, whose preamble
 * carries the same BlobToRaster and RasterToBlob as the ones below.
 */

#include "raster/raster.hpp"
#include "tydef.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "mobilityduck/meos_registration.hpp"

extern "C" {
    #include <meos.h>
    #include <meos_geo.h>
    #include <meos_raster.h>
}

#include <cstdlib>
#include <string>

namespace duckdb {

LogicalType RasterTypes::raster() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("raster");
    return type;
}

void RasterTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType("raster", raster());
}

namespace {

inline Raster *BlobToRaster(string_t blob) {
    Raster *rast = raster_from_wkb((const uint8_t *)blob.GetData(), blob.GetSize());
    if (!rast) {
        throw InvalidInputException("raster: invalid raster serialisation");
    }
    return rast;
}

inline string_t RasterToBlob(Vector &result, Raster *rast) {
    size_t sz = 0;
    uint8_t *wkb = raster_as_wkb(rast, 0, &sz);
    free(rast);
    if (!wkb) {
        throw InvalidInputException("raster: cannot serialise the raster");
    }
    string_t out = StringVector::AddStringOrBlob(result, (const char *)wkb, sz);
    free(wkb);
    return out;
}

} // namespace

/* =====================================================================
 * In / out
 *
 * The text of a raster is its hex-encoded WKB, the text of PostgreSQL's
 * raster, written little-endian (WKB_NDR) whatever the byte order of the
 * machine, so the same raster reads the same text on every platform.
 * ===================================================================== */

bool RasterFunctions::Raster_in_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t s) -> string_t {
            std::string str(s.GetData(), s.GetSize());
            Raster *rast = raster_from_hexwkb(str.c_str());
            if (!rast) throw InvalidInputException("raster: invalid raster text");
            return RasterToBlob(result, rast);
        });
    return true;
}

bool RasterFunctions::Raster_out_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            Raster *rast = BlobToRaster(blob);
            size_t sz = 0;
            char *str = raster_as_hexwkb(rast, WKB_NDR, &sz);
            free(rast);
            if (!str) throw InvalidInputException("raster: cannot write the raster as text");
            std::string copy(str, sz);
            free(str);
            return StringVector::AddString(result, copy);
        });
    return true;
}

/* =====================================================================
 * Registration
 * ===================================================================== */

void RasterTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader, LogicalType::VARCHAR, raster(),
        RasterFunctions::Raster_in_cast);
    RegisterMeosCastFunction(loader, raster(), LogicalType::VARCHAR,
        RasterFunctions::Raster_out_cast);
}

} // namespace duckdb
