#pragma once
#include "duckdb/execution/physical_operator.hpp"
#include "duckdb_version_compat.hpp"
#include "duckdb/storage/data_table.hpp"

namespace duckdb {

class DuckTableEntry;

class PhysicalCreateTRTreeIndex final : public PhysicalOperator {
public:
	static constexpr const PhysicalOperatorType TYPE = PhysicalOperatorType::EXTENSION;

public:
	PhysicalCreateTRTreeIndex(PhysicalPlan &physical_plan, const vector<LogicalType> &types_p,
	                        TableCatalogEntry &table, const vector<column_t> &column_ids,
	                        unique_ptr<CreateIndexInfo> info,
	                        vector<unique_ptr<Expression>> unbound_expressions, idx_t estimated_cardinality);

	DuckTableEntry &table;
	vector<column_t> storage_ids;
	unique_ptr<CreateIndexInfo> info;
	vector<unique_ptr<Expression>> unbound_expressions;
	const bool sorted;

public:
#if MOBILITYDUCK_DUCKDB_AT_LEAST(1, 5)
	// PhysicalOperator::GetData is a non-virtual wrapper from v1.5; the virtual
	// an operator implements is GetDataInternal.
	SourceResultType GetDataInternal(ExecutionContext &context, DataChunk &chunk,
	                                 OperatorSourceInput &input) const override {
		return SourceResultType::FINISHED;
	}
#else
	SourceResultType GetData(ExecutionContext &context, DataChunk &chunk, OperatorSourceInput &input) const override {
		return SourceResultType::FINISHED;
	}
#endif
	bool IsSource() const override {
		return true;
	}

public:
	unique_ptr<LocalSinkState> GetLocalSinkState(ExecutionContext &context) const override;
	//! Sink interface, global sink state
	unique_ptr<GlobalSinkState> GetGlobalSinkState(ClientContext &context) const override;
	SinkResultType Sink(ExecutionContext &context, DataChunk &chunk, OperatorSinkInput &input) const override;
	SinkCombineResultType Combine(ExecutionContext &context, OperatorSinkCombineInput &input) const override;
	SinkFinalizeType Finalize(Pipeline &pipeline, Event &event, ClientContext &context,
	                          OperatorSinkFinalizeInput &input) const override;

	bool IsSink() const override {
		return true;
	}
	bool ParallelSink() const override {
		return true;
	}

	ProgressData GetSinkProgress(ClientContext &context, GlobalSinkState &gstate,
	                             ProgressData source_progress) const override;
};

}
