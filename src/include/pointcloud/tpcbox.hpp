#pragma once

#include "meos_wrapper_simple.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

namespace duckdb {

/* tpcbox is the bounding box of a temporal point cloud: an stbox — a space and
 * time extent with an SRID — carrying the pcid of the schema its coordinates are
 * written in. Two boxes of different pcid describe different coordinate spaces
 * and never overlap, which is what separates it from a plain stbox.
 *
 * It is a fixed-layout value surfaced as a BLOB, the same shape as stbox and
 * tbox. Only the type registration and the text I/O boundary are hand-written
 * here; the operation surface is generated from the MEOS-API catalog into
 * src/generated/.
 */
struct TpcboxType {
    static LogicalType tpcbox();

    static void RegisterType(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
};

struct TpcboxFunctions {
    static bool Tpcbox_in_cast(Vector &source, Vector &result, idx_t count,
                               CastParameters &parameters);
    static bool Tpcbox_out_cast(Vector &source, Vector &result, idx_t count,
                                CastParameters &parameters);
};

} // namespace duckdb
