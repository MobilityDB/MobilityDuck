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

#include "duckdb/execution/physical_operator.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/planner/operator/logical_extension_operator.hpp"
#include "duckdb/catalog/catalog_entry/duck_table_entry.hpp"

#include "index/sptree_module.hpp"

namespace duckdb {

//! A join whose bounding-box condition is answered by probing a TSPTREE index
//! once per row of the other side, rather than comparing every pair.
//!
//! The indexed side is NOT a child: it is reached through the index and a row
//! fetch, so this carries a single child — the side supplying probe rows.
struct LogicalSPTreeIndexJoin : public LogicalExtensionOperator {
public:
	LogicalSPTreeIndexJoin(DuckTableEntry &index_table_p, TSPTreeIndex &index_p, idx_t probe_column_p,
	                      bool probe_is_temporal_p, vector<LogicalType> index_types_p,
	                      vector<idx_t> index_columns_p, vector<ColumnBinding> index_bindings_p)
	    : index_table(index_table_p), index(index_p), probe_column(probe_column_p),
	      probe_is_temporal(probe_is_temporal_p), index_types(std::move(index_types_p)),
	      index_columns(std::move(index_columns_p)), index_bindings(std::move(index_bindings_p)) {
	}

	//! The table the index belongs to; its rows are fetched by row id.
	DuckTableEntry &index_table;
	//! The index probed once per incoming row.
	TSPTreeIndex &index;
	//! Which column of the incoming chunk carries the query bounding box.
	idx_t probe_column;
	//! Whether that column holds a Temporal, whose box must be derived before
	//! probing, rather than a blob that already is the box.
	bool probe_is_temporal;
	//! Column types the indexed side contributes to the output.
	vector<LogicalType> index_types;
	//! Storage column ids fetched from the indexed table.
	vector<idx_t> index_columns;
	//! Bindings the indexed side contributed before the rewrite.
	vector<ColumnBinding> index_bindings;

public:
	PhysicalOperator &CreatePlan(ClientContext &context, PhysicalPlanGenerator &planner) override;
	vector<ColumnBinding> GetColumnBindings() override;
	void ResolveTypes() override;
	string GetExtensionName() const override {
		return "mobilityduck_sptree_index_join";
	}
};

//! Emits, for each incoming row, that row joined with every row of the indexed
//! table whose bounding box satisfies the operation.
class PhysicalSPTreeIndexJoin : public PhysicalOperator {
public:
	static constexpr const PhysicalOperatorType TYPE = PhysicalOperatorType::EXTENSION;

	PhysicalSPTreeIndexJoin(PhysicalPlan &physical_plan, vector<LogicalType> types, DuckTableEntry &index_table,
	                       TSPTreeIndex &index, idx_t probe_column, bool probe_is_temporal,
	                       vector<idx_t> index_columns, idx_t estimated_cardinality);

	DuckTableEntry &index_table;
	TSPTreeIndex &index;
	idx_t probe_column;
	bool probe_is_temporal;
	vector<idx_t> index_columns;
	//! How many columns the probe side contributes; the rest come from the index.
	idx_t probe_column_count;

public:
	unique_ptr<OperatorState> GetOperatorState(ExecutionContext &context) const override;
	OperatorResultType Execute(ExecutionContext &context, DataChunk &input, DataChunk &chunk,
	                           GlobalOperatorState &gstate, OperatorState &state) const override;
	bool ParallelOperator() const override {
		return false;
	}
	string GetName() const override {
		return "MOBILITY SPTREE INDEX JOIN";
	}
};

} // namespace duckdb
