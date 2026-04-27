#pragma once

#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include <tydef.hpp>

extern "C" {
#include <meos.h>
#include <meos_internal.h>
#include <meos_geo.h>
}

namespace duckdb {

struct SetFunctions {
    static void RegisterScalarFunctions(ExtensionLoader &loader);

    // for cast
    static bool Set_to_text(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Text_to_set(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Value_to_set_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Intset_to_floatset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Floatset_to_intset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Dateset_to_tstzset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tstzset_to_dateset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    // scalar functions
    static void Set_as_text(DataChunk &args, ExpressionState &state, Vector &result);
    static void Set_as_binary(DataChunk &args, ExpressionState &state, Vector &result);
    static void Set_as_hexwkb(DataChunk &args, ExpressionState &state, Vector &result);
    static void Set_constructor(DataChunk &args, ExpressionState &state, Vector &result);
    static void Value_to_set(DataChunk &args, ExpressionState &state, Vector &result);
    static void Intset_to_floatset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Floatset_to_intset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Dateset_to_tstzset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tstzset_to_dateset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Set_mem_size(DataChunk &args, ExpressionState &state, Vector &result);
    static void Set_hash(DataChunk &args, ExpressionState &state, Vector &result);
    static void Set_hash_extended(DataChunk &args, ExpressionState &state, Vector &result);
    static void Set_num_values(DataChunk &args, ExpressionState &state, Vector &result);
    static void Set_start_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Set_end_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Set_value_n(DataChunk &args, ExpressionState &state, Vector &result_vec);
    static void Set_values(DataChunk &args, ExpressionState &state, Vector &result);
    static void Numset_shift(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tstzset_shift(DataChunk &args, ExpressionState &state, Vector &result);
    static void Numset_scale(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tstzset_scale(DataChunk &args, ExpressionState &state, Vector &result);
    static void Numset_shift_scale(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tstzset_shift_scale(DataChunk &args, ExpressionState &state, Vector &result);
    static void Floatset_floor(DataChunk &args, ExpressionState &state, Vector &result);
    static void Floatset_ceil(DataChunk &args, ExpressionState &state, Vector &result);
    static void Floatset_round(DataChunk &args, ExpressionState &state, Vector &result);
    static void Floatset_degrees(DataChunk &args, ExpressionState &state, Vector &result);
    static void Floatset_radians(DataChunk &args, ExpressionState &state, Vector &result);
    static void Textset_lower(DataChunk &args, ExpressionState &state, Vector &result);
    static void Textset_upper(DataChunk &args, ExpressionState &state, Vector &result);
    static void Textset_initcap(DataChunk &args, ExpressionState &state, Vector &result);
    static void Textcat_text_textset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Textcat_textset_text(DataChunk &args, ExpressionState &state, Vector &result);

    // operators 
    static void Contains_set_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contains_set_set(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contained_value_set(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contained_set_set(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overlaps_set_set(DataChunk &args, ExpressionState &state, Vector &result);
    static void Left_value_set(DataChunk &args, ExpressionState &state, Vector &result);
    static void Left_set_set(DataChunk &args, ExpressionState &state, Vector &result);
    static void Left_set_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Right_value_set(DataChunk &args, ExpressionState &state, Vector &result);
    static void Right_set_set(DataChunk &args, ExpressionState &state, Vector &result);
    static void Right_set_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overleft_value_set(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overleft_set_set(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overleft_set_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overright_value_set(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overright_set_set(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overright_set_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Union_set_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Union_value_set(DataChunk &args, ExpressionState &state, Vector &result);
    static void Union_set_set(DataChunk &args, ExpressionState &state, Vector &result);
    static void Minus_set_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Minus_set_set(DataChunk &args, ExpressionState &state, Vector &result);
    static void Minus_value_set(DataChunk &args, ExpressionState &state, Vector &result);
    static void Intersect_set_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Intersect_set_set(DataChunk &args, ExpressionState &state, Vector &result);
    static void Intersect_value_set(DataChunk &args, ExpressionState &state, Vector &result);
    static void Distance_set_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Distance_set_set(DataChunk &args, ExpressionState &state, Vector &result);
    static void Distance_value_set(DataChunk &args, ExpressionState &state, Vector &result);
    static void Distance_value_value(DataChunk &args, ExpressionState &state, Vector &result);
    //TODO: Selectivity functions
    static void Set_eq(DataChunk &args, ExpressionState &state, Vector &result);
    static void Set_ne(DataChunk &args, ExpressionState &state, Vector &result);
    static void Set_lt(DataChunk &args, ExpressionState &state, Vector &result);
    static void Set_le(DataChunk &args, ExpressionState &state, Vector &result);
    static void Set_ge(DataChunk &args, ExpressionState &state, Vector &result);
    static void Set_gt(DataChunk &args, ExpressionState &state, Vector &result);
    static void Set_cmp(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
