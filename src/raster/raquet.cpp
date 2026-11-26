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

/* MobilityDuck binding for the MEOS raster tile (`meos_raster.h`): the `raquet`
 * Web-Mercator raster tile addressed by a QUADBIN cell.
 *
 * RAQUET is stored as a BLOB holding the MEOS WKB serialisation of the tile,
 * so a tile survives a Parquet round trip and matches the hex-encoded text
 * form produced by the VARCHAR cast.
 *
 * Only the type registration, its text casts and the tile constructor are
 * hand-written here; the operation surface is generated from the MEOS-API
 * catalog into src/generated/.
 */

#include "raster/raquet.hpp"
#include "quadbin/tquadbin.hpp"
#include "tydef.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "mobilityduck/meos_registration.hpp"

extern "C" {
    #include <meos.h>
    #include <meos_internal.h>
    #include <meos_raster.h>
}

#include <cstdlib>
#include <cstring>
#include <string>

namespace duckdb {

namespace {

/* A tile is stored as its memory image (raquet_copy / raquet_mem_size), as the
 * catalog's sqlTypes state it and as the generated preamble carries it. */
inline Raquet *BlobToRaquet(string_t blob) {
    size_t sz = blob.GetSize();
    void *copy = malloc(sz);
    memcpy(copy, blob.GetData(), sz);
    return static_cast<Raquet *>(copy);
}

inline string_t RaquetToBlob(Vector &result, Raquet *rq) {
    string_t out = StringVector::AddStringOrBlob(result,
        (const char *) rq, (size_t) raquet_mem_size(rq));
    free(rq);
    return out;
}

} // namespace

/* =====================================================================
 * In / out
 *
 * The text form of a tile is its hex-encoded WKB, the representation
 * MobilityDB's raquet_in / raquet_out use.
 * ===================================================================== */

bool RaquetFunctions::Raquet_in_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t s) -> string_t {
            std::string str(s.GetData(), s.GetSize());
            Raquet *rq = raquet_in(str.c_str());
            if (!rq) throw InvalidInputException("raquet_in: invalid tile text");
            return RaquetToBlob(result, rq);
        });
    return true;
}

/* =====================================================================
 * Registration
 * ===================================================================== */

void RaquetTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader, LogicalType::VARCHAR, raquet(),
        RaquetFunctions::Raquet_in_cast);
}

} // namespace duckdb
