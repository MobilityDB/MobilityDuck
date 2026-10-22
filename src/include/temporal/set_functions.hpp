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
    // Set_values retired: getValues(<set>) is generated (set_values) in generated_temporal_udfs.cpp.

    //TODO: Selectivity functions
};

} // namespace duckdb
