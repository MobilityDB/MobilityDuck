#pragma once

#include <mutex>

#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

/**
 * Mutex serialization for MobilityDuck scalar function bodies that ultimately call
 * MEOS / liblwgeom. MEOS uses GEOS's legacy global API (`initGEOS` / `finishGEOS`)
 * internally from worker threads; concurrent legacy GEOS init/teardown from DuckDB's
 * parallelism triggers intermittent GEOS errors and allocator corruption when spatial
 * and MobilityDuck coexist.
 *
 * Wrapping each MEOS-backed ScalarFunction execution prevents overlapping MEOS/GEOS
 * legacy pairs across concurrent pipelines while preserving parallelism elsewhere.
 */
inline std::mutex &MeosSerializedExecMutex() {
	static std::mutex mutex;
	return mutex;
}

inline ScalarFunction WrapScalarFunctionWithMeosExecMutex(ScalarFunction sf) {
	scalar_function_t orig = std::move(sf.function);
	sf.function = [orig = std::move(orig)](DataChunk &args, ExpressionState &state, Vector &result) {
		std::lock_guard<std::mutex> guard(MeosSerializedExecMutex());
		orig(args, state, result);
	};
	return sf;
}

inline void RegisterSerializedScalarFunction(ExtensionLoader &loader, ScalarFunction sf) {
	loader.RegisterFunction(WrapScalarFunctionWithMeosExecMutex(std::move(sf)));
}

} // namespace duckdb
