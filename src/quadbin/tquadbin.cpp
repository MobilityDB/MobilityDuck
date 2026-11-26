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

/* MobilityDuck binding for the MEOS QUADBIN cell index types (quadbin +
 * tquadbin).
 *
 * QUADBIN is surfaced as UBIGINT, the unsigned 64-bit cell id MEOS orders it by.
 * TQUADBIN is the temporal cell index, stored as a Temporal* blob (BLOB).
 * The analogue of H3INDEX / TH3INDEX for the square Web-Mercator DGGS.
 *
 * Only the TYPE registration + text I/O casts are hand-written here, as for
 * H3INDEX/TH3INDEX. The QUADBIN function surface is generated from the
 * MEOS-API catalog. */

#include "quadbin/tquadbin.hpp"
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
    #include <meos_quadbin.h>
    #include <meos_internal.h>
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace duckdb {

LogicalType QuadbinTypes::quadbin() {
    LogicalType type = LogicalType::UBIGINT;
    type.SetAlias("quadbin");
    return type;
}

LogicalType QuadbinTypes::tquadbin() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("tquadbin");
    return type;
}

void QuadbinTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType("quadbin",  quadbin());
    loader.RegisterType("tquadbin", tquadbin());
}

void QuadbinTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader, quadbin(), LogicalType::BIGINT,
        QuadbinFunctions::Quadbin_to_bigint_cast);
}

namespace {

inline Temporal *BlobToTemp(string_t blob) {
    size_t sz = blob.GetSize();
    uint8_t *copy = (uint8_t *) malloc(sz);
    memcpy(copy, blob.GetData(), sz);
    return reinterpret_cast<Temporal *>(copy);
}

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
 * Bigint casts — the two MobilityDB declares (quadbin/350_quadbin.in.sql):
 * quadbin AS bigint WITHOUT FUNCTION, the same 64 bits read signed, and
 * bigint AS quadbin WITH FUNCTION quadbin(bigint), which raises when the
 * integer encodes no cell.
 * ===================================================================== */

bool QuadbinFunctions::Quadbin_to_bigint_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<uint64_t, int64_t>(
        source, result, count,
        [&](uint64_t v) -> int64_t { return static_cast<int64_t>(v); });
    return true;
}

} // namespace duckdb
