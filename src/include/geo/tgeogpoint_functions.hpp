#pragma once

#include "meos_wrapper_simple.hpp"
#include "duckdb/common/typedefs.hpp"

#include "temporal/span.hpp"
#include "temporal/set.hpp"

#include "tydef.hpp"

namespace duckdb {

class ExtensionLoader;

struct TgeogpointFunctions {
    static bool Tpoint_in(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static void Tpointinst_constructor(DataChunk &args, ExpressionState &state, Vector &result);
    static bool StringToTgeogpoint(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool TgeogpointToString(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
};

} // namespace duckdb
