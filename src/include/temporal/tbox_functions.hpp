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
#include "duckdb/common/typedefs.hpp"

#include "span.hpp"
#include "set.hpp"
#include "spanset.hpp"

namespace duckdb {

class ExtensionLoader;

#define OUT_DEFAULT_DECIMAL_DIGITS 15
#define Float8GetDatum(X) ((Datum) *(uint64_t *) &(X))
#define Int32GetDatum(X) ((Datum) (X))

struct TboxFunctions {
    /* ***************************************************
     * In/out functions: VARCHAR <-> tbox
     ****************************************************/
    static bool Tbox_in(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tbox_out(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* ***************************************************
     * Conversion functions + cast functions: [TYPE] -> tbox
     ****************************************************/
    template <typename TA>
    static void NumberToTboxExecutor(Vector &value, MeosType basetype, Vector &result, idx_t count);
    static bool Number_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    // TODO: what is duckdb equivalent of numeric?

    static void TimestamptzToTboxExecutor(Vector &value, Vector &result, idx_t count);
    static bool Timestamptz_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    static void SetToTboxExecutor(Vector &value, Vector &result, idx_t count);
    static bool Set_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    static void SpanToTboxExecutor(Vector &value, Vector &result, idx_t count);
    static bool Span_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    static void SpansetToTboxExecutor(Vector &value, Vector &result, idx_t count); 
    static bool Spanset_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    /* ***************************************************
     * Conversion functions + cast functions: tbox -> [TYPE]
     ****************************************************/
    static void TboxToIntspanExecutor(Vector &value, Vector &result, idx_t count);
    static bool Tbox_to_intspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    static void TboxToFloatspanExecutor(Vector &value, Vector &result, idx_t count);
    static bool Tbox_to_floatspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    static void TboxToTstzspanExecutor(Vector &value, Vector &result, idx_t count);
    static bool Tbox_to_tstzspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    // TODO: Selectivity functions
    
    // TODO: extend aggregation

    /* ***************************************************
     * WKB / hex-WKB serialization + hashing
     ****************************************************/
};

} // namespace duckdb
