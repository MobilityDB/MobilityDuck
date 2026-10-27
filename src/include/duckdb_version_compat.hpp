#pragma once

// The DuckDB line being built decides the entry points this extension calls.
//
// GEOMETRY is a core DuckDB LogicalTypeId from v1.5.0
// (duckdb/common/types/geometry.hpp records "Added to core in DuckDB v1.5.0"),
// with its WKB conversion `Geometry::ToBinary`; before v1.5 it is a type of the
// spatial extension, which MobilityDuck does not build, so v1.5 is the oldest
// line MobilityDuck builds against. The boundary that hands a geometry to MEOS
// is written once here rather than at each call site.
//
// MOBILITYDUCK_DUCKDB_MAJOR / _MINOR come from CMakeLists.txt, which reads the
// DUCKDB_MAJOR_VERSION / DUCKDB_MINOR_VERSION variables DuckDB's own build sets.

#include "duckdb/common/types.hpp"
#include "duckdb_version_compat.hpp"
#include "duckdb/common/vector.hpp"
#include "duckdb/common/types/string_type.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/optimizer/optimizer_extension.hpp"
#include "duckdb/storage/table/table_index_list.hpp"

#if !defined(MOBILITYDUCK_DUCKDB_MAJOR) || !defined(MOBILITYDUCK_DUCKDB_MINOR)
#error "MOBILITYDUCK_DUCKDB_MAJOR/_MINOR are set by CMakeLists.txt; configure through it."
#endif

#define MOBILITYDUCK_DUCKDB_AT_LEAST(major, minor)                                                                     \
	(MOBILITYDUCK_DUCKDB_MAJOR > (major) ||                                                                            \
	 (MOBILITYDUCK_DUCKDB_MAJOR == (major) && MOBILITYDUCK_DUCKDB_MINOR >= (minor)))

#if !MOBILITYDUCK_DUCKDB_AT_LEAST(1, 5)
#error "MobilityDuck builds against DuckDB v1.5 or later, where GEOMETRY is a core type."
#endif

#include "duckdb/common/types/geometry.hpp"

namespace duckdb {

//! The GEOMETRY logical type of DuckDB core.
inline LogicalType MobilityDuckGeometryType() {
	return LogicalType::GEOMETRY();
}

//! Whether a value of this type is a geometry: core states GEOMETRY as a
//! LogicalTypeId of its own, and a BLOB holding WKB is read as one too.
inline bool MobilityDuckIsGeometryType(const LogicalType &type) {
	return type.id() == LogicalTypeId::GEOMETRY || type.id() == LogicalTypeId::BLOB;
}

//! The WKB of one geometry value, as the byte buffer MEOS reads.
inline void MobilityDuckGeometryToWKB(const string_t &geometry, vector<data_t> &buffer) {
	Vector source(LogicalType::GEOMETRY());
	source.SetVectorType(VectorType::CONSTANT_VECTOR);
	ConstantVector::GetData<string_t>(source)[0] = geometry;

	Vector target(LogicalType::BLOB);
	Geometry::ToBinary(source, target, 1);

	const auto wkb = ConstantVector::GetData<string_t>(target)[0];
	const auto data = const_data_ptr_cast(wkb.GetData());
	buffer.assign(data, data + wkb.GetSize());
}


//! Every index of a table, stopping when the callback answers true.
//!
//! TableIndexList::Scan takes this callback on the v1.4 line; from v1.5 the list
//! is walked as a range through Indexes(), which duckdb-spatial's own rtree does
//! (see duckdb-spatial/src/spatial/index/rtree/rtree_index_pragmas.cpp).
template <class FN>
inline void MobilityDuckScanIndexes(TableIndexList &indexes, FN &&callback) {
#if MOBILITYDUCK_DUCKDB_AT_LEAST(1, 5)
	for (auto &index : indexes.Indexes()) {
		if (callback(index)) {
			break;
		}
	}
#else
	indexes.Scan(std::forward<FN>(callback));
#endif
}

//! Registers one optimizer extension with the database.
//!
//! The v1.4 line appends to DBConfig::optimizer_extensions; from v1.5 the entry
//! point is the static OptimizerExtension::Register, which is what
//! duckdb-spatial's SpatialJoinOptimizer::Register calls.
inline void MobilityDuckRegisterOptimizer(DBConfig &config, OptimizerExtension extension) {
#if MOBILITYDUCK_DUCKDB_AT_LEAST(1, 5)
	OptimizerExtension::Register(config, std::move(extension));
#else
	config.optimizer_extensions.push_back(std::move(extension));
#endif
}

} // namespace duckdb
