#pragma once

#include "duckdb/execution/physical_operator.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/planner/operator/logical_extension_operator.hpp"
#include "duckdb/catalog/catalog_entry/duck_table_entry.hpp"

#include "index/rtree_module.hpp"

namespace duckdb {

//! A join whose bounding-box condition is answered by probing a TRTREE index
//! once per row of the other side, rather than comparing every pair.
//!
//! The indexed side is NOT a child: it is reached through the index and a row
//! fetch, so this carries a single child — the side supplying probe rows.
struct LogicalRTreeIndexJoin : public LogicalExtensionOperator {
public:
	LogicalRTreeIndexJoin(DuckTableEntry &index_table_p, TRTreeIndex &index_p, idx_t probe_column_p,
	                      bool probe_is_temporal_p, vector<LogicalType> index_types_p,
	                      vector<idx_t> index_columns_p, vector<ColumnBinding> index_bindings_p)
	    : index_table(index_table_p), index(index_p), probe_column(probe_column_p),
	      probe_is_temporal(probe_is_temporal_p), index_types(std::move(index_types_p)),
	      index_columns(std::move(index_columns_p)), index_bindings(std::move(index_bindings_p)) {
	}

	//! The table the index belongs to; its rows are fetched by row id.
	DuckTableEntry &index_table;
	//! The index probed once per incoming row.
	TRTreeIndex &index;
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
		return "mobilityduck_rtree_index_join";
	}
};

//! Emits, for each incoming row, that row joined with every row of the indexed
//! table whose bounding box satisfies the operation.
class PhysicalRTreeIndexJoin : public PhysicalOperator {
public:
	static constexpr const PhysicalOperatorType TYPE = PhysicalOperatorType::EXTENSION;

	PhysicalRTreeIndexJoin(PhysicalPlan &physical_plan, vector<LogicalType> types, DuckTableEntry &index_table,
	                       TRTreeIndex &index, idx_t probe_column, bool probe_is_temporal,
	                       vector<idx_t> index_columns, idx_t estimated_cardinality);

	DuckTableEntry &index_table;
	TRTreeIndex &index;
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
		return "MOBILITY RTREE INDEX JOIN";
	}
};

} // namespace duckdb
