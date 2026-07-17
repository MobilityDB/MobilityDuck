#pragma once

#include "meos_wrapper_simple.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

namespace duckdb {

/* cbuffer is a circular buffer (a point + a radius); a varlena value surfaced as
 * a BLOB.  tcbuffer is its temporal counterpart, stored as a Temporal* blob.
 * Canonical names per meos_catalog.c ([T_CBUFFER]="cbuffer", [T_TCBUFFER]="tcbuffer").
 * This binding registers only the type + text I/O boundary; the operation surface
 * (constructors, accessors, predicates, distance, …) is generated from the
 * MEOS-API catalog into src/generated/. */
struct CbufferTypes {
    static LogicalType cbuffer();
    static LogicalType tcbuffer();

    static void RegisterTypes(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
};

struct CbufferFunctions {
    /* In/out — static cbuffer value */
    static bool Cbuffer_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Cbuffer_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* In/out — tcbuffer temporal value */
    static bool Tcbuffer_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tcbuffer_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
};

} // namespace duckdb
