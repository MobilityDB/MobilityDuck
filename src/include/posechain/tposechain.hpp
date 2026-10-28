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

/* posechain is a directed chain of rigid-body poses — one topocentric frame at
 * the outside and a link per joint — carried as a varlena.  tposechain is its
 * temporal counterpart, stored as a Temporal* blob.  Canonical names per
 * meos_catalog.c ([T_POSECHAIN]="posechain", [T_TPOSECHAIN]="tposechain"), and
 * the temporal type's base value is `posechain` ([T_TPOSECHAIN].
 * temptype_basetype = T_POSECHAIN).
 *
 * The value is a fixed-size varlena (vl_len_ · flags · srid · count · count ×
 * doubles), so it rides the generic temporal operations the way tpose does —
 * unlike trgeometry, which appends a reference geometry and therefore owns a
 * `trgeometry_*` counterpart for each of them.
 *
 * This binding registers only the type + text I/O boundary; the operation
 * surface is generated from the MEOS-API catalog into src/generated/. */
struct PosechainTypes {
    static LogicalType posechain();
    static LogicalType tposechain();

    static void RegisterTypes(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
};

struct PosechainFunctions {
    /* In/out — static posechain value */
    static bool Posechain_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Posechain_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* In/out — tposechain temporal value */
    static bool Tposechain_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tposechain_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
};

} // namespace duckdb
