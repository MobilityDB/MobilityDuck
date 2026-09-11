#pragma once

extern "C" {
#include <meos.h>
}

namespace duckdb {

// MEOS keeps the session timezone, the collation cache, errno, the PROJ and
// GEOS contexts and the RNGs in thread-local storage, so a thread that calls
// MEOS initialises its own caches before its first call (see meos.h,
// "Multithreading"). DuckDB runs scalar, cast and aggregate bodies on
// TaskScheduler worker threads, so a one-shot init on the load thread leaves
// workers with a NULL session_timezone and pg_next_dst_boundary segfaults on
// the first timestamp parse.
//
// Only the thread-local caches belong here. meos_initialize() is process-wide
// setup — the allocator and the error handler — and LoadInternal runs it once
// under a std::call_once. Repeating it per thread would reset the
// process-global error handler to the exit-on-error default, so an error
// raised on any other thread during that window would print a bare message and
// end the process rather than reach the handler that turns it into a DuckDB
// exception. The PROJ, GEOS and GSL contexts are thread-local statics inside
// MEOS that it creates lazily on first use, so a caller neither can nor needs
// to initialise them.
inline void EnsureMeosThreadInitialized() {
	static thread_local const bool meos_thread_ready = []() {
		// MEOS carries its own time zone database, so the zone is the same on
		// every host, whether or not it has a zone directory.
		meos_initialize_timezone("Europe/Brussels");
		meos_initialize_collation();
		return true;
	}();
	(void) meos_thread_ready;
}

} // namespace duckdb
