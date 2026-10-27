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

/* trgeometry is a temporal rigid geometry: a reference geometry carried
 * unchanged plus a temporal pose placing it, so its base value is `pose`
 * (meos_catalog.c: [T_TRGEOMETRY].temptype_basetype = T_POSE) and the pose
 * type the PoseTypes family registers is the one its accessors answer in.
 * Canonical name per meos_catalog.c ([T_TRGEOMETRY]="trgeometry"); `trgeo`
 * is the file/helper abbreviation only, never the user-visible name.
 *
 * The reference geometry is appended at the END of the varlena, and
 * `VARSIZE` — which `temporal_mem_size` returns — spans it, so the temporal
 * blob marshaller carries the geometry with the value.  Its text I/O is
 * `trgeometry_out`, NOT the generic `temporal_out`: an op that walks only
 * the pose skeleton drops the appended geometry.
 *
 * This binding registers only the type + text I/O boundary; the operation
 * surface is generated from the MEOS-API catalog into src/generated/. */
struct TrgeometryTypes {
    static LogicalType trgeometry();

    static void RegisterTypes(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
};

struct TrgeometryFunctions {
    static bool Trgeometry_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Trgeometry_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
};

} // namespace duckdb
