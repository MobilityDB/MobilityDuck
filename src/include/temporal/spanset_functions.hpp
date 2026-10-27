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

struct SpansetFunctions{
    // for cast
    static bool Spanset_to_text(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Text_to_spanset(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Value_to_spanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Set_to_spanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Span_to_spanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Spanset_to_span_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Intspanset_to_floatspanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Floatspanset_to_intspanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Datespanset_to_tstzspanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tstzspanset_to_datespanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    // TODO: Multirange functions

    // Transformations functions
    // floor/ceil/round/degrees/radians on floatspanset are generated (generated_temporal_udfs.cpp).
    
};


} // namespace duckdb
