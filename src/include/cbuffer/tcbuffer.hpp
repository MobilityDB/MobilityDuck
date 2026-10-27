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

/* cbuffer is a circular buffer (a point + a radius); a varlena value surfaced as
 * a BLOB.  tcbuffer is its temporal counterpart, stored as a Temporal* blob.
 * Canonical names per meos_catalog.c ([T_CBUFFER]="cbuffer", [T_TCBUFFER]="tcbuffer").
 * This binding registers only the type + text I/O boundary; the operation surface
 * (constructors, accessors, predicates, distance, …) is generated from the
 * MEOS-API catalog into src/generated/. */
struct CbufferTypes {
    static LogicalType cbuffer();
    static LogicalType tcbuffer();

    static void RegisterTypes(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
};

struct CbufferFunctions {
    /* In/out — static cbuffer value */
    static bool Cbuffer_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Cbuffer_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* In/out — tcbuffer temporal value */
    static bool Tcbuffer_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tcbuffer_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
};

} // namespace duckdb
