#pragma once

extern "C" {
#include <meos.h>
}

// Defined in mobilityduck_extension.cpp. Converts MEOS errors into DuckDB
// exceptions instead of the process-exiting default handler.
extern "C" void MobilityduckMeosErrorHandler(int errlevel, int errcode, const char *errmsg);

namespace duckdb {

// MEOS keeps the session timezone, errno, PROJ context and the RNGs in
// thread-local storage; each thread that calls MEOS must initialise it
// before its first call (see meos.h, "Multithreading"). DuckDB runs
// scalar, cast and aggregate bodies on TaskScheduler worker threads, so a
// one-shot init on the load thread leaves workers with a NULL
// session_timezone and pg_next_dst_boundary segfaults on the first
// timestamp parse. This runs the per-thread init exactly once per thread.
//
// meos_initialize() resets the process-global error handler to the
// exit-on-error default, so MobilityduckMeosErrorHandler is re-installed
// here; the store is an idempotent atomic write of the same pointer.
inline void EnsureMeosThreadInitialized() {
	static thread_local const bool meos_thread_ready = []() {
		meos_initialize();
		meos_initialize_error_handler(&MobilityduckMeosErrorHandler);
		meos_initialize_timezone("Europe/Brussels");
		return true;
	}();
	(void) meos_thread_ready;
}

} // namespace duckdb
