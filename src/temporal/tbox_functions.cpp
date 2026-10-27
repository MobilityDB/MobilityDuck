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

#include "temporal/tbox_functions.hpp"
#include "time_util.hpp"

// #include "duckdb/common/types/blob.hpp"
#include "duckdb/common/exception.hpp"
// #include "duckdb/common/string_util.hpp"
// #include "duckdb/function/scalar_function.hpp"
// #include "duckdb/main/extension/extension_loader.hpp"
// #include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

namespace duckdb {

namespace {

inline string_t MallocBlobToResult(Vector &result, void *buf, size_t sz) {
	string_t blob(reinterpret_cast<const char *>(buf), UnsafeNumericCast<uint32_t>(sz));
	string_t stored = StringVector::AddStringOrBlob(result, blob);
	free(buf);
	return stored;
}

} // namespace

bool TboxFunctions::Tbox_in(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    bool success = true;
    try {
        UnaryExecutor::ExecuteWithNulls<string_t, string_t>(
            source, result, count,
            [&](string_t input, ValidityMask &mask, idx_t idx) {
                if (input.GetSize() == 0) {
                    return string_t();
                }
                std::string input_str(input.GetDataUnsafe(), input.GetSize());
                TBox *tbox = tbox_in(input_str.c_str());
                if (!tbox) {
                    throw InternalException("Failure in Tbox_in: unable to cast string to tbox");
                    success = false;
                    return string_t();
                }
                size_t tbox_size = sizeof(TBox);
                char *tbox_data = (char*)malloc(tbox_size);
                memcpy(tbox_data, tbox, tbox_size);
                free(tbox);
                return MallocBlobToResult(result, tbox_data, tbox_size);
            }
        );
    } catch (const std::exception &e) {
        throw InternalException(e.what());
        success = false;
    }
    return success;
}

bool TboxFunctions::Tbox_out(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    bool success = true;
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input) {
            TBox *tbox = nullptr;
            if (input.GetSize() > 0) {
                tbox = (TBox*)malloc(input.GetSize());
                memcpy(tbox, input.GetDataUnsafe(), input.GetSize());
            }
            if (!tbox) {
                throw InternalException("Failure in Tbox_out: unable to cast binary to tbox");
                success = false;
                return string_t();
            }
            char *str = tbox_out(tbox, OUT_DEFAULT_DECIMAL_DIGITS);
            if (!str) {
                free(tbox);
                throw InternalException("Failure in Tbox_out: tbox_out returned null");
            }
            std::string out(str);
            free(str);
            free(tbox);
            return StringVector::AddString(result, out);
        }
    );
    return success;
}

template <typename TA>
void TboxFunctions::NumberToTboxExecutor(Vector &value, MeosType basetype, Vector &result, idx_t count) {
    UnaryExecutor::Execute<TA, string_t>(
        value, result, count,
        [&](TA value) {
            Datum datum;
            if (basetype == T_INT4) {
                datum = Int32GetDatum(value);
            } else if (basetype == T_FLOAT8) {
                datum = Float8GetDatum(value);
            } else {
                throw InternalException("Unsupported basetype in NumberToTboxExecutor");
            }
            TBox *tbox = number_tbox(datum, basetype);
            size_t tbox_size = sizeof(TBox);
            char *tbox_data = (char*)malloc(tbox_size);
            memcpy(tbox_data, tbox, tbox_size);
            free(tbox);
            return MallocBlobToResult(result, tbox_data, tbox_size);
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

bool TboxFunctions::Number_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    const auto &source_type = source.GetType();
    
    if (source_type.id() == LogicalTypeId::INTEGER) {
        NumberToTboxExecutor<int32_t>(source, T_INT4, result, count);
    } else if (source_type.id() == LogicalTypeId::DOUBLE) {
        NumberToTboxExecutor<double>(source, T_FLOAT8, result, count);
    } else {
        throw InternalException("Number_to_tbox_cast: source must be integer, float, or decimal");
    }
    return true;
}

void TboxFunctions::TimestamptzToTboxExecutor(Vector &value, Vector &result, idx_t count) {
    UnaryExecutor::Execute<timestamp_tz_t, string_t>(
        value, result, count,
        [&](timestamp_tz_t duckdb_ts) {
            timestamp_tz_t meos_ts = DuckDBToMeosTimestamp(duckdb_ts);
            TBox *tbox = timestamptz_to_tbox((TimestampTz)meos_ts.value);
            size_t tbox_size = sizeof(TBox);
            char *tbox_data = (char*)malloc(tbox_size);
            memcpy(tbox_data, tbox, tbox_size);
            free(tbox);
            return MallocBlobToResult(result, tbox_data, tbox_size);
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

bool TboxFunctions::Timestamptz_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    TboxFunctions::TimestamptzToTboxExecutor(source, result, count);
    return true;
}

void TboxFunctions::SetToTboxExecutor(Vector &value, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        value, result, count,
        [&](string_t set_str) {
            Set *set = nullptr;
            if (set_str.GetSize() > 0) {
                set = (Set*)malloc(set_str.GetSize());
                memcpy(set, set_str.GetDataUnsafe(), set_str.GetSize());
            }
            if (!set) {
                throw InternalException("Failure in Set_to_tbox: unable to cast binary to set");
            }
            TBox *tbox = set_to_tbox(set);
            size_t tbox_size = sizeof(TBox);
            char *tbox_data = (char*)malloc(tbox_size);
            memcpy(tbox_data, tbox, tbox_size);
            free(set);
            free(tbox);
            return MallocBlobToResult(result, tbox_data, tbox_size);
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

bool TboxFunctions::Set_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    TboxFunctions::SetToTboxExecutor(source, result, count);
    return true;
}

void TboxFunctions::SpanToTboxExecutor(Vector &value, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        value, result, count,
        [&](string_t span_str) {
            Span *span = nullptr;
            if (span_str.GetSize() > 0) {
                span = (Span*)malloc(span_str.GetSize());
                memcpy(span, span_str.GetDataUnsafe(), span_str.GetSize());
            }
            if (!span) {
                throw InternalException("Failure in Span_to_tbox: unable to cast binary to span");
            }
            TBox *tbox = span_to_tbox(span);
            size_t tbox_size = sizeof(TBox);
            char *tbox_data = (char*)malloc(tbox_size);
            memcpy(tbox_data, tbox, tbox_size);
            free(span);
            free(tbox);
            return MallocBlobToResult(result, tbox_data, tbox_size);
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

bool TboxFunctions::Span_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    TboxFunctions::SpanToTboxExecutor(source, result, count);
    return true;
}

void TboxFunctions::TboxToIntspanExecutor(Vector &value, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        value, result, count,
        [&](string_t tbox_str) {
            TBox *tbox = nullptr;
            if (tbox_str.GetSize() > 0) {
                tbox = (TBox*)malloc(tbox_str.GetSize());
                memcpy(tbox, tbox_str.GetDataUnsafe(), tbox_str.GetSize());
            }
            if (!tbox) {
                throw InternalException("Failure in TboxToIntspanExecutor: unable to cast binary to tbox");
            }
            Span *span = tbox_to_intspan(tbox);
            size_t span_size = sizeof(Span);
            char *span_data = (char*)malloc(span_size);
            memcpy(span_data, span, span_size);
            free(tbox);
            free(span);
            return MallocBlobToResult(result, span_data, span_size);
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

bool TboxFunctions::Tbox_to_intspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    TboxFunctions::TboxToIntspanExecutor(source, result, count);
    return true;
}

void TboxFunctions::TboxToFloatspanExecutor(Vector &value, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        value, result, count,
        [&](string_t tbox_str) {
            TBox *tbox = nullptr;
            if (tbox_str.GetSize() > 0) {
                tbox = (TBox*)malloc(tbox_str.GetSize());
                memcpy(tbox, tbox_str.GetDataUnsafe(), tbox_str.GetSize());
            }
            if (!tbox) {
                throw InternalException("Failure in TboxToFloatspanExecutor: unable to cast binary to tbox");
            }
            Span *span = tbox_to_floatspan(tbox);
            size_t span_size = sizeof(Span);
            char *span_data = (char*)malloc(span_size);
            memcpy(span_data, span, span_size);
            free(tbox);
            free(span);
            return MallocBlobToResult(result, span_data, span_size);
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

bool TboxFunctions::Tbox_to_floatspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    TboxFunctions::TboxToFloatspanExecutor(source, result, count);
    return true;
}

void TboxFunctions::TboxToTstzspanExecutor(Vector &value, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        value, result, count,
        [&](string_t tbox_str) {
            TBox *tbox = nullptr;
            if (tbox_str.GetSize() > 0) {
                tbox = (TBox*)malloc(tbox_str.GetSize());
                memcpy(tbox, tbox_str.GetDataUnsafe(), tbox_str.GetSize());
            }
            if (!tbox) {
                throw InternalException("Failure in TboxToTstzspanExecutor: unable to cast binary to tbox");
            }
            Span *span = tbox_to_tstzspan(tbox);
            size_t span_size = sizeof(Span);
            char *span_data = (char*)malloc(span_size);
            memcpy(span_data, span, span_size);
            free(tbox);
            free(span);
            return MallocBlobToResult(result, span_data, span_size);
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

bool TboxFunctions::Tbox_to_tstzspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    TboxFunctions::TboxToTstzspanExecutor(source, result, count);
    return true;
}

void TboxFunctions::SpansetToTboxExecutor(Vector &value, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        value, result, count,
        [&](string_t spanset_str) {
            SpanSet *spanset = nullptr;
            if (spanset_str.GetSize() > 0) {
                spanset = (SpanSet*)malloc(spanset_str.GetSize());
                memcpy(spanset, spanset_str.GetDataUnsafe(), spanset_str.GetSize());
            }
            if (!spanset) {
                throw InternalException("Failure in SpansetToTboxExecutor: unable to cast binary to spanset");
            }
            TBox *tbox = spanset_to_tbox(spanset);
            size_t tbox_size = sizeof(TBox);
            char *tbox_data = (char*)malloc(tbox_size);
            memcpy(tbox_data, tbox, tbox_size);
            free(spanset);
            free(tbox);
            return MallocBlobToResult(result, tbox_data, tbox_size);
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

bool TboxFunctions::Spanset_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    TboxFunctions::SpansetToTboxExecutor(source, result, count);
    return true;
}

/* ***************************************************
 * WKB / hex-WKB serialization + hashing
 ****************************************************/

// hash(tbox) / hashExtended(tbox, UBIGINT) are generated from the catalog
// (bare canonical names) in generated_temporal_udfs.cpp.

} // namespace duckdb
