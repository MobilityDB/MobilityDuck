#pragma once

// =====================================================================
// MobilityDuck binding helpers
// ---------------------------------------------------------------------
// Utilities for writing DuckDB scalar bindings around MEOS accessors
// that return a `Datum`. The common pattern in MobilityDuck is:
//
//   * a BLOB-backed aliased temporal/set type comes in as a `string_t`,
//   * we copy it into owned memory, cast it to the MEOS struct, call a
//     MEOS accessor returning `Datum`, and write the result out.
//
// The existing bindings hard-wired `UnaryExecutor::Execute<string_t,
// int64_t>` for every variant — which "worked" under DuckDB 1.3's
// looser vector-type checks even when the registered return type was
// BOOLEAN or DOUBLE, but is explicitly rejected by DuckDB 1.4
// ("Expected INT64, found BOOL"). The fix is to write one typed C
// function per (accessor, base-type) combo and register each overload
// against the matching input alias.
//
// The helpers here centralise that pattern so each typed registration
// is a single line instead of ~25 lines of boilerplate copied N times.
// =====================================================================

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#include "duckdb/common/exception.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/common/vector_operations/unary_executor.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/planner/expression.hpp"

#include "mobilityduck/meos_exec_serial.hpp"

extern "C" {
#include <meos.h>
}

namespace mobilityduck {

// ---------------------------------------------------------------------
// Datum → C++ primitive conversion
// ---------------------------------------------------------------------
// MEOS's `Datum` is a `uintptr_t` (Postgres convention). How the bits
// encode a given value depends on the value's type; always use the
// right accessor per primitive.
//
//   * bool    — low byte holds 0/1.
//   * int32   — low 32 bits, zero-extended.
//   * int64   — identity cast.
//   * double  — raw IEEE-754 bits stored in the Datum; needs memcpy
//               rather than a numeric cast.
// ---------------------------------------------------------------------

template <typename T>
inline T DatumTo(uintptr_t d);

template <>
inline bool DatumTo<bool>(uintptr_t d) {
	return static_cast<bool>(d & 0xff);
}

template <>
inline int32_t DatumTo<int32_t>(uintptr_t d) {
	return static_cast<int32_t>(d & 0xffffffffu);
}

template <>
inline int64_t DatumTo<int64_t>(uintptr_t d) {
	return static_cast<int64_t>(d);
}

template <>
inline double DatumTo<double>(uintptr_t d) {
#if UINTPTR_MAX >= 0xFFFFFFFFFFFFFFFFull
	// 64-bit platforms (Linux amd64/arm64, macOS amd64/arm64, Windows amd64):
	// `Datum` is a 64-bit integer carrying the double's raw IEEE-754 bits
	// (Postgres's USE_FLOAT8_BYVAL convention).
	double v;
	std::memcpy(&v, &d, sizeof(double));
	return v;
#else
	// 32-bit platforms (notably wasm32-emscripten): `Datum` is a pointer to
	// a double stored elsewhere — matching Postgres's !USE_FLOAT8_BYVAL
	// convention. Lifetime is the caller's responsibility (MEOS currently
	// points at storage owned by the Temporal or palloc'd context; copy
	// before freeing the source).
	return *reinterpret_cast<const double *>(d);
#endif
}

// ---------------------------------------------------------------------
// MakeTemporalDatumAccessor
// ---------------------------------------------------------------------
// Build a `scalar_function_t` for unary MEOS accessors of the shape
//   Datum meos_fn(const Temporal *temp);
// (e.g. `temporal_start_value`, `temporal_end_value`, `temporal_min_value`,
// `temporal_max_value`, `tinstant_value`). The generated function:
//
//   * reads the incoming value as a `string_t` backed by the BLOB
//     bytes of the serialized Temporal,
//   * copies them into heap-owned memory so MEOS may mutate if it
//     wants (it does not today, but the contract allows it),
//   * calls `meos_fn`, converts the returned Datum to `CppResult`,
//   * frees the copy and returns to the executor.
//
// `CppResult` must match the DuckDB LogicalType registered for this
// overload (bool ↔ BOOLEAN, int32_t ↔ INTEGER, int64_t ↔ BIGINT,
// double ↔ DOUBLE).
// ---------------------------------------------------------------------

template <typename CppResult>
inline duckdb::scalar_function_t
MakeTemporalDatumAccessor(uintptr_t (*meos_fn)(const Temporal *),
                          const char *err_label) {
	return [meos_fn, err_label](duckdb::DataChunk &args,
	                            duckdb::ExpressionState &,
	                            duckdb::Vector &result) {
		duckdb::UnaryExecutor::Execute<duckdb::string_t, CppResult>(
		    args.data[0], result, args.size(),
		    [&](duckdb::string_t input) -> CppResult {
			    const uint8_t *data = reinterpret_cast<const uint8_t *>(input.GetData());
			    size_t data_size = input.GetSize();
			    if (data_size < sizeof(void *)) {
				    throw duckdb::InvalidInputException("[%s] invalid Temporal data: insufficient size", err_label);
			    }
			    uint8_t *data_copy = static_cast<uint8_t *>(std::malloc(data_size));
			    if (!data_copy) {
				    throw duckdb::InternalException("[%s] allocation failure", err_label);
			    }
			    std::memcpy(data_copy, data, data_size);
			    Temporal *temp = reinterpret_cast<Temporal *>(data_copy);
			    uintptr_t ret = meos_fn(temp);
			    // On 32-bit platforms (wasm32) Datum may be a pointer into
			    // `data_copy`; copy the value out before freeing the buffer.
			    CppResult result_value = DatumTo<CppResult>(ret);
			    std::free(data_copy);
			    return result_value;
		    });
		if (args.size() == 1) {
			result.SetVectorType(duckdb::VectorType::CONSTANT_VECTOR);
		}
	};
}

// ---------------------------------------------------------------------
// RegisterTemporalDatumAccessor
// ---------------------------------------------------------------------
// Convenience: register one typed scalar-function overload on `loader`
// by name + (input temporal type, output LogicalType), wiring in the
// generated executor built from `meos_fn`.
//
// Example (tbool):
//   RegisterTemporalDatumAccessor<bool>(
//       loader, "startValue", TemporalTypes::tbool(),
//       duckdb::LogicalType::BOOLEAN, temporal_start_value);
// ---------------------------------------------------------------------

template <typename CppResult>
inline void RegisterTemporalDatumAccessor(duckdb::ExtensionLoader &loader,
                                          const std::string &name,
                                          const duckdb::LogicalType &input_type,
                                          const duckdb::LogicalType &return_type,
                                          uintptr_t (*meos_fn)(const Temporal *)) {
	duckdb::RegisterSerializedScalarFunction(
	    loader,
	    duckdb::ScalarFunction(
	        name,
	        {input_type},
	        return_type,
	        MakeTemporalDatumAccessor<CppResult>(meos_fn, name.c_str())));
}

} // namespace mobilityduck
