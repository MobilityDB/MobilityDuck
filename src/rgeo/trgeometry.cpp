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

/* MobilityDuck binding for the MEOS temporal rigid geometry type (trgeometry).
 *
 * trgeometry is a reference geometry carried unchanged plus a temporal pose
 * placing it, stored as a Temporal* blob (BLOB) whose varlena APPENDS the
 * reference geometry after the pose skeleton.
 *
 * Only the type registration + text I/O boundary is hand-written here; the
 * operation surface is generated from the MEOS-API catalog into src/generated/.
 */

#include "rgeo/trgeometry.hpp"
#include "temporal/temporal.hpp"
#include "tydef.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "mobilityduck/meos_registration.hpp"
#include "time_util.hpp"

extern "C" {
    #include <meos.h>
    #include <meos_pose.h>
    #include <meos_rgeo.h>
    #include <meos_internal.h>
}

#include <cstdlib>
#include <cstring>
#include <string>

namespace duckdb {

LogicalType TrgeometryTypes::trgeometry() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("trgeometry");
    return type;
}

void TrgeometryTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType("trgeometry", trgeometry());
}

namespace {

inline Temporal *BlobToTemp(string_t blob) {
    size_t sz = blob.GetSize();
    uint8_t *copy = (uint8_t *) malloc(sz);
    memcpy(copy, blob.GetData(), sz);
    return reinterpret_cast<Temporal *>(copy);
}

/* `temporal_mem_size` returns VARSIZE, which spans the appended reference
 * geometry as well as the pose skeleton, so the whole value travels. */
inline string_t TempToBlob(Vector &result, Temporal *t) {
    if (!t) return string_t();
    size_t sz = temporal_mem_size(t);
    string_t out = StringVector::AddStringOrBlob(
        result, string_t(reinterpret_cast<const char *>(t), sz));
    free(t);
    return out;
}

} // namespace

/* =====================================================================
 * In / out — trgeometry temporal value
 * ===================================================================== */

bool TrgeometryFunctions::Trgeometry_in_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t s) -> string_t {
            std::string str(s.GetData(), s.GetSize());
            Temporal *t = trgeometry_in(str.c_str());
            return TempToBlob(result, t);
        });
    return true;
}

bool TrgeometryFunctions::Trgeometry_out_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            Temporal *t = BlobToTemp(blob);
            /* Not temporal_out: the generic form walks the pose skeleton
             * alone and loses the appended reference geometry. */
            char *str = trgeometry_out(t);
            free(t);
            std::string copy(str);
            free(str);
            return StringVector::AddString(result, copy);
        });
    return true;
}

/* =====================================================================
 * Registration
 * ===================================================================== */

void TrgeometryTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader, LogicalType::VARCHAR, trgeometry(),
        TrgeometryFunctions::Trgeometry_in_cast);
    RegisterMeosCastFunction(loader, trgeometry(), LogicalType::VARCHAR,
        TrgeometryFunctions::Trgeometry_out_cast);
}

} // namespace duckdb
