#pragma once

#include <string>

#include "duckdb/common/exception.hpp"
#include "duckdb/main/client_context.hpp"

extern "C" {
#include <meos.h>
}

namespace duckdb {

// The zone a thread's MEOS starts in, and the zone the extension gives a new
// session: a non-UTC zone surfaces the off-by-an-hour errors UTC hides. MEOS
// carries its own time zone database, so the zone is the same on every host,
// whether or not it has a zone directory.
static constexpr const char *MEOS_DEFAULT_TIMEZONE = "Europe/Brussels";

// The zone this thread's MEOS reads and writes timestamps in; empty while MEOS
// holds no zone, after a zone it does not know was asked of it.
inline std::string &MeosThreadTimezone() {
	static thread_local std::string zone = MEOS_DEFAULT_TIMEZONE;
	return zone;
}

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
		meos_initialize_timezone(MEOS_DEFAULT_TIMEZONE);
		meos_initialize_collation();
		return true;
	}();
	(void) meos_thread_ready;
}

// The session's TimeZone setting, or an empty string when the session states
// none (the setting is ICU's, so it is absent when ICU is not loaded).
inline std::string SessionTimezone(ClientContext &context) {
	Value zone;
	if (!context.TryGetCurrentSetting("TimeZone", zone) || zone.IsNull()) {
		return std::string();
	}
	return zone.ToString();
}

// MEOS parses and prints timestamps, and turns them into dates, in the zone of
// the DuckDB session that runs the query, the way MobilityDB follows the
// PostgreSQL session TimeZone. MEOS keeps its zone per thread, so each thread
// moves to the session's zone on its own first MEOS call after a
// `SET TimeZone`. A zone MEOS does not know is an error naming it.
inline void FollowSessionTimezone(const std::string &zone) {
	EnsureMeosThreadInitialized();
	auto &current = MeosThreadTimezone();
	if (zone.empty() || zone == current) {
		return;
	}
	// meos_initialize_timezone releases the zone it holds before it loads the
	// next, so a failed switch leaves MEOS without one and none is recorded
	current.clear();
	try {
		meos_initialize_timezone(zone.c_str());
	} catch (std::exception &) {
		throw InvalidInputException("MEOS does not know the time zone \"%s\" that the session sets", zone);
	}
	current = zone;
}

inline void EnsureMeosThreadInitialized(ClientContext &context) {
	FollowSessionTimezone(SessionTimezone(context));
}

} // namespace duckdb
