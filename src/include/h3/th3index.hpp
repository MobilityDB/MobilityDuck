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

#pragma once

#include "meos_wrapper_simple.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

namespace duckdb {

/* H3INDEX is a 64-bit H3 cell id, surfaced as BIGINT (the uint64 bit pattern
 * reinterprets losslessly; equality/ordering care only about the bits).
 * H3 cells are geography-like — inherently WGS84 / EPSG:4326.
 * TH3INDEX is the temporal H3 cell index, stored as a Temporal* blob (BLOB).
 *
 * These type registrations are the justified hand layer (like QUADBIN /
 * TQUADBIN); the H3 function surface is generated from the MEOS-API catalog. */
struct H3indexTypes {
    static LogicalType h3index();
    static LogicalType th3index();

    static void RegisterTypes(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
};

struct H3indexFunctions {
    /* In/out — static h3index cell (BIGINT), H3 hex-string form */
    static bool H3index_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool H3index_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* In/out — th3index temporal value */
    static bool Th3index_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Th3index_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
};

} // namespace duckdb
