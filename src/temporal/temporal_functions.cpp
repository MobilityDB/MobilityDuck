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
#include "temporal/temporal_functions.hpp"
#include "temporal/spanset.hpp"
#include "geo_util.hpp"
#include "duckdb_version_compat.hpp"

#include "duckdb/common/types/blob.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <cmath>
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include <functional>

#include "time_util.hpp"
#include "temporal/temporal_blob.hpp"

namespace duckdb {

static const alias_type_struct DUCKDB_ALIAS_TYPE_CATALOG[] = {
    {(char*)"tint", T_TINT},
    {(char*)"tbigint", T_TBIGINT},
    {(char*)"tfloat", T_TFLOAT},
    {(char*)"tbool", T_TBOOL},
    {(char*)"ttext", T_TTEXT},
    {(char*)"tgeompoint", T_TGEOMPOINT},
    {(char*)"tgeogpoint", T_TGEOGPOINT},
    {(char*)"tgeometry", T_TGEOMETRY}
};

MeosType TemporalHelpers::GetTemptypeFromAlias(const char *alias) {
    for (size_t i = 0; i < sizeof(DUCKDB_ALIAS_TYPE_CATALOG) / sizeof(DUCKDB_ALIAS_TYPE_CATALOG[0]); i++) {
        if (strcmp(alias, DUCKDB_ALIAS_TYPE_CATALOG[i].alias) == 0) {
            return DUCKDB_ALIAS_TYPE_CATALOG[i].temptype;
        }
    }
    throw InternalException("Unknown alias: " + std::string(alias));
}

/* ***************************************************
 * In/out functions: VARCHAR <-> Temporal
 ****************************************************/

bool TemporalFunctions::Temporal_in(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    auto &target_type = result.GetType();
    MeosType temptype = TemporalHelpers::GetTemptypeFromAlias(target_type.GetAlias().c_str());
    bool success = true;
    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(
        source, result, count,
        [&](string_t input_string, ValidityMask &mask, idx_t idx) {
            std::string input_str = input_string.GetString();
            Temporal *temp = temporal_in(input_str.c_str(), temptype);
            if (!temp) {
                throw InternalException("Failure in Temporal_in: unable to cast string to temporal");
                success = false;
                return string_t();
            }
            size_t temp_size = temporal_mem_size(temp);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, temp, temp_size);
            string_t output(reinterpret_cast<char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, output);

            free(temp_data);
            free(temp);
            return stored_data;
        }
    );
    return success;
}

bool TemporalFunctions::Temporal_out(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    bool success = true;
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_out] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid Temporal input: " + input_blob.GetString());
            }

            char *ret = TemporalAsText(temp, OUT_DEFAULT_DECIMAL_DIGITS);
            if (!ret) {
                free(data_copy);
                throw InternalException("Failure in Temporal_out: unable to cast temporal to string");
            }
            std::string ret_string(ret);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_string);

            free(ret);
            free(temp);
            return stored_data;
        }
    );
    return success;
}

bool TemporalFunctions::Blob_to_tstzspanset(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    bool success = true;
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) {
            return StringVector::AddStringOrBlob(result, input_blob);
        }
    );
    return success;
}

/* ***************************************************
 * Constructor functions
 ****************************************************/

/* ***************************************************
 * Conversion functions: [TYPE] -> Temporal
 ****************************************************/

static string_t Tbool_to_tint_common(string_t input, Vector &result) {
    const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
    size_t data_size = input.GetSize();
    if (data_size < sizeof(void*)) {
        throw InvalidInputException("[Tbool_to_tint] Invalid Temporal data: insufficient size");
    }
    uint8_t *data_copy = (uint8_t*)malloc(data_size);
    memcpy(data_copy, data, data_size);
    Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

    Temporal *ret = tbool_to_tint(temp);
    if (!ret) {
        free(data_copy);
        throw InternalException("Failure in Tbool_to_tint: unable to convert tbool to tint");
    }

    size_t ret_size = temporal_mem_size(ret);
    uint8_t *ret_data = (uint8_t*)malloc(ret_size);
    memcpy(ret_data, ret, ret_size);
    string_t result_str(reinterpret_cast<char*>(ret_data), ret_size);
    string_t stored_data = StringVector::AddStringOrBlob(result, result_str);

    free(ret_data);
    free(ret);
    free(data_copy);
    return stored_data;
}

bool TemporalFunctions::Tbool_to_tint_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input) {
            return Tbool_to_tint_common(input, result);
        }
    );
    return true;
}

static inline string_t Tint_to_tfloat_common(string_t input, Vector &result) {
    const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
    size_t data_size = input.GetSize();
    if (data_size < sizeof(void*)) {
        throw InvalidInputException("[Tint_to_tfloat] Invalid Temporal data: insufficient size");
    }
    uint8_t *data_copy = (uint8_t*)malloc(data_size);
    memcpy(data_copy, data, data_size);
    Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
    if (!temp) {
        free(data_copy);
        throw InternalException("Failure in Tint_to_tfloat: unable to cast string to temporal");
    }
    Temporal *ret = tint_to_tfloat(temp);
    if (!ret) {
        free(data_copy);
        throw InternalException("Failure in Tint_to_tfloat: unable to convert tint to tfloat");
    }
    size_t ret_size = temporal_mem_size(ret);
    uint8_t *ret_data = (uint8_t*)malloc(ret_size);
    memcpy(ret_data, ret, ret_size);
    string_t result_str(reinterpret_cast<char*>(ret_data), ret_size);
    string_t stored_data = StringVector::AddStringOrBlob(result, result_str);
    free(ret_data);
    free(ret);
    free(data_copy);
    return stored_data;
}
bool TemporalFunctions::Tint_to_tfloat_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input) {
            return Tint_to_tfloat_common(input, result);
        }
    );
    return true;
}
static inline string_t Tfloat_to_tint_common(string_t input, Vector &result) {
    const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
    size_t data_size = input.GetSize();
    if (data_size < sizeof(void*)) {
        throw InvalidInputException("[Tfloat_to_tint] Invalid Temporal data: insufficient size");
    }
    uint8_t *data_copy = (uint8_t*)malloc(data_size);
    memcpy(data_copy, data, data_size);
    Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
    if (!temp) {
        free(data_copy);
        throw InternalException("Failure in Tfloat_to_tint: unable to cast string to temporal");
    }
    Temporal *ret = tfloat_to_tint(temp);
    if (!ret) {
        free(data_copy);
        throw InternalException("Failure in Tfloat_to_tint: unable to convert tfloat to tint");
    }
    size_t ret_size = temporal_mem_size(ret);
    uint8_t *ret_data = (uint8_t*)malloc(ret_size);
    memcpy(ret_data, ret, ret_size);
    string_t result_str(reinterpret_cast<char*>(ret_data), ret_size);
    string_t stored_data = StringVector::AddStringOrBlob(result, result_str);
    free(ret_data);
    free(ret);
    free(data_copy);
    return stored_data;
}
bool TemporalFunctions::Tfloat_to_tint_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input) {
            return Tfloat_to_tint_common(input, result);
        }
    );
    return true;
}

static inline string_t Tnumber_to_tbox_common(Datum datum, MeosType basetype, Vector &result) {
    TBox *tbox = number_tbox(datum, basetype);
    size_t tbox_size = sizeof(TBox);
    uint8_t *tbox_data = (uint8_t*)malloc(tbox_size);
    memcpy(tbox_data, tbox, tbox_size);
    string_t result_str(reinterpret_cast<char*>(tbox_data), tbox_size);
    string_t stored_data = StringVector::AddStringOrBlob(result, result_str);
    free(tbox_data);
    free(tbox);
    return stored_data;
}
static string_t Tnumber_temporal_to_tbox_common(string_t input, Vector &result) {
    const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
    size_t data_size = input.GetSize();
    if (data_size < sizeof(void*)) {
        throw InvalidInputException("[Tnumber_to_tbox] Invalid Temporal data: insufficient size");
    }
    uint8_t *data_copy = (uint8_t*)malloc(data_size);
    memcpy(data_copy, data, data_size);
    Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

    TBox *tbox = tnumber_to_tbox(temp);
    if (!tbox) {
        free(data_copy);
        throw InternalException("Failure in Tnumber_to_tbox: unable to convert temporal to tbox");
    }

    size_t tbox_size = sizeof(TBox);
    uint8_t *tbox_data = (uint8_t*)malloc(tbox_size);
    memcpy(tbox_data, tbox, tbox_size);
    string_t result_str(reinterpret_cast<char*>(tbox_data), tbox_size);
    string_t stored_data = StringVector::AddStringOrBlob(result, result_str);

    free(tbox_data);
    free(tbox);
    free(data_copy);
    return stored_data;
}

bool TemporalFunctions::Tnumber_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    if (source.GetType().id() == LogicalTypeId::BLOB) {
        UnaryExecutor::Execute<string_t, string_t>(
            source, result, count,
            [&](string_t input) {
                return Tnumber_temporal_to_tbox_common(input, result);
            }
        );
    } else if (source.GetType().id() == LogicalTypeId::DOUBLE) {
        MeosType basetype = TemporalHelpers::GetTemptypeFromAlias(source.GetType().GetAlias().c_str());
        UnaryExecutor::Execute<double, string_t>(
            source, result, count,
            [&](double value) {
                return Tnumber_to_tbox_common(Float8GetDatum(value), basetype, result);
            }
        );
    } else if (source.GetType().id() == LogicalTypeId::INTEGER || source.GetType().id() == LogicalTypeId::BIGINT ||
               source.GetType().id() == LogicalTypeId::SMALLINT || source.GetType().id() == LogicalTypeId::TINYINT) {
        MeosType basetype = TemporalHelpers::GetTemptypeFromAlias(source.GetType().GetAlias().c_str());
        UnaryExecutor::Execute<int64_t, string_t>(
            source, result, count,
            [&](int64_t value) {
                return Tnumber_to_tbox_common((Datum)value, basetype, result);
            }
        );
    } else {
        throw InvalidInputException("Invalid argument type for Tnumber_to_tbox_cast: " + source.GetType().ToString());
    }
    return true;
}
/****************************************************
 * Special Cast
****************************************************/

void TemporalFunctions::Temporal_enforce_typmod(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, int32_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input, int32_t typmod) -> string_t {
            return StringVector::AddStringOrBlob(result, input);
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

// Tnumber_at_tbox / Tnumber_minus_tbox retired: atTbox/minusTbox are generated from the
// catalog (Gen_tnumber_at_tbox / Gen_tnumber_minus_tbox in generated_temporal_udfs.cpp),
// which call the same MEOS tnumber_at_tbox / tnumber_minus_tbox.

// Tnumber_avg_value retired: avgValue(tint | tbigint | tfloat) is generated from the catalog
// (Gen_tnumber_avg_value in generated_temporal_udfs.cpp), which calls the same MEOS
// tnumber_avg_value.

// whenTrue (tbool_when_true), tsample (temporal_tsample), shiftScaleTime
// (temporal_shift_scale_time) and the <type>Inst conversions (temporal_as_tinstant) are generated
// from the catalog (generated_temporal_udfs.cpp).

/* ***************************************************
 * Arithmetic operators on tnumber
 ****************************************************/

// floor/ceil/round/degrees/radians on tfloat are generated from the catalog
// (generated_temporal_udfs.cpp); no hand bodies remain.

/* ***************************************************
 * Math functions
 ****************************************************/

// round(tfloat) at both arities is generated from the catalog temporal_round
// signature (generated_temporal_udfs.cpp); no hand body remains.

} // namespace duckdb
