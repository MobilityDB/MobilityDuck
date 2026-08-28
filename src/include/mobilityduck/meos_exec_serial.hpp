#pragma once

#include <mutex>

#include "duckdb/function/cast/default_casts.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include "mobilityduck/meos_thread.hpp"

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
		// DuckDB runs scalar/cast/aggregate bodies on worker threads whose MEOS
		// thread-local state (timezone/locale/PROJ/RNG) is uninitialised; init once
		// per thread before any MEOS call (see meos_thread.hpp).
		EnsureMeosThreadInitialized();
		orig(args, state, result);
	};
	return sf;
}

inline void RegisterSerializedScalarFunction(ExtensionLoader &loader, ScalarFunction sf) {
	loader.RegisterFunction(WrapScalarFunctionWithMeosExecMutex(std::move(sf)));
}

/**
 * Register one scalar under the canonical MobilityDB name it is built with, and
 * under any additional spelling this extension already publishes. The canonical
 * name is the one the MobilityDB manual and every other binding use, so a query
 * written against them runs here; an extra spelling stays valid beside it.
 */
inline void RegisterSerializedScalarFunctionAs(ExtensionLoader &loader, ScalarFunction sf,
                                               const vector<string> &also) {
	for (const auto &name : also) {
		ScalarFunction alias = sf;
		alias.name = name;
		RegisterSerializedScalarFunction(loader, std::move(alias));
	}
	RegisterSerializedScalarFunction(loader, std::move(sf));
}

/**
 * Cast functions are a separate registration path from scalar functions and
 * have no shared execution wrapper, yet they call MEOS just the same (e.g. the
 * VARCHAR -> tgeompoint parse). The original function pointer is stashed in
 * the bound cast data and reached through a trampoline that runs the
 * per-thread MEOS init before delegating. MobilityDuck cast functions do not
 * use cast_data themselves, so forwarding it untouched is safe.
 */
struct MeosCastData : BoundCastData {
	explicit MeosCastData(cast_function_t orig_p) : orig(orig_p) {
	}
	cast_function_t orig;
	unique_ptr<BoundCastData> Copy() const override {
		return make_uniq<MeosCastData>(orig);
	}
};

inline bool MeosCastTrampoline(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
	EnsureMeosThreadInitialized();
	auto &data = parameters.cast_data->Cast<MeosCastData>();
	return data.orig(source, result, count, parameters);
}

inline void RegisterMeosCastFunction(ExtensionLoader &loader, const LogicalType &source, const LogicalType &target,
                                     cast_function_t function, int64_t implicit_cast_cost = -1) {
	loader.RegisterCastFunction(source, target, BoundCastInfo(MeosCastTrampoline, make_uniq<MeosCastData>(function)),
	                            implicit_cast_cost);
}

} // namespace duckdb
