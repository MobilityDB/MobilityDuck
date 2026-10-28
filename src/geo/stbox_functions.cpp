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

#include "geo/stbox_functions.hpp"
#include "time_util.hpp"
#include "geo_util.hpp"
#include <cfloat>

#include "duckdb/common/exception.hpp"
#include "duckdb/common/vector.hpp"
#include "duckdb/common/typedefs.hpp"

#include <cmath>
#include <string>

#include "duckdb_version_compat.hpp"

namespace duckdb {

namespace {

/* For stbox_to_geo: 2D geodetic box via MEOS constructor (no Z dimension in output geometry). */

inline void Stbox_normalize_geodetic_srid(STBox *box) {
    if ((stbox_isgeodetic(box) || MEOS_FLAGS_GET_GEODETIC(box->flags)) && box->srid == 0) {
        box->srid = 4326;
    }
}

} // namespace

/* ***************************************************
 * In/out functions: VARCHAR <-> stbox
 ****************************************************/

inline void Stbox_in_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(
        source, result, count,
        [&](string_t input_string, ValidityMask &mask, idx_t idx) -> string_t {
            std::string input_str = input_string.GetString();
            STBox *stbox = stbox_in(input_str.c_str());
            if (!stbox) {
                throw InternalException("Failure in Stbox_in: unable to cast string to stbox");
                return string_t();
            }
            if (input_str.find("GEODSTBOX") != std::string::npos) {
                MEOS_FLAGS_SET_GEODETIC(stbox->flags, true);
            }
            Stbox_normalize_geodetic_srid(stbox);
            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
                free(stbox);
                throw InternalException("Failure in Stbox_in: unable to allocate memory for stbox");
                return string_t();
            }
            memcpy(stbox_data, stbox, stbox_size);
            string_t ret_str(reinterpret_cast<const char*>(stbox_data), stbox_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(stbox_data);
            free(stbox);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

bool StboxFunctions::Stbox_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Stbox_in_common(source, result, count);
    return true;
}

bool StboxFunctions::Stbox_out(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    bool success = true;
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid stbox data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_out: unable to cast binary to stbox");
            }
            char *ret = stbox_out(stbox, OUT_DEFAULT_DECIMAL_DIGITS);
            if (!ret) {
                free(data_copy);
                throw InternalException("Failure in Stbox_out: unable to cast binary to stbox");
            }
            std::string ret_str(ret);
            free(ret);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);

            free(stbox);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
    return success;
}

/* ***************************************************
 * Conversion functions + cast functions: [TYPE] -> stbox
 ****************************************************/

void StboxFunctions::Geo_to_stbox_common(Vector &source, Vector &result, idx_t count) {
    // the box of a geometry carries the SRID its type states (GeometryCrsSrid)
    const int32 srid = GeometryCrsSrid(source.GetType());
    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(
        source, result, count,
        [&](string_t geometry_blob, ValidityMask &mask, idx_t idx) -> string_t {
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
                return string_t();
            }
            STBox *ret = geo_to_stbox(gs);
            if (!ret) {
                free(gs);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
                free(ret);
                free(gs);
                throw InternalException("Failure in Geo_to_stbox: unable to allocate memory for stbox");
                return string_t();
            }
            memcpy(stbox_data, ret, stbox_size);
            string_t ret_str(reinterpret_cast<const char*>(stbox_data), stbox_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(stbox_data);
            free(ret);
            free(gs);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

bool StboxFunctions::Geo_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Geo_to_stbox_common(source, result, count);
    return true;
}

static void Timestamptz_to_stbox_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::Execute<timestamp_tz_t, string_t>(
        source, result, count,
        [&](timestamp_tz_t ts_duckdb) -> string_t {
            timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
            STBox *ret = timestamptz_to_stbox((TimestampTz)ts_meos.value);
            if (!ret) {
                throw InternalException("Failure in Timestamptz_to_stbox: unable to convert timestamptz to stbox");
            }
            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
                free(ret);
                throw InternalException("Failure in Timestamptz_to_stbox: unable to allocate memory for stbox");
            }
            memcpy(stbox_data, ret, stbox_size);
            string_t ret_str(reinterpret_cast<const char*>(stbox_data), stbox_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(stbox_data);
            free(ret);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

bool StboxFunctions::Timestamptz_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Timestamptz_to_stbox_common(source, result, count);
    return true;
}

static void Tstzset_to_stbox_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_stbox) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            // A set carries its elements after the header, so its blob is at least the header
            // and longer for every element past the first: the guard is a minimum, not a match.
            if (data_size < sizeof(Set)) {
                throw InvalidInputException("Invalid TSTZSET data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Set *set = reinterpret_cast<Set*>(data_copy);
            if (!set) {
                free(data_copy);
                throw InternalException("Failure in Tstzset_to_stbox: unable to cast binary to set");
            }
            STBox *ret = tstzset_to_stbox(set);
            if (!ret) {
                free(set);
                throw InternalException("Failure in Tstzset_to_stbox: unable to convert tstzset to stbox");
            }
            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
                free(ret);
                free(set);
                throw InternalException("Failure in Tstzset_to_stbox: unable to allocate memory for stbox");
            }
            memcpy(stbox_data, ret, stbox_size);
            string_t ret_str(reinterpret_cast<const char*>(stbox_data), stbox_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(stbox_data);
            free(ret);
            free(set);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

bool StboxFunctions::Tstzset_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Tstzset_to_stbox_common(source, result, count);
    return true;
}

static void Tstzspan_to_stbox_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_stbox) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(Span)) {
                throw InvalidInputException("Invalid tstzspan data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Span *span = reinterpret_cast<Span*>(data_copy);
            if (!span) {
                free(data_copy);
                throw InternalException("Failure in Tstzspan_to_stbox: unable to cast binary to span");
            }
            STBox *ret = tstzspan_to_stbox(span);
            if (!ret) {
                free(span);
                throw InternalException("Failure in Tstzspan_to_stbox: unable to convert tstzspan to stbox");
            }
            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
                free(ret);
                free(span);
                throw InternalException("Failure in Tstzspan_to_stbox: unable to allocate memory for stbox");
            }
            memcpy(stbox_data, ret, stbox_size);
            string_t ret_str(reinterpret_cast<const char*>(stbox_data), stbox_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(stbox_data);
            free(ret);
            free(span);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

bool StboxFunctions::Tstzspan_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Tstzspan_to_stbox_common(source, result, count);
    return true;
}   

static void Tstzspanset_to_stbox_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_stbox) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            // Likewise a span set: sizeof covers the header and one span, never a longer one.
            if (data_size < sizeof(SpanSet)) {
                throw InvalidInputException("Invalid TSTZSPANSET data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            SpanSet *set = reinterpret_cast<SpanSet*>(data_copy);
            if (!set) {
                free(data_copy);
                throw InternalException("Failure in Tstzspanset_to_stbox: unable to cast binary to span set");
            }
            STBox *ret = tstzspanset_to_stbox(set);
            if (!ret) {
                free(set);
                throw InternalException("Failure in Tstzspanset_to_stbox: unable to convert tstzspanset to stbox");
            }
            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
                free(ret);
                free(set);
                throw InternalException("Failure in Tstzspanset_to_stbox: unable to allocate memory for stbox");
            }
            memcpy(stbox_data, ret, stbox_size);
            string_t ret_str(reinterpret_cast<const char*>(stbox_data), stbox_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(stbox_data);
            free(ret);
            free(set);
            return stored_data;
        }
    );
}

bool StboxFunctions::Tstzspanset_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Tstzspanset_to_stbox_common(source, result, count);
    return true;
}

/* ***************************************************
 * Topological operators
 ****************************************************/

} // namespace duckdb
