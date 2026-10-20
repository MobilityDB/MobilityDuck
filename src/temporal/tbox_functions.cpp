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
        // The registration declares INTEGER and the executor reads the vector as the type
        // named here, so it is int32_t: reading it as int64_t takes 64 bits from a 32-bit
        // vector, which DuckDB 1.4 refuses with `Expected vector of type INT64, but found
        // vector of type INT32`. The lambda's own `Int32GetDatum` says the same width.
        NumberTimestamptzToTboxExecutor<int32_t>(args.data[0], args.data[1], T_INT4, result, args.size());
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
        NumberTstzspanToTboxExecutor<int32_t>(args.data[0], args.data[1], T_INT4, result, args.size());
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

// hash(tbox) / hashExtended(tbox, UBIGINT) are generated from the catalog
// (bare canonical names) in generated_temporal_udfs.cpp.

} // namespace duckdb
