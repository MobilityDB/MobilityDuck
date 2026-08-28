#pragma once

#include "meos_wrapper_simple.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

namespace duckdb {

/* S2CELL is a 64-bit unsigned cell id (Google S2 spherical DGGS); surfaced as
 * BIGINT (signed reinterpretation is safe — equality and ordering care only
 * about the bit pattern).  TS2CELL is the temporal cell index, stored as a
 * Temporal* blob (BLOB).  Analogue of QUADBIN / TQUADBIN and H3INDEX /
 * TH3INDEX; its text form is the S2 hex token (47c3c3), which s2cell_in and
 * s2cell_out own. */
struct S2cellTypes {
    static LogicalType s2cell();
    static LogicalType ts2cell();

    static void RegisterTypes(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
};

struct S2cellFunctions {
    /* In/out — static S2CELL cell (BIGINT), the hex token MEOS parses */
    static bool S2cell_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool S2cell_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* In/out — TS2CELL temporal value */
    static bool Ts2cell_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Ts2cell_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
};

} // namespace duckdb
