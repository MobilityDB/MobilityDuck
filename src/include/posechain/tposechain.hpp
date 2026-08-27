#pragma once

#include "meos_wrapper_simple.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

namespace duckdb {

/* posechain is a directed chain of rigid-body poses — one topocentric frame at
 * the outside and a link per joint — carried as a varlena.  tposechain is its
 * temporal counterpart, stored as a Temporal* blob.  Canonical names per
 * meos_catalog.c ([T_POSECHAIN]="posechain", [T_TPOSECHAIN]="tposechain"), and
 * the temporal type's base value is `posechain` ([T_TPOSECHAIN].
 * temptype_basetype = T_POSECHAIN).
 *
 * The value is a fixed-size varlena (vl_len_ · flags · srid · count · count ×
 * doubles), so it rides the generic temporal operations the way tpose does —
 * unlike trgeometry, which appends a reference geometry and therefore owns a
 * `trgeometry_*` counterpart for each of them.
 *
 * This binding registers only the type + text I/O boundary; the operation
 * surface is generated from the MEOS-API catalog into src/generated/. */
struct PosechainTypes {
    static LogicalType posechain();
    static LogicalType tposechain();

    static void RegisterTypes(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
};

struct PosechainFunctions {
    /* In/out — static posechain value */
    static bool Posechain_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Posechain_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* In/out — tposechain temporal value */
    static bool Tposechain_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tposechain_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
};

} // namespace duckdb
