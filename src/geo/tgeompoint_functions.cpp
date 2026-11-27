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

#include "meos_wrapper_simple.hpp"
#include "common.hpp"

#include "geo/tgeompoint_functions.hpp"
#include "temporal/temporal_functions.hpp"
#include "time_util.hpp"
#include "geo_util.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/vector.hpp"
#include "duckdb/common/vector_operations/ternary_executor.hpp"
#include "duckdb/common/typedefs.hpp"

#include "duckdb_version_compat.hpp"

#include "duckdb/common/types.hpp"

#include <cfloat>
#include <cmath>

#include "meos_internal_geo.h"

namespace duckdb {

inline void Tspatial_to_stbox_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid tgeompoint data: null pointer");
                return string_t();
            }
            STBox *stbox = tspatial_to_stbox(temp);
            if (!stbox) {
                free(temp);
                throw InvalidInputException("Failed to convert tgeompoint to stbox");
                return string_t();
            }

            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
                free(temp);
                throw InvalidInputException("Failed to allocate memory for stbox");
                return string_t();
            }
            memcpy(stbox_data, stbox, stbox_size);
            string_t stbox_string(reinterpret_cast<const char*>(stbox_data), stbox_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, stbox_string);

            free(stbox_data);
            free(stbox);
            free(temp);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
 * Conversion functions
 ****************************************************/

inline void Temporal_to_tstzspan_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_to_tstzspan: unable to cast string to temporal");
            }

            Span *ret = (Span*)malloc(sizeof(Span));
            temporal_set_tstzspan(temp, ret);
            size_t span_size = sizeof(*ret);
            uint8_t *span_buffer = (uint8_t*) malloc(span_size);
            memcpy(span_buffer, ret, span_size);
            string_t span_string_t((char *) span_buffer, span_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
            free(span_buffer);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
 * Accessor functions
 ****************************************************/

/* ***************************************************
 * Stops function
 ****************************************************/

// void TgeompointFunctions::gs_as_text(DataChunk &args, ExpressionState &state, Vector &result) {
//     UnaryExecutor::Execute<string_t, string_t>(
//         args.data[0], result, args.size(),
//         [&](string_t blob) -> string_t {
//             const uint8_t *data = reinterpret_cast<const uint8_t*>(blob.GetData());
//             size_t data_size = blob.GetSize();
//             uint8_t *data_copy = (uint8_t*)malloc(data_size);
//             memcpy(data_copy, data, data_size);
//             GSERIALIZED *gs = reinterpret_cast<GSERIALIZED*>(data_copy);
//             if (!gs) {
//                 free(data_copy);
//                 throw InvalidInputException("Invalid GSERIALIZED data: null pointer");
//             }
//             char *gs_text = geo_as_ewkt(gs, 10);
//             if (!gs_text) {
//                 free(data_copy);
//                 throw InvalidInputException("Failed to convert GSERIALIZED to text");
//             }
//             string_t gs_text_string(gs_text);
//             string_t stored_result = StringVector::AddStringOrBlob(result, gs_text_string);
//             free(gs);
//             return stored_result;
//         }
//     );
//     if (args.size() == 1) {
//         result.SetVectorType(VectorType::CONSTANT_VECTOR);
//     }
// }

} // namespace duckdb
