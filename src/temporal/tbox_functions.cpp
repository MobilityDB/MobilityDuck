#include "meos_wrapper_simple.hpp"
#include "common.hpp"

#include "temporal/tbox_functions.hpp"
#include "time_util.hpp"

// #include "duckdb/common/types/blob.hpp"
#include "duckdb/common/exception.hpp"
// #include "duckdb/common/string_util.hpp"
// #include "duckdb/function/scalar_function.hpp"
// #include "duckdb/main/extension_util.hpp"
// #include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

namespace duckdb {

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
                return string_t(tbox_data, tbox_size);
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
            return string_t(str);
        }
    );
    return success;
}

template <typename TA>
void TboxFunctions::NumberTimestamptzToTboxExecutor(Vector &value, Vector &t, meosType basetype, Vector &result, idx_t count) {
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
            return string_t(tbox_data, tbox_size);
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
            return string_t(tbox_data, tbox_size);
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

template <typename TA>
void TboxFunctions::NumberTstzspanToTboxExecutor(Vector &value, Vector &span_str, meosType basetype, Vector &result, idx_t count) {
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
            return string_t(tbox_data, tbox_size);
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
            return string_t(tbox_data, tbox_size);
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

template <typename TA>
void TboxFunctions::NumberToTboxExecutor(Vector &value, meosType basetype, Vector &result, idx_t count) {
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
            return string_t(tbox_data, tbox_size);
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
            return string_t(tbox_data, tbox_size);
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
            return string_t(tbox_data, tbox_size);
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
            return string_t(tbox_data, tbox_size);
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
            return string_t(span_data, span_size);
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
            return string_t(span_data, span_size);
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
            return string_t(span_data, span_size);
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
            return string_t(shifted_tbox_data, shifted_tbox_size);
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
            return string_t(shifted_tbox_data, shifted_tbox_size);
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
            return string_t(scaled_tbox_data, scaled_tbox_size);
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
            return string_t(scaled_tbox_data, scaled_tbox_size);
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
            return string_t(shifted_scaled_tbox_data, shifted_scaled_tbox_size);
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
            return string_t(shifted_scaled_tbox_data, shifted_scaled_tbox_size);
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

template <typename TB>
void TboxFunctions::TboxExpandValueExecutor(Vector &tbox, Vector &value, meosType basetype, Vector &result, idx_t count) {
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
            return string_t(ret_data, ret_size);
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TboxFunctions::Tbox_expand_value(DataChunk &args, ExpressionState &state, Vector &result) {
    const auto &arg_type = args.data[1].GetType();
    if (arg_type.id() == LogicalTypeId::INTEGER) {
        TboxExpandValueExecutor<int64_t>(args.data[0], args.data[1], T_INT4, result, args.size());
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
            return string_t(ret_data, ret_size);
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

} // namespace duckdb