#pragma once

#include <sys/stat.h>

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
		// The timezone init reads the IANA database, which minimal images
		// (Alpine/musl, edge devices) do not ship; without it MEOS's pgtz code
		// fails on opendir, so the thread keeps the default zone instead.
		struct stat tz_st {};
		if (stat("/usr/share/zoneinfo", &tz_st) == 0 && (tz_st.st_mode & S_IFDIR)) {
			meos_initialize_timezone("Europe/Brussels");
		}
		meos_initialize_collation();
		return true;
	}();
	(void) meos_thread_ready;
}

} // namespace duckdb
