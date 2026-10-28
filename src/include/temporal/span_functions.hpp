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

#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include <tydef.hpp>

extern "C" {
    #include <meos.h>
    #include <meos_internal.h>
}

namespace duckdb {


struct SpanFunctions {
    // for cast
    static bool Span_to_text(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Text_to_span(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Value_to_span_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Intspan_to_floatspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Floatspan_to_intspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Datespan_to_tstzspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tstzspan_to_datespan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Set_to_span_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    // TODO (Type Range): static bool Range_to_span_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    // TODO (Type Range): static bool Span_to_range_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    // scalar functions
    // TODO (Type Range): static void Range_to_span(DataChunk &args, ExpressionState &state, Vector &result);
    // TODO (Type Range): static void Span_to_range(DataChunk &args, ExpressionState &state, Vector &result);
        // Set operators
    
};



} // namespace duckdb
