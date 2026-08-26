#pragma once

#include "meos_wrapper_simple.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

namespace duckdb {

/* pose is a rigid-body placement (a position and an orientation); a varlena
 * value surfaced as a BLOB.  tpose is its temporal counterpart, stored as a
 * Temporal* blob.  Canonical names per meos_catalog.c ([T_POSE]="pose",
 * [T_TPOSE]="tpose").  This binding registers only the type + text I/O
 * boundary; the operation surface (constructors, accessors, predicates,
 * distance, …) is generated from the MEOS-API catalog into src/generated/. */
struct PoseTypes {
    static LogicalType pose();
    static LogicalType tpose();

    static void RegisterTypes(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
};

struct PoseFunctions {
    /* In/out — static pose value */
    static bool Pose_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Pose_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* In/out — tpose temporal value */
    static bool Tpose_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tpose_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
};

} // namespace duckdb
