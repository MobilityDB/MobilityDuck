/*****************************************************************************
 *
 * This MobilityDB code is provided under The PostgreSQL License.
 * Copyright (c) 2016-2026, Université libre de Bruxelles and MobilityDB
 * contributors
 *
 * MobilityDB includes portions of PostGIS version 3 source code released
 * under the GNU General Public License (GPLv2 or later).
 * Copyright (c) 2001-2026, PostGIS contributors
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without a written
 * agreement is hereby granted, provided that the above copyright notice and
 * this paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL UNIVERSITE LIBRE DE BRUXELLES BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
 * LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION,
 * EVEN IF UNIVERSITE LIBRE DE BRUXELLES HAS BEEN ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * UNIVERSITE LIBRE DE BRUXELLES SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON
 * AN "AS IS" BASIS, AND UNIVERSITE LIBRE DE BRUXELLES HAS NO OBLIGATIONS TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 *****************************************************************************/

#pragma once

// MobilityDuck builds against DuckDB v1.5.6, the version its CI builds and tests.
//
// GEOMETRY is a core DuckDB LogicalTypeId (duckdb/common/types/geometry.hpp),
// with its WKB conversion `Geometry::ToBinary`; a DuckDB older than v1.5 has no
// such type and is refused below. The boundary that hands a geometry to MEOS is
// written once here rather than at each call site.
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
//! The list is walked as a range through Indexes(), which duckdb-spatial's own rtree does
//! (see duckdb-spatial/src/spatial/index/rtree/rtree_index_pragmas.cpp).
template <class FN>
inline void MobilityDuckScanIndexes(TableIndexList &indexes, FN &&callback) {
	for (auto &index : indexes.Indexes()) {
		if (callback(index)) {
			break;
		}
	}
}

//! Registers one optimizer extension with the database.
//!
//! The entry point is the static OptimizerExtension::Register, which is what
//! duckdb-spatial's SpatialJoinOptimizer::Register calls.
inline void MobilityDuckRegisterOptimizer(DBConfig &config, OptimizerExtension extension) {
	OptimizerExtension::Register(config, std::move(extension));
}

} // namespace duckdb
