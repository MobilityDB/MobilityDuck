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
void TboxFunctions::NumberTimestamptzToTboxExecutor(Vector &value, Vector &t, MeosType basetype, Vector &result, idx_t count) {
    BinaryExecutor::Execute<TA, timestamp_tz_t, string_t>(
        value, t, result, count,
        [&](TA value, timestamp_tz_t t) {
            timestamp_tz_t meos_ts = DuckDBToMeosTimestamp(t);
            Datum datum;
            if (basetype == T_INT4) {
                datum = Int32GetDatum(value);
            } else if (basetype == T_FLOAT8) {
                datum = Float8GetDatum(value);
            } else {
                throw InternalException("Unsupported basetype in NumberTimestamptzToTboxExecutor");
            }
            TBox *tbox = number_timestamptz_to_tbox(datum, basetype, (TimestampTz)meos_ts.value);
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

void TboxFunctions::Number_timestamptz_to_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    const auto &arg_type = args.data[0].GetType();
    
    if (arg_type.id() == LogicalTypeId::INTEGER) {
        NumberTimestamptzToTboxExecutor<int64_t>(args.data[0], args.data[1], T_INT4, result, args.size());
    } else if (arg_type.id() == LogicalTypeId::DOUBLE) {
        NumberTimestamptzToTboxExecutor<double>(args.data[0], args.data[1], T_FLOAT8, result, args.size());
    } else {
        throw InternalException("Number_timestamptz_to_tbox: args[0] must be integer or float");
    }
}

void TboxFunctions::Numspan_timestamptz_to_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, timestamp_tz_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t span_str, timestamp_tz_t t) {
            Span *span = nullptr;
            if (span_str.GetSize() > 0) {
                span = (Span*)malloc(span_str.GetSize());
                memcpy(span, span_str.GetDataUnsafe(), span_str.GetSize());
            }
            if (!span) {
                throw InternalException("Failure in Numspan_timestamptz_to_tbox: unable to cast binary to span");
            }
            timestamp_tz_t meos_ts = DuckDBToMeosTimestamp(t);
            TBox *tbox = numspan_timestamptz_to_tbox(span, (TimestampTz)meos_ts.value);
            size_t tbox_size = sizeof(TBox);
            char *tbox_data = (char*)malloc(tbox_size);
            memcpy(tbox_data, tbox, tbox_size);
            free(span);
            free(tbox);
            return MallocBlobToResult(result, tbox_data, tbox_size);
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

template <typename TA>
void TboxFunctions::NumberTstzspanToTboxExecutor(Vector &value, Vector &span_str, MeosType basetype, Vector &result, idx_t count) {
    BinaryExecutor::Execute<TA, string_t, string_t>(
        value, span_str, result, count,
        [&](TA value, string_t span_str) {
            Datum datum;
            if (basetype == T_INT4) {
                datum = Int32GetDatum(value);
            } else if (basetype == T_FLOAT8) {
                datum = Float8GetDatum(value);
            } else {
                throw InternalException("Unsupported basetype in NumberTstzspanToTboxExecutor");
            }
            Span *span = nullptr;
            if (span_str.GetSize() > 0) {
                span = (Span*)malloc(span_str.GetSize());
                memcpy(span, span_str.GetDataUnsafe(), span_str.GetSize());
            }
            if (!span) {
                throw InternalException("Failure in NumberTstzspanToTboxExecutor: unable to cast binary to span");
            }
            TBox *tbox = number_tstzspan_to_tbox(datum, basetype, span);
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

void TboxFunctions::Number_tstzspan_to_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    const auto &arg_type = args.data[0].GetType();
    
    if (arg_type.id() == LogicalTypeId::INTEGER) {
        NumberTstzspanToTboxExecutor<int64_t>(args.data[0], args.data[1], T_INT4, result, args.size());
    } else if (arg_type.id() == LogicalTypeId::DOUBLE) {
        NumberTstzspanToTboxExecutor<double>(args.data[0], args.data[1], T_FLOAT8, result, args.size());
    } else {
        throw InternalException("Number_tstzspan_to_tbox: args[0] must be integer or float");
    }
}

void TboxFunctions::Numspan_tstzspan_to_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t numspan_str, string_t tstzspan_str) {
            Span *numspan = nullptr;
            if (numspan_str.GetSize() > 0) {
                numspan = (Span*)malloc(numspan_str.GetSize());
                memcpy(numspan, numspan_str.GetDataUnsafe(), numspan_str.GetSize());
            }
            if (!numspan) {
                throw InternalException("Failure in Numspan_tstzspan_to_tbox: unable to cast binary to span");
            }

            Span *tstzspan = nullptr;
            if (tstzspan_str.GetSize() > 0) {
                tstzspan = (Span*)malloc(tstzspan_str.GetSize());
                memcpy(tstzspan, tstzspan_str.GetDataUnsafe(), tstzspan_str.GetSize());
            }
            if (!tstzspan) {
                throw InternalException("Failure in Numspan_tstzspan_to_tbox: unable to cast binary to span");
            }

            TBox *tbox = numspan_tstzspan_to_tbox(numspan, tstzspan);
            size_t tbox_size = sizeof(TBox);
            char *tbox_data = (char*)malloc(tbox_size);
            memcpy(tbox_data, tbox, tbox_size);
            free(numspan);
            free(tstzspan);
            free(tbox);
            return MallocBlobToResult(result, tbox_data, tbox_size);
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
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

void TboxFunctions::Number_to_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    const auto &arg_type = args.data[0].GetType();
    
    if (arg_type.id() == LogicalTypeId::INTEGER) {
        NumberToTboxExecutor<int64_t>(args.data[0], T_INT4, result, args.size());
    } else if (arg_type.id() == LogicalTypeId::DOUBLE) {
        NumberToTboxExecutor<double>(args.data[0], T_FLOAT8, result, args.size());
    } else {
        throw InternalException("Number_to_tbox: args[0] must be integer or float");
    }
}

bool TboxFunctions::Number_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    const auto &source_type = source.GetType();
    
    if (source_type.id() == LogicalTypeId::INTEGER) {
        NumberToTboxExecutor<int64_t>(source, T_INT4, result, count);
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

void TboxFunctions::Timestamptz_to_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    TimestamptzToTboxExecutor(args.data[0], result, args.size());
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

void TboxFunctions::Set_to_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    SetToTboxExecutor(args.data[0], result, args.size());
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

void TboxFunctions::Span_to_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    SpanToTboxExecutor(args.data[0], result, args.size());
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

void TboxFunctions::Tbox_to_intspan(DataChunk &args, ExpressionState &state, Vector &result) {
    TboxToIntspanExecutor(args.data[0], result, args.size());
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

void TboxFunctions::Tbox_to_floatspan(DataChunk &args, ExpressionState &state, Vector &result) {
    TboxToFloatspanExecutor(args.data[0], result, args.size());
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

void TboxFunctions::Tbox_to_tstzspan(DataChunk &args, ExpressionState &state, Vector &result) {
    TboxToTstzspanExecutor(args.data[0], result, args.size());
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

void TboxFunctions::Spanset_to_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    SpansetToTboxExecutor(args.data[0], result, args.size());
}

bool TboxFunctions::Spanset_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    TboxFunctions::SpansetToTboxExecutor(source, result, count);
    return true;
}

void TboxFunctions::Tbox_hasx(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, bool>(
        args.data[0], result, args.size(),
        [&](string_t tbox_str) {
            TBox *tbox = nullptr;
            if (tbox_str.GetSize() > 0) {
                tbox = (TBox*)malloc(tbox_str.GetSize());
                memcpy(tbox, tbox_str.GetDataUnsafe(), tbox_str.GetSize());
            }
            if (!tbox) {
                throw InternalException("Failure in Tbox_hasx: unable to cast binary to tbox");
            }
            bool ret = tbox_hasx(tbox);
            free(tbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_hast(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, bool>(
        args.data[0], result, args.size(),
        [&](string_t tbox_str) {
            TBox *tbox = nullptr;
            if (tbox_str.GetSize() > 0) {
                tbox = (TBox*)malloc(tbox_str.GetSize());
                memcpy(tbox, tbox_str.GetDataUnsafe(), tbox_str.GetSize());
            }
            if (!tbox) {
                throw InternalException("Failure in Tbox_hast: unable to cast binary to tbox");
            }
            bool ret = tbox_hast(tbox);
            free(tbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_xmin(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, double>(
        args.data[0], result, args.size(),
        [&](string_t tbox_str, ValidityMask &mask, idx_t idx) {
            TBox *tbox = nullptr;
            if (tbox_str.GetSize() > 0) {
                tbox = (TBox*)malloc(tbox_str.GetSize());
                memcpy(tbox, tbox_str.GetDataUnsafe(), tbox_str.GetSize());
            }
            if (!tbox) {
                throw InternalException("Failure in Tbox_xmin: unable to cast binary to tbox");
            }
            double ret;
            if (!tbox_xmin(tbox, &ret)) {
                free(tbox);
                mask.SetInvalid(idx);
                return double();
            }
            free(tbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_xmin_inc(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, bool>(
        args.data[0], result, args.size(),
        [&](string_t tbox_str, ValidityMask &mask, idx_t idx) {
            TBox *tbox = nullptr;
            if (tbox_str.GetSize() > 0) {
                tbox = (TBox*)malloc(tbox_str.GetSize());
                memcpy(tbox, tbox_str.GetDataUnsafe(), tbox_str.GetSize());
            }
            if (!tbox) {
                throw InternalException("Failure in Tbox_xmin_inc: unable to cast binary to tbox");
            }
            bool ret;
            if (!tbox_xmin_inc(tbox, &ret)) {
                free(tbox);
                mask.SetInvalid(idx);
                return bool();
            }
            free(tbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_xmax(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, double>(
        args.data[0], result, args.size(),
        [&](string_t tbox_str, ValidityMask &mask, idx_t idx) {
            TBox *tbox = nullptr;
            if (tbox_str.GetSize() > 0) {
                tbox = (TBox*)malloc(tbox_str.GetSize());
                memcpy(tbox, tbox_str.GetDataUnsafe(), tbox_str.GetSize());
            }
            if (!tbox) {
                throw InternalException("Failure in Tbox_xmax: unable to cast binary to tbox");
            }
            double ret;
            if (!tbox_xmax(tbox, &ret)) {
                free(tbox);
                mask.SetInvalid(idx);
                return double();
            }
            free(tbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_xmax_inc(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, bool>(
        args.data[0], result, args.size(),
        [&](string_t tbox_str, ValidityMask &mask, idx_t idx) {
            TBox *tbox = nullptr;
            if (tbox_str.GetSize() > 0) {
                tbox = (TBox*)malloc(tbox_str.GetSize());
                memcpy(tbox, tbox_str.GetDataUnsafe(), tbox_str.GetSize());
            }
            if (!tbox) {
                throw InternalException("Failure in Tbox_xmax_inc: unable to cast binary to tbox");
            }
            bool ret;
            if (!tbox_xmax_inc(tbox, &ret)) {
                free(tbox);
                mask.SetInvalid(idx);
                return bool();
            }
            free(tbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_tmin(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t>(
        args.data[0], result, args.size(),
        [&](string_t tbox_str, ValidityMask &mask, idx_t idx) {
            TBox *tbox = nullptr;
            if (tbox_str.GetSize() > 0) {
                tbox = (TBox*)malloc(tbox_str.GetSize());
                memcpy(tbox, tbox_str.GetDataUnsafe(), tbox_str.GetSize());
            }
            if (!tbox) {
                throw InternalException("Failure in Tbox_tmin: unable to cast binary to tbox");
            }
            TimestampTz ret_meos;
            if (!tbox_tmin(tbox, &ret_meos)) {
                free(tbox);
                mask.SetInvalid(idx);
                return timestamp_tz_t();
            }
            timestamp_tz_t ret = MeosToDuckDBTimestamp((timestamp_tz_t)ret_meos);
            free(tbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_tmin_inc(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, bool>(
        args.data[0], result, args.size(),
        [&](string_t tbox_str, ValidityMask &mask, idx_t idx) {
            TBox *tbox = nullptr;
            if (tbox_str.GetSize() > 0) {
                tbox = (TBox*)malloc(tbox_str.GetSize());
                memcpy(tbox, tbox_str.GetDataUnsafe(), tbox_str.GetSize());
            }
            if (!tbox) {
                throw InternalException("Failure in Tbox_tmin_inc: unable to cast binary to tbox");
            }
            bool ret;
            if (!tbox_tmin_inc(tbox, &ret)) {
                free(tbox);
                mask.SetInvalid(idx);
                return bool();
            }
            free(tbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_tmax(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t>(
        args.data[0], result, args.size(),
        [&](string_t tbox_str, ValidityMask &mask, idx_t idx) {
            TBox *tbox = nullptr;
            if (tbox_str.GetSize() > 0) {
                tbox = (TBox*)malloc(tbox_str.GetSize());
                memcpy(tbox, tbox_str.GetDataUnsafe(), tbox_str.GetSize());
            }
            if (!tbox) {
                throw InternalException("Failure in Tbox_tmax: unable to cast binary to tbox");
            }
            TimestampTz ret_meos;
            if (!tbox_tmax(tbox, &ret_meos)) {
                free(tbox);
                mask.SetInvalid(idx);
                return timestamp_tz_t();
            }
            timestamp_tz_t ret = MeosToDuckDBTimestamp((timestamp_tz_t)ret_meos);
            free(tbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_tmax_inc(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, bool>(
        args.data[0], result, args.size(),
        [&](string_t tbox_str, ValidityMask &mask, idx_t idx) {
            TBox *tbox = nullptr;
            if (tbox_str.GetSize() > 0) {
                tbox = (TBox*)malloc(tbox_str.GetSize());
                memcpy(tbox, tbox_str.GetDataUnsafe(), tbox_str.GetSize());
            }
            if (!tbox) {
                throw InternalException("Failure in Tbox_tmax_inc: unable to cast binary to tbox");
            }
            bool ret;
            if (!tbox_tmax_inc(tbox, &ret)) {
                free(tbox);
                mask.SetInvalid(idx);
                return bool();
            }
            free(tbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

template <typename TB>
void TboxFunctions::TboxShiftValueExecutor(Vector &tbox, Vector &shift, LogicalType type, Vector &result, idx_t count) {
    BinaryExecutor::Execute<string_t, TB, string_t>(
        tbox, shift, result, count,
        [&](string_t tbox_str, TB shift) {
            TBox *tbox = nullptr;
            if (tbox_str.GetSize() > 0) {
                tbox = (TBox*)malloc(tbox_str.GetSize());
                memcpy(tbox, tbox_str.GetDataUnsafe(), tbox_str.GetSize());
            }
            if (!tbox) {
                throw InternalException("Failure in Tbox_shift_value: unable to cast binary to tbox");
            }
            Datum datum;
            if (type == LogicalType::INTEGER) {
                datum = Int32GetDatum(shift);
            } else if (type == LogicalType::DOUBLE) {
                datum = Float8GetDatum(shift);
            } else {
                throw InternalException("Tbox_shift_value: type must be integer or double");
            }
            TBox *shifted_tbox = tbox_shift_scale_value(tbox, datum, 0, true, false);
            size_t shifted_tbox_size = sizeof(TBox);
            char *shifted_tbox_data = (char*)malloc(shifted_tbox_size);
            memcpy(shifted_tbox_data, shifted_tbox, shifted_tbox_size);
            free(tbox);
            free(shifted_tbox);
            return MallocBlobToResult(result, shifted_tbox_data, shifted_tbox_size);
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_shift_value(DataChunk &args, ExpressionState &state, Vector &result) {
    const auto &arg_type = args.data[1].GetType();
    if (arg_type.id() == LogicalTypeId::INTEGER) {
        TboxShiftValueExecutor<int64_t>(args.data[0], args.data[1], arg_type, result, args.size());
    } else if (arg_type.id() == LogicalTypeId::DOUBLE) {
        TboxShiftValueExecutor<double>(args.data[0], args.data[1], arg_type, result, args.size());
    } else {
        throw InternalException("Tbox_shift_value: type must be integer or double");
    }
}

void TboxFunctions::Tbox_shift_time(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, interval_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox_str, interval_t interval_duckdb) {
            TBox *tbox = nullptr;
            if (tbox_str.GetSize() > 0) {
                tbox = (TBox*)malloc(tbox_str.GetSize());
                memcpy(tbox, tbox_str.GetDataUnsafe(), tbox_str.GetSize());
            }
            if (!tbox) {
                throw InternalException("Failure in Tbox_shift_time: unable to cast binary to tbox");
            }
            MeosInterval shift = IntervaltToInterval(interval_duckdb);
            TBox *shifted_tbox = tbox_shift_scale_time(tbox, &shift, NULL);
            size_t shifted_tbox_size = sizeof(TBox);
            char *shifted_tbox_data = (char*)malloc(shifted_tbox_size);
            memcpy(shifted_tbox_data, shifted_tbox, shifted_tbox_size);
            free(tbox);
            free(shifted_tbox);
            return MallocBlobToResult(result, shifted_tbox_data, shifted_tbox_size);
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

template <typename TB>
void TboxFunctions::TboxScaleValueExecutor(Vector &tbox, Vector &width, LogicalType type, Vector &result, idx_t count) {
    BinaryExecutor::Execute<string_t, TB, string_t>(
        tbox, width, result, count,
        [&](string_t tbox_str, TB width) {
            TBox *tbox = nullptr;
            if (tbox_str.GetSize() > 0) {
                tbox = (TBox*)malloc(tbox_str.GetSize());
                memcpy(tbox, tbox_str.GetDataUnsafe(), tbox_str.GetSize());
            }
            if (!tbox) {
                throw InternalException("Failure in Tbox_scale_value: unable to cast binary to tbox");
            }
            Datum datum;
            if (type == LogicalType::INTEGER) {
                datum = Int32GetDatum(width);
            } else if (type == LogicalType::DOUBLE) {
                datum = Float8GetDatum(width);
            } else {
                throw InternalException("Tbox_scale_value: type must be integer or double");
            }
            TBox *scaled_tbox = tbox_shift_scale_value(tbox, 0, datum, false, true);
            size_t scaled_tbox_size = sizeof(TBox);
            char *scaled_tbox_data = (char*)malloc(scaled_tbox_size);
            memcpy(scaled_tbox_data, scaled_tbox, scaled_tbox_size);
            free(tbox);
            free(scaled_tbox);
            return MallocBlobToResult(result, scaled_tbox_data, scaled_tbox_size);
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_scale_value(DataChunk &args, ExpressionState &state, Vector &result) {
    const auto &arg_type = args.data[1].GetType();
    if (arg_type.id() == LogicalTypeId::INTEGER) {
        TboxScaleValueExecutor<int64_t>(args.data[0], args.data[1], arg_type, result, args.size());
    } else if (arg_type.id() == LogicalTypeId::DOUBLE) {
        TboxScaleValueExecutor<double>(args.data[0], args.data[1], arg_type, result, args.size());
    } else {
        throw InternalException("Tbox_scale_value: type must be integer or double");
    }
}

void TboxFunctions::Tbox_scale_time(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, interval_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox_str, interval_t interval_duckdb) {
            TBox *tbox = nullptr;
            if (tbox_str.GetSize() > 0) {
                tbox = (TBox*)malloc(tbox_str.GetSize());
                memcpy(tbox, tbox_str.GetDataUnsafe(), tbox_str.GetSize());
            }
            if (!tbox) {
                throw InternalException("Failure in Tbox_scale_time: unable to cast binary to tbox");
            }
            MeosInterval duration = IntervaltToInterval(interval_duckdb);
            TBox *scaled_tbox = tbox_shift_scale_time(tbox, NULL, &duration);
            size_t scaled_tbox_size = sizeof(TBox);
            char *scaled_tbox_data = (char*)malloc(scaled_tbox_size);
            memcpy(scaled_tbox_data, scaled_tbox, scaled_tbox_size);
            free(tbox);
            free(scaled_tbox);
            return MallocBlobToResult(result, scaled_tbox_data, scaled_tbox_size);
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

template <typename TB>
void TboxFunctions::TboxShiftScaleValueExecutor(Vector &tbox, Vector &shift, Vector &width, LogicalType type, Vector &result, idx_t count) {
    TernaryExecutor::Execute<string_t, TB, TB, string_t>(
        tbox, shift, width, result, count,
        [&](string_t tbox_str, TB shift, TB width) {
            TBox *tbox = nullptr;
            if (tbox_str.GetSize() > 0) {
                tbox = (TBox*)malloc(tbox_str.GetSize());
                memcpy(tbox, tbox_str.GetDataUnsafe(), tbox_str.GetSize());
            }
            if (!tbox) {
                throw InternalException("Failure in Tbox_shift_scale_value: unable to cast binary to tbox");
            }
            Datum shift_datum;
            Datum width_datum;
            if (type == LogicalType::INTEGER) {
                shift_datum = Int32GetDatum(shift);
                width_datum = Int32GetDatum(width);
            } else if (type == LogicalType::DOUBLE) {
                shift_datum = Float8GetDatum(shift);
                width_datum = Float8GetDatum(width);
            } else {
                throw InternalException("Tbox_shift_scale_value: type must be integer or double");
            }
            TBox *shifted_scaled_tbox = tbox_shift_scale_value(tbox, shift_datum, width_datum, true, true);
            size_t shifted_scaled_tbox_size = sizeof(TBox);
            char *shifted_scaled_tbox_data = (char*)malloc(shifted_scaled_tbox_size);
            memcpy(shifted_scaled_tbox_data, shifted_scaled_tbox, shifted_scaled_tbox_size);
            free(tbox);
            free(shifted_scaled_tbox);
            return MallocBlobToResult(result, shifted_scaled_tbox_data, shifted_scaled_tbox_size);
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_shift_scale_value(DataChunk &args, ExpressionState &state, Vector &result) {
    const auto &arg_type = args.data[1].GetType();
    if (arg_type.id() == LogicalTypeId::INTEGER) {
        TboxShiftScaleValueExecutor<int64_t>(args.data[0], args.data[1], args.data[2], arg_type, result, args.size());
    } else if (arg_type.id() == LogicalTypeId::DOUBLE) {
        TboxShiftScaleValueExecutor<double>(args.data[0], args.data[1], args.data[2], arg_type, result, args.size());
    } else {
        throw InternalException("Tbox_shift_scale_value: type must be integer or double");
    }
}

void TboxFunctions::Tbox_shift_scale_time(DataChunk &args, ExpressionState &state, Vector &result) {
    TernaryExecutor::Execute<string_t, interval_t, interval_t, string_t>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t tbox_str, interval_t duckdb_shift, interval_t duckdb_duration) {
            TBox *tbox = nullptr;
            if (tbox_str.GetSize() > 0) {
                tbox = (TBox*)malloc(tbox_str.GetSize());
                memcpy(tbox, tbox_str.GetDataUnsafe(), tbox_str.GetSize());
            }
            if (!tbox) {
                throw InternalException("Failure in Tbox_shift_scale_time: unable to cast binary to tbox");
            }
            MeosInterval shift = IntervaltToInterval(duckdb_shift);
            MeosInterval duration = IntervaltToInterval(duckdb_duration);
            TBox *shifted_scaled_tbox = tbox_shift_scale_time(tbox, &shift, &duration);
            size_t shifted_scaled_tbox_size = sizeof(TBox);
            char *shifted_scaled_tbox_data = (char*)malloc(shifted_scaled_tbox_size);
            memcpy(shifted_scaled_tbox_data, shifted_scaled_tbox, shifted_scaled_tbox_size);
            free(tbox);
            free(shifted_scaled_tbox);
            return MallocBlobToResult(result, shifted_scaled_tbox_data, shifted_scaled_tbox_size);
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

template <typename TB>
void TboxFunctions::TboxExpandValueExecutor(Vector &tbox, Vector &value, MeosType basetype, Vector &result, idx_t count) {
    BinaryExecutor::ExecuteWithNulls<string_t, TB, string_t>(
        tbox, value, result, count,
        [&](string_t tbox_str, TB value, ValidityMask &mask, idx_t idx) {
            TBox *tbox = nullptr;
            if (tbox_str.GetSize() > 0) {
                tbox = (TBox*)malloc(tbox_str.GetSize());
                memcpy(tbox, tbox_str.GetDataUnsafe(), tbox_str.GetSize());
            }
            if (!tbox) {
                throw InternalException("Failure in Tbox_expand_value: unable to cast binary to tbox");
            }
            Datum datum;
            if (basetype == T_INT4) {
                datum = Int32GetDatum(value);
            } else if (basetype == T_FLOAT8) {
                datum = Float8GetDatum(value);
            } else {
                throw InternalException("Unsupported basetype in TboxExpandValueExecutor");
            }
            TBox *ret = tbox_expand_value(tbox, datum, basetype);
            if (!ret) {
                free(tbox);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = sizeof(TBox);
            char *ret_data = (char*)malloc(ret_size);
            memcpy(ret_data, ret, ret_size);
            free(tbox);
            free(ret);
            return MallocBlobToResult(result, ret_data, ret_size);
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_expand_value(DataChunk &args, ExpressionState &state, Vector &result) {
    const auto &arg_type = args.data[1].GetType();
    if (arg_type.id() == LogicalTypeId::INTEGER) {
        TboxExpandValueExecutor<int32_t>(args.data[0], args.data[1], T_INT4, result, args.size());
    } else if (arg_type.id() == LogicalTypeId::DOUBLE) {
        TboxExpandValueExecutor<double>(args.data[0], args.data[1], T_FLOAT8, result, args.size());
    } else {
        throw InternalException("Tbox_expand_value: type must be integer or double");
    }
}

void TboxFunctions::Tbox_expand_time(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, interval_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox_str, interval_t interval_duckdb, ValidityMask &mask, idx_t idx) {
            TBox *tbox = nullptr;
            if (tbox_str.GetSize() > 0) {
                tbox = (TBox*)malloc(tbox_str.GetSize());
                memcpy(tbox, tbox_str.GetDataUnsafe(), tbox_str.GetSize());
            }
            if (!tbox) {
                throw InternalException("Failure in Tbox_expand_time: unable to cast binary to tbox");
            }
            MeosInterval interval = IntervaltToInterval(interval_duckdb);
            TBox *ret = tbox_expand_time(tbox, &interval);
            if (!ret) {
                free(tbox);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = sizeof(TBox);
            char *ret_data = (char*)malloc(ret_size);
            memcpy(ret_data, ret, ret_size);
            free(tbox);
            free(ret);
            return MallocBlobToResult(result, ret_data, ret_size);
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_round(DataChunk &args, ExpressionState &state, Vector &result) {
    if (args.ColumnCount() == 1){
        UnaryExecutor::Execute<string_t, string_t>(
            args.data[0], result, args.size(),
            [&](string_t tbox_str) {
                TBox *tbox = nullptr;
                if (tbox_str.GetSize() > 0) {
                    tbox = (TBox*)malloc(tbox_str.GetSize());
                    memcpy(tbox, tbox_str.GetDataUnsafe(), tbox_str.GetSize());
                }
                if (!tbox) {
                    throw InternalException("Failure in Tbox_round: unable to cast binary to tbox");
                }
                TBox *rounded_tbox = tbox_round(tbox, 0);
                size_t rounded_tbox_size = sizeof(TBox);
                char *rounded_tbox_data = (char*)malloc(rounded_tbox_size);
                memcpy(rounded_tbox_data, rounded_tbox, rounded_tbox_size);
                free(tbox);
                free(rounded_tbox);
                return MallocBlobToResult(result, rounded_tbox_data, rounded_tbox_size);
            }
        );
    } else if (args.ColumnCount() == 2) {
        BinaryExecutor::Execute<string_t, double, string_t>(
            args.data[0], args.data[1], result, args.size(),
            [&](string_t tbox_str, double precision) {
                TBox *tbox = nullptr;
                if (tbox_str.GetSize() > 0) {
                    tbox = (TBox*)malloc(tbox_str.GetSize());
                    memcpy(tbox, tbox_str.GetDataUnsafe(), tbox_str.GetSize());
                }
                if (!tbox) {
                    throw InternalException("Failure in Tbox_round: unable to cast binary to tbox");
                }
                TBox *rounded_tbox = tbox_round(tbox, precision);
                size_t rounded_tbox_size = sizeof(TBox);
                char *rounded_tbox_data = (char*)malloc(rounded_tbox_size);
                memcpy(rounded_tbox_data, rounded_tbox, rounded_tbox_size);
                free(tbox);
                free(rounded_tbox);
                return MallocBlobToResult(result, rounded_tbox_data, rounded_tbox_size);
            }
        );
    }
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Contains_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox1_str, string_t tbox2_str) {
            TBox *tbox1 = nullptr;
            if (tbox1_str.GetSize() > 0) {
                tbox1 = (TBox*)malloc(tbox1_str.GetSize());
                memcpy(tbox1, tbox1_str.GetDataUnsafe(), tbox1_str.GetSize());
            }
            if (!tbox1) {
                throw InternalException("Failure in Contains_tbox_tbox: unable to cast binary to tbox");
            }
            TBox *tbox2 = nullptr;
            if (tbox2_str.GetSize() > 0) {
                tbox2 = (TBox*)malloc(tbox2_str.GetSize());
                memcpy(tbox2, tbox2_str.GetDataUnsafe(), tbox2_str.GetSize());
            }
            if (!tbox2) {
                free(tbox1);
                throw InternalException("Failure in Contains_tbox_tbox: unable to cast binary to tbox");
            }
            bool ret = contains_tbox_tbox(tbox1, tbox2);
            free(tbox1);
            free(tbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Contained_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox1_str, string_t tbox2_str) {
            TBox *tbox1 = nullptr;
            if (tbox1_str.GetSize() > 0) {
                tbox1 = (TBox*)malloc(tbox1_str.GetSize());
                memcpy(tbox1, tbox1_str.GetDataUnsafe(), tbox1_str.GetSize());
            }
            if (!tbox1) {
                throw InternalException("Failure in Contained_tbox_tbox: unable to cast binary to tbox");
            }
            TBox *tbox2 = nullptr;
            if (tbox2_str.GetSize() > 0) {
                tbox2 = (TBox*)malloc(tbox2_str.GetSize());
                memcpy(tbox2, tbox2_str.GetDataUnsafe(), tbox2_str.GetSize());
            }
            if (!tbox2) {
                free(tbox1);
                throw InternalException("Failure in Contained_tbox_tbox: unable to cast binary to tbox");
            }
            bool ret = contained_tbox_tbox(tbox1, tbox2);
            free(tbox1);
            free(tbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Overlaps_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox1_str, string_t tbox2_str) {
            TBox *tbox1 = nullptr;
            if (tbox1_str.GetSize() > 0) {
                tbox1 = (TBox*)malloc(tbox1_str.GetSize());
                memcpy(tbox1, tbox1_str.GetDataUnsafe(), tbox1_str.GetSize());
            }
            if (!tbox1) {
                throw InternalException("Failure in Overlaps_tbox_tbox: unable to cast binary to tbox");
            }
            TBox *tbox2 = nullptr;
            if (tbox2_str.GetSize() > 0) {
                tbox2 = (TBox*)malloc(tbox2_str.GetSize());
                memcpy(tbox2, tbox2_str.GetDataUnsafe(), tbox2_str.GetSize());
            }
            if (!tbox2) {
                free(tbox1);
                throw InternalException("Failure in Overlaps_tbox_tbox: unable to cast binary to tbox");
            }
            bool ret = overlaps_tbox_tbox(tbox1, tbox2);
            free(tbox1);
            free(tbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Adjacent_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox1_str, string_t tbox2_str) {
            TBox *tbox1 = nullptr;
            if (tbox1_str.GetSize() > 0) {
                tbox1 = (TBox*)malloc(tbox1_str.GetSize());
                memcpy(tbox1, tbox1_str.GetDataUnsafe(), tbox1_str.GetSize());
            }
            if (!tbox1) {
                throw InternalException("Failure in Adjacent_tbox_tbox: unable to cast binary to tbox");
            }
            TBox *tbox2 = nullptr;
            if (tbox2_str.GetSize() > 0) {
                tbox2 = (TBox*)malloc(tbox2_str.GetSize());
                memcpy(tbox2, tbox2_str.GetDataUnsafe(), tbox2_str.GetSize());
            }
            if (!tbox2) {
                free(tbox1);
                throw InternalException("Failure in Adjacent_tbox_tbox: unable to cast binary to tbox");
            }
            bool ret = adjacent_tbox_tbox(tbox1, tbox2);
            free(tbox1);
            free(tbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Same_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox1_str, string_t tbox2_str) {
            TBox *tbox1 = nullptr;
            if (tbox1_str.GetSize() > 0) {
                tbox1 = (TBox*)malloc(tbox1_str.GetSize());
                memcpy(tbox1, tbox1_str.GetDataUnsafe(), tbox1_str.GetSize());
            }
            if (!tbox1) {
                throw InternalException("Failure in Same_tbox_tbox: unable to cast binary to tbox");
            }
            TBox *tbox2 = nullptr;
            if (tbox2_str.GetSize() > 0) {
                tbox2 = (TBox*)malloc(tbox2_str.GetSize());
                memcpy(tbox2, tbox2_str.GetDataUnsafe(), tbox2_str.GetSize());
            }
            if (!tbox2) {
                free(tbox1);
                throw InternalException("Failure in Same_tbox_tbox: unable to cast binary to tbox");
            }
            bool ret = same_tbox_tbox(tbox1, tbox2);
            free(tbox1);
            free(tbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Left_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox1_str, string_t tbox2_str) {
            TBox *tbox1 = nullptr;
            if (tbox1_str.GetSize() > 0) {
                tbox1 = (TBox*)malloc(tbox1_str.GetSize());
                memcpy(tbox1, tbox1_str.GetDataUnsafe(), tbox1_str.GetSize());
            }
            if (!tbox1) {
                throw InternalException("Failure in Left_tbox_tbox: unable to cast binary to tbox");
            }
            TBox *tbox2 = nullptr;
            if (tbox2_str.GetSize() > 0) {
                tbox2 = (TBox*)malloc(tbox2_str.GetSize());
                memcpy(tbox2, tbox2_str.GetDataUnsafe(), tbox2_str.GetSize());
            }
            if (!tbox2) {
                free(tbox1);
                throw InternalException("Failure in Left_tbox_tbox: unable to cast binary to tbox");
            }
            bool ret = left_tbox_tbox(tbox1, tbox2);
            free(tbox1);
            free(tbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Overleft_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox1_str, string_t tbox2_str) {
            TBox *tbox1 = nullptr;
            if (tbox1_str.GetSize() > 0) {
                tbox1 = (TBox*)malloc(tbox1_str.GetSize());
                memcpy(tbox1, tbox1_str.GetDataUnsafe(), tbox1_str.GetSize());
            }
            if (!tbox1) {
                throw InternalException("Failure in Overleft_tbox_tbox: unable to cast binary to tbox");
            }
            TBox *tbox2 = nullptr;
            if (tbox2_str.GetSize() > 0) {
                tbox2 = (TBox*)malloc(tbox2_str.GetSize());
                memcpy(tbox2, tbox2_str.GetDataUnsafe(), tbox2_str.GetSize());
            }
            if (!tbox2) {
                free(tbox1);
                throw InternalException("Failure in Overleft_tbox_tbox: unable to cast binary to tbox");
            }
            bool ret = overleft_tbox_tbox(tbox1, tbox2);
            free(tbox1);
            free(tbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Right_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox1_str, string_t tbox2_str) {
            TBox *tbox1 = nullptr;
            if (tbox1_str.GetSize() > 0) {
                tbox1 = (TBox*)malloc(tbox1_str.GetSize());
                memcpy(tbox1, tbox1_str.GetDataUnsafe(), tbox1_str.GetSize());
            }
            if (!tbox1) {
                throw InternalException("Failure in Right_tbox_tbox: unable to cast binary to tbox");
            }
            TBox *tbox2 = nullptr;
            if (tbox2_str.GetSize() > 0) {
                tbox2 = (TBox*)malloc(tbox2_str.GetSize());
                memcpy(tbox2, tbox2_str.GetDataUnsafe(), tbox2_str.GetSize());
            }
            if (!tbox2) {
                free(tbox1);
                throw InternalException("Failure in Right_tbox_tbox: unable to cast binary to tbox");
            }
            bool ret = right_tbox_tbox(tbox1, tbox2);
            free(tbox1);
            free(tbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Overright_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result) {  
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox1_str, string_t tbox2_str) {
            TBox *tbox1 = nullptr;
            if (tbox1_str.GetSize() > 0) {
                tbox1 = (TBox*)malloc(tbox1_str.GetSize());
                memcpy(tbox1, tbox1_str.GetDataUnsafe(), tbox1_str.GetSize());
            }
            if (!tbox1) {
                throw InternalException("Failure in Overright_tbox_tbox: unable to cast binary to tbox");
            }
            TBox *tbox2 = nullptr;
            if (tbox2_str.GetSize() > 0) {
                tbox2 = (TBox*)malloc(tbox2_str.GetSize());
                memcpy(tbox2, tbox2_str.GetDataUnsafe(), tbox2_str.GetSize());
            }
            if (!tbox2) {
                free(tbox1);
                throw InternalException("Failure in Overright_tbox_tbox: unable to cast binary to tbox");
            }
            bool ret = overright_tbox_tbox(tbox1, tbox2);
            free(tbox1);
            free(tbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Before_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox1_str, string_t tbox2_str) {
            TBox *tbox1 = nullptr;
            if (tbox1_str.GetSize() > 0) {
                tbox1 = (TBox*)malloc(tbox1_str.GetSize());
                memcpy(tbox1, tbox1_str.GetDataUnsafe(), tbox1_str.GetSize());
            }
            if (!tbox1) {
                throw InternalException("Failure in Before_tbox_tbox: unable to cast binary to tbox");
            }
            TBox *tbox2 = nullptr;
            if (tbox2_str.GetSize() > 0) {
                tbox2 = (TBox*)malloc(tbox2_str.GetSize());
                memcpy(tbox2, tbox2_str.GetDataUnsafe(), tbox2_str.GetSize());
            }
            if (!tbox2) {
                free(tbox1);
                throw InternalException("Failure in Before_tbox_tbox: unable to cast binary to tbox");
            }
            bool ret = before_tbox_tbox(tbox1, tbox2);
            free(tbox1);
            free(tbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Overbefore_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox1_str, string_t tbox2_str) {
            TBox *tbox1 = nullptr;
            if (tbox1_str.GetSize() > 0) {
                tbox1 = (TBox*)malloc(tbox1_str.GetSize());
                memcpy(tbox1, tbox1_str.GetDataUnsafe(), tbox1_str.GetSize());
            }
            if (!tbox1) {
                throw InternalException("Failure in Overbefore_tbox_tbox: unable to cast binary to tbox");
            }
            TBox *tbox2 = nullptr;
            if (tbox2_str.GetSize() > 0) {
                tbox2 = (TBox*)malloc(tbox2_str.GetSize());
                memcpy(tbox2, tbox2_str.GetDataUnsafe(), tbox2_str.GetSize());
            }
            if (!tbox2) {
                free(tbox1);
                throw InternalException("Failure in Overbefore_tbox_tbox: unable to cast binary to tbox");
            }
            bool ret = overbefore_tbox_tbox(tbox1, tbox2);
            free(tbox1);
            free(tbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::After_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox1_str, string_t tbox2_str) {
            TBox *tbox1 = nullptr;
            if (tbox1_str.GetSize() > 0) {
                tbox1 = (TBox*)malloc(tbox1_str.GetSize());
                memcpy(tbox1, tbox1_str.GetDataUnsafe(), tbox1_str.GetSize());
            }
            if (!tbox1) {
                throw InternalException("Failure in After_tbox_tbox: unable to cast binary to tbox");
            }
            TBox *tbox2 = nullptr;
            if (tbox2_str.GetSize() > 0) {
                tbox2 = (TBox*)malloc(tbox2_str.GetSize());
                memcpy(tbox2, tbox2_str.GetDataUnsafe(), tbox2_str.GetSize());
            }
            if (!tbox2) {
                free(tbox1);
                throw InternalException("Failure in After_tbox_tbox: unable to cast binary to tbox");
            }
            bool ret = after_tbox_tbox(tbox1, tbox2);
            free(tbox1);
            free(tbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Overafter_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox1_str, string_t tbox2_str) {
            TBox *tbox1 = nullptr;
            if (tbox1_str.GetSize() > 0) {
                tbox1 = (TBox*)malloc(tbox1_str.GetSize());
                memcpy(tbox1, tbox1_str.GetDataUnsafe(), tbox1_str.GetSize());
            }
            if (!tbox1) {
                throw InternalException("Failure in Overafter_tbox_tbox: unable to cast binary to tbox");
            }
            TBox *tbox2 = nullptr;
            if (tbox2_str.GetSize() > 0) {
                tbox2 = (TBox*)malloc(tbox2_str.GetSize());
                memcpy(tbox2, tbox2_str.GetDataUnsafe(), tbox2_str.GetSize());
            }
            if (!tbox2) {
                free(tbox1);
                throw InternalException("Failure in Overafter_tbox_tbox: unable to cast binary to tbox");
            }
            bool ret = overafter_tbox_tbox(tbox1, tbox2);
            free(tbox1);
            free(tbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Union_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox1_str, string_t tbox2_str) {
            TBox *tbox1 = nullptr;
            if (tbox1_str.GetSize() > 0) {
                tbox1 = (TBox*)malloc(tbox1_str.GetSize());
                memcpy(tbox1, tbox1_str.GetDataUnsafe(), tbox1_str.GetSize());
            }
            if (!tbox1) {
                throw InternalException("Failure in Union_tbox_tbox: unable to cast binary to tbox");
            }
            TBox *tbox2 = nullptr;
            if (tbox2_str.GetSize() > 0) {
                tbox2 = (TBox*)malloc(tbox2_str.GetSize());
                memcpy(tbox2, tbox2_str.GetDataUnsafe(), tbox2_str.GetSize());
            }
            if (!tbox2) {
                free(tbox1);
                throw InternalException("Failure in Union_tbox_tbox: unable to cast binary to tbox");
            }
            TBox *ret = union_tbox_tbox(tbox1, tbox2, true);
            if (!ret) {
                free(tbox1);
                free(tbox2);
                return string_t();
            }
            size_t ret_size = sizeof(TBox);
            char *ret_data = (char*)malloc(ret_size);
            memcpy(ret_data, ret, ret_size);
            free(tbox1);
            free(tbox2);
            free(ret);
            return MallocBlobToResult(result, ret_data, ret_size);
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Intersection_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox1_str, string_t tbox2_str) {
            TBox *tbox1 = nullptr;
            if (tbox1_str.GetSize() > 0) {
                tbox1 = (TBox*)malloc(tbox1_str.GetSize());
                memcpy(tbox1, tbox1_str.GetDataUnsafe(), tbox1_str.GetSize());
            }
            if (!tbox1) {
                throw InternalException("Failure in Intersect_tbox_tbox: unable to cast binary to tbox");
            }
            TBox *tbox2 = nullptr;
            if (tbox2_str.GetSize() > 0) {
                tbox2 = (TBox*)malloc(tbox2_str.GetSize());
                memcpy(tbox2, tbox2_str.GetDataUnsafe(), tbox2_str.GetSize());
            }
            if (!tbox2) {
                free(tbox1);
                throw InternalException("Failure in Intersect_tbox_tbox: unable to cast binary to tbox");
            }
            TBox *ret = intersection_tbox_tbox(tbox1, tbox2);
            if (!ret) {
                free(tbox1);
                free(tbox2);
                return string_t();
            }
            size_t ret_size = sizeof(TBox);
            char *ret_data = (char*)malloc(ret_size);
            memcpy(ret_data, ret, ret_size);
            free(tbox1);
            free(tbox2);
            free(ret);
            return MallocBlobToResult(result, ret_data, ret_size);
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

// Comparison operators
void TboxFunctions::Tbox_eq(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox1_str, string_t tbox2_str) {
            TBox *tbox1 = nullptr;
            if (tbox1_str.GetSize() > 0) {
                tbox1 = (TBox*)malloc(tbox1_str.GetSize());
                memcpy(tbox1, tbox1_str.GetDataUnsafe(), tbox1_str.GetSize());
            }
            if (!tbox1) {
                throw InternalException("Failure in Tbox_eq: unable to cast binary to tbox");
            }
            TBox *tbox2 = nullptr;
            if (tbox2_str.GetSize() > 0) {
                tbox2 = (TBox*)malloc(tbox2_str.GetSize());
                memcpy(tbox2, tbox2_str.GetDataUnsafe(), tbox2_str.GetSize());
            }
            if (!tbox2) {
                free(tbox1);
                throw InternalException("Failure in Tbox_eq: unable to cast binary to tbox");
            }
            bool ret = tbox_eq(tbox1, tbox2);
            free(tbox1);
            free(tbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_ne(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox1_str, string_t tbox2_str) {
            TBox *tbox1 = nullptr;
            if (tbox1_str.GetSize() > 0) {
                tbox1 = (TBox*)malloc(tbox1_str.GetSize());
                memcpy(tbox1, tbox1_str.GetDataUnsafe(), tbox1_str.GetSize());
            }
            if (!tbox1) {
                throw InternalException("Failure in Tbox_ne: unable to cast binary to tbox");
            }
            TBox *tbox2 = nullptr;
            if (tbox2_str.GetSize() > 0) {
                tbox2 = (TBox*)malloc(tbox2_str.GetSize());
                memcpy(tbox2, tbox2_str.GetDataUnsafe(), tbox2_str.GetSize());
            }
            if (!tbox2) {
                free(tbox1);
                throw InternalException("Failure in Tbox_ne: unable to cast binary to tbox");
            }
            bool ret = tbox_ne(tbox1, tbox2);
            free(tbox1);
            free(tbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_lt(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox1_str, string_t tbox2_str) {
            TBox *tbox1 = nullptr;
            if (tbox1_str.GetSize() > 0) {
                tbox1 = (TBox*)malloc(tbox1_str.GetSize());
                memcpy(tbox1, tbox1_str.GetDataUnsafe(), tbox1_str.GetSize());
            }
            if (!tbox1) {
                throw InternalException("Failure in Tbox_lt: unable to cast binary to tbox");
            }
            TBox *tbox2 = nullptr;
            if (tbox2_str.GetSize() > 0) {
                tbox2 = (TBox*)malloc(tbox2_str.GetSize());
                memcpy(tbox2, tbox2_str.GetDataUnsafe(), tbox2_str.GetSize());
            }
            if (!tbox2) {
                free(tbox1);
                throw InternalException("Failure in Tbox_lt: unable to cast binary to tbox");
            }
            bool ret = tbox_lt(tbox1, tbox2);
            free(tbox1);
            free(tbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_le(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox1_str, string_t tbox2_str) {
            TBox *tbox1 = nullptr;
            if (tbox1_str.GetSize() > 0) {
                tbox1 = (TBox*)malloc(tbox1_str.GetSize());
                memcpy(tbox1, tbox1_str.GetDataUnsafe(), tbox1_str.GetSize());
            }
            if (!tbox1) {
                throw InternalException("Failure in Tbox_le: unable to cast binary to tbox");
            }
            TBox *tbox2 = nullptr;
            if (tbox2_str.GetSize() > 0) {
                tbox2 = (TBox*)malloc(tbox2_str.GetSize());
                memcpy(tbox2, tbox2_str.GetDataUnsafe(), tbox2_str.GetSize());
            }
            if (!tbox2) {
                free(tbox1);
                throw InternalException("Failure in Tbox_le: unable to cast binary to tbox");
            }
            bool ret = tbox_le(tbox1, tbox2);
            free(tbox1);
            free(tbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_gt(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox1_str, string_t tbox2_str) {
            TBox *tbox1 = nullptr;
            if (tbox1_str.GetSize() > 0) {
                tbox1 = (TBox*)malloc(tbox1_str.GetSize());
                memcpy(tbox1, tbox1_str.GetDataUnsafe(), tbox1_str.GetSize());
            }
            if (!tbox1) {
                throw InternalException("Failure in Tbox_gt: unable to cast binary to tbox");
            }
            TBox *tbox2 = nullptr;
            if (tbox2_str.GetSize() > 0) {
                tbox2 = (TBox*)malloc(tbox2_str.GetSize());
                memcpy(tbox2, tbox2_str.GetDataUnsafe(), tbox2_str.GetSize());
            }
            if (!tbox2) {
                free(tbox1);
                throw InternalException("Failure in Tbox_gt: unable to cast binary to tbox");
            }
            bool ret = tbox_gt(tbox1, tbox2);
            free(tbox1);
            free(tbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_ge(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox1_str, string_t tbox2_str) {
            TBox *tbox1 = nullptr;
            if (tbox1_str.GetSize() > 0) {
                tbox1 = (TBox*)malloc(tbox1_str.GetSize());
                memcpy(tbox1, tbox1_str.GetDataUnsafe(), tbox1_str.GetSize());
            }
            if (!tbox1) {
                throw InternalException("Failure in Tbox_ge: unable to cast binary to tbox");
            }
            TBox *tbox2 = nullptr;
            if (tbox2_str.GetSize() > 0) {
                tbox2 = (TBox*)malloc(tbox2_str.GetSize());
                memcpy(tbox2, tbox2_str.GetDataUnsafe(), tbox2_str.GetSize());
            }
            if (!tbox2) {
                free(tbox1);
                throw InternalException("Failure in Tbox_ge: unable to cast binary to tbox");
            }
            bool ret = tbox_ge(tbox1, tbox2);
            free(tbox1);
            free(tbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_cmp(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, int32_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tbox1_str, string_t tbox2_str) {
            TBox *tbox1 = nullptr;
            if (tbox1_str.GetSize() > 0) {
                tbox1 = (TBox*)malloc(tbox1_str.GetSize());
                memcpy(tbox1, tbox1_str.GetDataUnsafe(), tbox1_str.GetSize());
            }
            if (!tbox1) {
                throw InternalException("Failure in Tbox_cmp: unable to cast binary to tbox");
            }
            TBox *tbox2 = nullptr;
            if (tbox2_str.GetSize() > 0) {
                tbox2 = (TBox*)malloc(tbox2_str.GetSize());
                memcpy(tbox2, tbox2_str.GetDataUnsafe(), tbox2_str.GetSize());
            }
            if (!tbox2) {
                free(tbox1);
                throw InternalException("Failure in Tbox_cmp: unable to cast binary to tbox");
            }
            int32_t ret = tbox_cmp(tbox1, tbox2);
            free(tbox1);
            free(tbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
 * WKB / hex-WKB serialization + hashing
 ****************************************************/

void TboxFunctions::Tbox_as_wkb(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            if (input.GetSize() < sizeof(TBox)) {
                throw InvalidInputException("asBinary: invalid TBox value");
            }
            uint8_t *copy = (uint8_t *)malloc(input.GetSize());
            memcpy(copy, input.GetData(), input.GetSize());
            TBox *tbox = reinterpret_cast<TBox *>(copy);
            size_t wkb_size = 0;
            uint8_t *wkb = tbox_as_wkb(tbox, WKB_EXTENDED, &wkb_size);
            free(copy);
            if (!wkb || wkb_size == 0) {
                if (wkb) free(wkb);
                throw InternalException("asBinary: tbox_as_wkb failed");
            }
            string_t ret(reinterpret_cast<const char *>(wkb), wkb_size);
            string_t stored = StringVector::AddStringOrBlob(result, ret);
            free(wkb);
            return stored;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_as_hexwkb(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            if (input.GetSize() < sizeof(TBox)) {
                throw InvalidInputException("asHexWKB: invalid TBox value");
            }
            uint8_t *copy = (uint8_t *)malloc(input.GetSize());
            memcpy(copy, input.GetData(), input.GetSize());
            TBox *tbox = reinterpret_cast<TBox *>(copy);
            size_t hex_size = 0;
            char *hex = tbox_as_hexwkb(tbox, WKB_EXTENDED, &hex_size);
            (void)hex_size;
            free(copy);
            if (!hex) {
                throw InternalException("asHexWKB: tbox_as_hexwkb failed");
            }
            string_t stored = StringVector::AddString(result, hex);
            free(hex);
            return stored;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_from_wkb(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_wkb) -> string_t {
            if (input_wkb.GetSize() == 0) {
                throw InvalidInputException("tboxFromBinary: empty input");
            }
            uint8_t *wkb = (uint8_t *)malloc(input_wkb.GetSize());
            memcpy(wkb, input_wkb.GetData(), input_wkb.GetSize());
            TBox *tbox = tbox_from_wkb(wkb, input_wkb.GetSize());
            free(wkb);
            if (!tbox) {
                throw InvalidInputException("tboxFromBinary: invalid WKB");
            }
            size_t sz = sizeof(TBox);
            uint8_t *out = (uint8_t *)malloc(sz);
            memcpy(out, tbox, sz);
            string_t ret(reinterpret_cast<const char *>(out), sz);
            string_t stored = StringVector::AddStringOrBlob(result, ret);
            free(out);
            free(tbox);
            return stored;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_from_hexwkb(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_hex) -> string_t {
            std::string hex(input_hex.GetData(), input_hex.GetSize());
            TBox *tbox = tbox_from_hexwkb(hex.c_str());
            if (!tbox) {
                throw InvalidInputException("tboxFromHexWKB: invalid hex-WKB");
            }
            size_t sz = sizeof(TBox);
            uint8_t *out = (uint8_t *)malloc(sz);
            memcpy(out, tbox, sz);
            string_t ret(reinterpret_cast<const char *>(out), sz);
            string_t stored = StringVector::AddStringOrBlob(result, ret);
            free(out);
            free(tbox);
            return stored;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_hash(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, int32_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> int32_t {
            if (input.GetSize() < sizeof(TBox)) {
                throw InvalidInputException("tbox_hash: invalid TBox value");
            }
            uint8_t *copy = (uint8_t *)malloc(input.GetSize());
            memcpy(copy, input.GetData(), input.GetSize());
            TBox *tbox = reinterpret_cast<TBox *>(copy);
            uint32_t h = tbox_hash(tbox);
            free(copy);
            return static_cast<int32_t>(h);
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_hash_extended(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, int64_t, int64_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input, int64_t seed) -> int64_t {
            if (input.GetSize() < sizeof(TBox)) {
                throw InvalidInputException("tbox_hash_extended: invalid TBox value");
            }
            uint8_t *copy = (uint8_t *)malloc(input.GetSize());
            memcpy(copy, input.GetData(), input.GetSize());
            TBox *tbox = reinterpret_cast<TBox *>(copy);
            uint64_t h = tbox_hash_extended(tbox, static_cast<uint64_t>(seed));
            free(copy);
            return static_cast<int64_t>(h);
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

} // namespace duckdb
