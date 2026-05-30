#pragma once

#include <setjmp.h>
#include <string>
#include <utility>

#include "duckdb/common/exception.hpp"

namespace duckdb {

// MEOS reports errors through a C callback installed with
// meos_initialize_error_handler(). Throwing a C++ exception directly out of
// that callback unwinds *through* MEOS's C stack frames, which is undefined
// behaviour: MEOS's thread-local error buffer, the lwgeom WKT parser and the
// GEOS context are left half-updated, so the next MEOS call SIGSEGVs
// non-deterministically. Instead the handler siglongjmp()s back to the
// nearest guarded boundary and the DuckDB exception is thrown there, from
// pure C++ frames only.
//
// Defined in mobilityduck_extension.cpp (the translation unit that owns the
// MEOS error handler).
extern thread_local sigjmp_buf MeosJmpBuf;
extern thread_local bool       MeosGuardActive;
extern thread_local std::string MeosErrMsg;

// Run body() with a MEOS longjmp landing pad installed. If MEOS raises an
// error (level >= ERROR) anywhere inside body(), control returns here and a
// DuckDB InvalidInputException is thrown from C++ — no exception ever unwinds
// through MEOS C frames. Non-reentrant by construction: the guarded boundary
// is the outermost point of a single scalar/cast execution; DuckDB does not
// nest one registered function's executor inside another's. MeosGuardActive
// and MeosErrMsg have thread/static storage, so they are exempt from the
// setjmp local-clobber rule; this is the standard safe setjmp-wrapper shape
// (sigsetjmp in a function that then invokes a callback).
template <class Body>
inline void MeosGuardedRun(Body &&body) {
	MeosGuardActive = true;
	if (sigsetjmp(MeosJmpBuf, 0) != 0) {
		MeosGuardActive = false;
		throw InvalidInputException(MeosErrMsg);
	}
	body();
	MeosGuardActive = false;
}

} // namespace duckdb
