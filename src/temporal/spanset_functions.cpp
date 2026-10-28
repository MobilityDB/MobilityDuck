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

#include "temporal/spanset.hpp"
#include "temporal/spanset_functions.hpp"
#include "temporal/span.hpp"
#include "temporal/set.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include "time_util.hpp"


extern "C" {     
    #include "meos.h"    
    #include "meos_internal.h"   
    #include "meos_geo.h"
}

namespace duckdb {


// --- Cast to text ---
bool SpansetFunctions::Spanset_to_text(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    source.Flatten(count);
    auto result_data = FlatVector::GetData<string_t>(result); 

    for (idx_t i = 0; i < count; ++i) {
        if (FlatVector::IsNull(source, i)) {
            FlatVector::SetNull(result, i, true);
            continue;
        }

        Value val = source.GetValue(i);
        const string_t &blob = StringValue::Get(val);
        const uint8_t *data = (const uint8_t *)(blob.GetData());
        size_t size = blob.GetSize();

        SpanSet *s = (SpanSet*)malloc(size);
        memcpy(s, data, size);        

        char *cstr = spanset_out(s, 15);  
        result_data[i] = StringVector::AddString(result, cstr);

        free(cstr);
        free(s);
    }

    result.SetVectorType(VectorType::FLAT_VECTOR);
    return true;
}

bool SpansetFunctions::Text_to_spanset(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    source.Flatten(count);
    result.SetVectorType(VectorType::FLAT_VECTOR);

    auto target_type = result.GetType();
    MeosType spanset_type = SpansetTypeMapping::GetMeosTypeFromAlias(target_type.GetAlias());

    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_str) -> string_t {
            std::string str = input_str.GetString();

            SpanSet *s = spanset_in(str.c_str(), spanset_type);            

            string_t result_blob = StringVector::AddStringOrBlob(result, (const char *)s, spanset_mem_size(s));
            free(s);
            return result_blob;
        }
    );

    return true;
}


// --- Conversion: base type -> spanset ---

static inline void Write_spanset(Vector &result, idx_t row, SpanSet *s) {
    auto out = FlatVector::GetData<string_t>(result);
    out[row] = StringVector::AddStringOrBlob(result, (const char *)s, spanset_mem_size(s));
    free(s);
}

static inline void Value_to_spanset_core(Vector &source, Vector &result, idx_t count, MeosType base_type) {
    source.Flatten(count);
    result.SetVectorType(VectorType::FLAT_VECTOR);

    auto handle_null = [&](idx_t row) { FlatVector::SetNull(result, row, true); };

    switch (base_type) {
        case T_INT4: {
            auto in = FlatVector::GetData<int32_t>(source);
            for (idx_t i = 0; i < count; ++i) {
                if (FlatVector::IsNull(source, i)) { handle_null(i); continue; }
                SpanSet *s = value_spanset(Datum(in[i]), T_INT4);
                Write_spanset(result, i, s);
            }
            break;
        }
        case T_INT8: {
            auto in = FlatVector::GetData<int64_t>(source);
            for (idx_t i = 0; i < count; ++i) {
                if (FlatVector::IsNull(source, i)) { handle_null(i); continue; }
                SpanSet *s = value_spanset(Datum(in[i]), T_INT8);
                Write_spanset(result, i, s);
            }
            break;
        }
        case T_FLOAT8: {
            auto in = FlatVector::GetData<double>(source);
            for (idx_t i = 0; i < count; ++i) {
                if (FlatVector::IsNull(source, i)) { handle_null(i); continue; }
                SpanSet *s = value_spanset(Float8GetDatum(in[i]), T_FLOAT8);
                Write_spanset(result, i, s);
            }
            break;
        }
        case T_DATE: {
            auto in = FlatVector::GetData<date_t>(source);
            for (idx_t i = 0; i < count; ++i) {
                if (FlatVector::IsNull(source, i)) { handle_null(i); continue; }
                int32_t days = (int32_t)ToMeosDate(in[i]);
                SpanSet *s = value_spanset(Datum(days), T_DATE);
                Write_spanset(result, i, s);
            }
            break;
        }
        case T_TIMESTAMPTZ: {
            auto in = FlatVector::GetData<timestamp_t>(source);
            for (idx_t i = 0; i < count; ++i) {
                if (FlatVector::IsNull(source, i)) { handle_null(i); continue; }
                auto meos_ts = ToMeosTimestamp(in[i]);
                SpanSet *s = value_spanset(Datum(meos_ts), T_TIMESTAMPTZ);
                Write_spanset(result, i, s);
            }
            break;
        }
        default:
            throw NotImplementedException("SpansetConversion: unsupported base type for conversion to spanset");
    }
}

// --- CAST (conversion: base -> spanset) ----
bool SpansetFunctions::Value_to_spanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    auto target_type   = result.GetType();
    MeosType spanset_t = SpansetTypeMapping::GetMeosTypeFromAlias(target_type.GetAlias());
    MeosType span_t    = spansettype_spantype(spanset_t);
    MeosType base_t    = spantype_basetype(span_t);

    Value_to_spanset_core(source, result, count, base_t);
    return true;
}

// --- Conversion: set -> spanset ---
static void Set_to_spanset_common(Vector &source, Vector &result, idx_t count) {
    source.Flatten(count);
    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto handle_null = [&](idx_t row) { FlatVector::SetNull(result, row, true); };
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            const uint8_t *bytes = (const uint8_t *)blob.GetData();
            size_t size = blob.GetSize();            
            Set *s = (Set *)malloc(size);
            memcpy(s, bytes, size);            

            SpanSet *spanset = set_spanset(s);
            free(s);

            size_t result_size = spanset_mem_size(spanset);
            string_t result_blob = StringVector::AddStringOrBlob(result, (const char *)spanset, result_size);
            free(spanset);

            return result_blob;
        }
    );
}

// --- CAST (conversion: set -> spanset) ----
bool SpansetFunctions::Set_to_spanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {    
    Set_to_spanset_common(source, result, count);
    return true;
}


// --- Conversion: span -> spanset ---
static inline void Span_to_spanset_common (Vector &source, Vector &result, idx_t count){
    source.Flatten(count);
    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto handle_null = [&](idx_t row) { FlatVector::SetNull(result, row, true); };
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            const uint8_t *bytes = (const uint8_t *)blob.GetData();
            size_t size = blob.GetSize();            
            Span *s = (Span *)malloc(size);
            memcpy(s, bytes, size);            

            SpanSet *spanset = span_to_spanset(s);
            free(s);

            size_t result_size = spanset_mem_size(spanset);
            string_t result_blob = StringVector::AddStringOrBlob(result, (const char *)spanset, result_size);
            free(spanset);

            return result_blob;
        }
    );
}

// --- CAST (conversion: span -> spanset) ----
bool SpansetFunctions::Span_to_spanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {    
    Span_to_spanset_common(source, result, count);
    return true;
}

// --- Conversion: spanset -> span ---

static inline void Spanset_to_span_common(Vector &source, Vector &result, idx_t count) {
    source.Flatten(count);
    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto handle_null = [&](idx_t row) { FlatVector::SetNull(result, row, true); };
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            const uint8_t *bytes = (const uint8_t *)blob.GetData();
            size_t size = blob.GetSize();            
            SpanSet *spanset = (SpanSet *)malloc(size);
            memcpy(spanset, bytes, size);            

            Span *s = spanset_span(spanset);
            free(spanset);

            size_t result_size = sizeof(Span);
            string_t result_blob = StringVector::AddStringOrBlob(result, (const char *)s, result_size);
            free(s);

            return result_blob;
        }
    );
}

// --- CAST (conversion: spanset -> span) ----
bool SpansetFunctions::Spanset_to_span_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Spanset_to_span_common(source, result, count);
    return true;
}


// --- Conversion: intspanset <-> floatspanset ---
static inline void Intspanset_to_floatspanset_common(Vector &source, Vector &result, idx_t count) {
    source.Flatten(count);
    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto handle_null = [&](idx_t row) { FlatVector::SetNull(result, row, true); };
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            const uint8_t *bytes = (const uint8_t *)blob.GetData();
            size_t size = blob.GetSize();            
            SpanSet *intspanset = (SpanSet *)malloc(size);
            memcpy(intspanset, bytes, size);            

            SpanSet *floatspanset = intspanset_to_floatspanset(intspanset);
            free(intspanset);

            size_t result_size = spanset_mem_size(floatspanset);
            string_t result_blob = StringVector::AddStringOrBlob(result, (const char *)floatspanset, result_size);
            free(floatspanset);

            return result_blob;
        }
    );
}

static inline void Floatspanset_to_intspanset_common(Vector &source, Vector &result, idx_t count) {
    source.Flatten(count);
    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto handle_null = [&](idx_t row) { FlatVector::SetNull(result, row, true); };
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            const uint8_t *bytes = (const uint8_t *)blob.GetData();
            size_t size = blob.GetSize();            
            SpanSet *floatspanset = (SpanSet *)malloc(size);
            memcpy(floatspanset, bytes, size);            

            SpanSet *intspanset = floatspanset_to_intspanset(floatspanset);
            free(floatspanset);

            size_t result_size = spanset_mem_size(intspanset);
            string_t result_blob = StringVector::AddStringOrBlob(result, (const char *)intspanset, result_size);
            free(intspanset);

            return result_blob;
        }
    );
}

// --- CAST (conversion: intspanset -> floatspanset) ----
bool SpansetFunctions::Intspanset_to_floatspanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Intspanset_to_floatspanset_common(source, result, count);
    return true;
}

// --- CAST (conversion: floatspanset -> intspanset) ----
bool SpansetFunctions::Floatspanset_to_intspanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Floatspanset_to_intspanset_common(source, result, count);
    return true;
}

// --- Conversion: datespanset <-> tstzspanset ----
static inline void Datespanset_to_tstzspanset_common(Vector &source, Vector &result, idx_t count) {
    source.Flatten(count);
    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto handle_null = [&](idx_t row) { FlatVector::SetNull(result, row, true); };
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            const uint8_t *bytes = (const uint8_t *)blob.GetData();
            size_t size = blob.GetSize();            
            SpanSet *datespanset = (SpanSet *)malloc(size);
            memcpy(datespanset, bytes, size);            

            SpanSet *tstzspanset = datespanset_to_tstzspanset(datespanset);
            free(datespanset);

            size_t result_size = spanset_mem_size(tstzspanset);
            string_t result_blob = StringVector::AddStringOrBlob(result, (const char *)tstzspanset, result_size);
            free(tstzspanset);

            return result_blob;
        }
    );
}

static inline void Tstzspanset_to_datespanset_common(Vector &source, Vector &result, idx_t count) {
    source.Flatten(count);
    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto handle_null = [&](idx_t row) { FlatVector::SetNull(result, row, true); };
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            const uint8_t *bytes = (const uint8_t *)blob.GetData();
            size_t size = blob.GetSize();            
            SpanSet *tstzspanset = (SpanSet *)malloc(size);
            memcpy(tstzspanset, bytes, size);            

            SpanSet *datespanset = tstzspanset_to_datespanset(tstzspanset);
            free(tstzspanset);

            size_t result_size = spanset_mem_size(datespanset);
            string_t result_blob = StringVector::AddStringOrBlob(result, (const char *)datespanset, result_size);
            free(datespanset);

            return result_blob;
        }
    );
}

// --- CAST (conversion: datespanset -> tstzspanset) ----
bool SpansetFunctions::Datespanset_to_tstzspanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Datespanset_to_tstzspanset_common(source, result, count);
    return true;
}
// --- CAST (conversion: tstzspanset -> datespanset) ----
bool SpansetFunctions::Tstzspanset_to_datespanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Tstzspanset_to_datespanset_common(source, result, count);
    return true;
}

// floor/ceil/round/degrees/radians on floatspanset are generated from the catalog
// (generated_temporal_udfs.cpp); no hand bodies remain.

// spans(<spanset_type>) is generated from the catalog (spanset_spans) in generated_temporal_udfs.cpp.

} // namespace duckdb   
