#pragma once

#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

extern "C" {
void meos_initialize(void);
void meos_initialize_error_handler(void (*)(int, int, const char *));
}

namespace duckdb {

// Defined in mobilityduck_extension.cpp — converts MEOS errors to DuckDB exceptions.
extern "C" void MobilityduckMeosErrorHandler(int errlevel, int errcode, const char *errmsg);

/**
 * Ensure MEOS is initialised on the calling thread.
 *
 * MEOS (post-PR #815) stores all mutable state in thread-local variables
 * (PJ_CONTEXT, GSL RNGs, error code, …).  Each DuckDB worker thread must
 * call meos_initialize() before invoking any MEOS function; failing to do so
 * leaves TLS pointers NULL and causes segfaults under DuckDB's parallel
 * execution.
 *
 * The error handler is process-global (written with __ATOMIC_RELEASE) and
 * safe to install from any thread; installing it here ensures every thread
 * that initialises MEOS also registers the exception-throwing handler.
 *
 * Cost: one thread-local bool check per scalar-function call — negligible.
 */
inline void EnsureMeosInitializedOnThread() {
	thread_local bool initialized = false;
	if (!initialized) {
		meos_initialize();
		meos_initialize_error_handler(&MobilityduckMeosErrorHandler);
		initialized = true;
	}
}

inline ScalarFunction WrapScalarFunctionWithMeosInit(ScalarFunction sf) {
	scalar_function_t orig = std::move(sf.function);
	sf.function = [orig = std::move(orig)](DataChunk &args, ExpressionState &state, Vector &result) {
		EnsureMeosInitializedOnThread();
		orig(args, state, result);
	};
	return sf;
}

// API name kept for compatibility with the ~1900 call sites throughout the codebase.
// The implementation no longer serialises via a mutex — it ensures per-thread MEOS
// initialisation instead, which is the correct fix for DuckDB parallel execution.
inline void RegisterSerializedScalarFunction(ExtensionLoader &loader, ScalarFunction sf) {
	loader.RegisterFunction(WrapScalarFunctionWithMeosInit(std::move(sf)));
}

} // namespace duckdb
