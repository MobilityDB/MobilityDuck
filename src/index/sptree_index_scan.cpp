#include "duckdb/catalog/catalog_entry/duck_table_entry.hpp"
#include "duckdb/catalog/dependency_list.hpp"
#include "duckdb/common/mutex.hpp"
#include "duckdb/function/function_set.hpp"
#include "duckdb/optimizer/matcher/expression_matcher.hpp"
#include "duckdb/planner/expression_iterator.hpp"
#include "duckdb/planner/operator/logical_get.hpp"
#include "duckdb/storage/table/scan_state.hpp"
#include "duckdb/transaction/duck_transaction.hpp"
#include "duckdb/transaction/local_storage.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/catalog/catalog_entry/duck_index_entry.hpp"
#include "duckdb/storage/data_table.hpp"

#include "index/sptree_module.hpp"
#include "index/sptree_index_scan.hpp"

namespace duckdb {

BindInfo TSPTreeIndexScanBindInfo(const optional_ptr<FunctionData> bind_data_p) {
	auto &bind_data = bind_data_p->Cast<TSPTreeIndexScanBindData>();
	return BindInfo(bind_data.table);
}

//-------------------------------------------------------------------------
// Global State
//-------------------------------------------------------------------------
struct SPTreeIndexScanGlobalState : public GlobalTableFunctionState {
	DataChunk all_columns;
	vector<idx_t> projection_ids;
	ColumnFetchState fetch_state;
	TableScanState local_storage_state;
	vector<StorageIndex> column_ids;

	unique_ptr<IndexScanState> index_state;
	Vector row_ids = Vector(LogicalType::ROW_TYPE);
};

static unique_ptr<GlobalTableFunctionState> SPTreeIndexScanInitGlobal(ClientContext &context,
                                                                    TableFunctionInitInput &input) {												
	auto &bind_data = input.bind_data->Cast<TSPTreeIndexScanBindData>();

	auto result = make_uniq<SPTreeIndexScanGlobalState>();

	
	auto &local_storage = LocalStorage::Get(context, bind_data.table.catalog);
	result->column_ids.reserve(input.column_ids.size());

	for (auto &id : input.column_ids) {
		storage_t col_id = id;
		if (id != DConstants::INVALID_INDEX) {
			col_id = bind_data.table.GetColumn(LogicalIndex(id)).StorageOid();
		}
		result->column_ids.emplace_back(col_id);
	}

	// The scan declares projection pushdown, so when the plan reads a column it
	// does not return — the indexed column under `WHERE g && ...` while the query
	// selects another — the fetch covers every scanned column and the output holds
	// only the projected ones. Fetch into a chunk of the scanned types and let the
	// output reference the projected subset, as the built-in table scan does; the
	// execute path below already follows that shape once projection_ids is set.
	if (input.CanRemoveFilterColumns()) {
		result->projection_ids = input.projection_ids;
		const auto &columns = bind_data.table.GetColumns();
		vector<LogicalType> scanned_types;
		for (const auto &col_idx : input.column_indexes) {
			if (col_idx.IsRowIdColumn()) {
				// Constructed by value: binding the constexpr member by reference
				// pulls a second definition of it into the extension archive.
				scanned_types.push_back(LogicalType(LogicalType::ROW_TYPE));
			} else {
				scanned_types.push_back(columns.GetColumn(col_idx.ToLogical()).Type());
			}
		}
		result->all_columns.Initialize(context, scanned_types);
	}

	// Initialize the storage scan state
	result->local_storage_state.Initialize(result->column_ids, context, input.filters);
	local_storage.InitializeScan(bind_data.table.GetStorage(), result->local_storage_state.local_state, input.filters);

	// Initialize index scan - works for both stbox and tstzspan
	if (bind_data.query_box) {
        result->index_state = bind_data.index.Cast<TSPTreeIndex>().InitializeScan(
            bind_data.query_box.get(), 
            bind_data.query_box_size,
			bind_data.operation
        );
    }
    
	return std::move(result);
}

//-------------------------------------------------------------------------
// Execute
//-------------------------------------------------------------------------
static void SPTreeIndexScanExecute(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {

	auto &bind_data = data_p.bind_data->Cast<TSPTreeIndexScanBindData>();

	auto &state = data_p.global_state->Cast<SPTreeIndexScanGlobalState>();

	auto &transaction = DuckTransaction::Get(context, bind_data.table.catalog);

	auto row_count = bind_data.index.Cast<TSPTreeIndex>().Scan(*state.index_state, state.row_ids);

	if (row_count == 0) {
		output.SetCardinality(0);
		return;
	}
	if (state.projection_ids.empty()) {
		bind_data.table.GetStorage().Fetch(transaction, output, state.column_ids, state.row_ids, row_count,
		                                   state.fetch_state);
		return;
	}

	state.all_columns.Reset();

	bind_data.table.GetStorage().Fetch(transaction, state.all_columns, state.column_ids, state.row_ids, row_count,
	                                   state.fetch_state);

	output.ReferenceColumns(state.all_columns, state.projection_ids);

}


//-------------------------------------------------------------------------
// Get Function
//-------------------------------------------------------------------------
TableFunction TSPTreeIndexScanFunction::GetFunction() {
	TableFunction func("mobility sptree index", {}, SPTreeIndexScanExecute);
	func.init_global = SPTreeIndexScanInitGlobal;
    
    func.get_bind_info = TSPTreeIndexScanBindInfo;
    
    func.projection_pushdown = true;
    func.filter_pushdown = false; 
	return func;
}

// -------------------------------------------------------------------------
// Register
// -------------------------------------------------------------------------
void TSPTreeModule::RegisterIndexScan(ExtensionLoader &loader) {
	loader.RegisterFunction( TSPTreeIndexScanFunction::GetFunction());
}

} 
