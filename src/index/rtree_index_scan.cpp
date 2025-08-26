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
#include "duckdb/main/extension_util.hpp"
#include "duckdb/catalog/catalog_entry/duck_index_entry.hpp"
#include "duckdb/storage/data_table.hpp"

#include "index/rtree_module.hpp"
#include "index/rtree_index_scan.hpp"

namespace duckdb {

BindInfo RTreeIndexScanBindInfo(const optional_ptr<FunctionData> bind_data_p) {
	fprintf(stderr,"RTreeIndexScanBindInfo start\n");
	auto &bind_data = bind_data_p->Cast<RTreeIndexScanBindData>();
	return BindInfo(bind_data.table);
}

//-------------------------------------------------------------------------
// Global State
//-------------------------------------------------------------------------
struct RTreeIndexScanGlobalState : public GlobalTableFunctionState {
	DataChunk all_columns;
	vector<idx_t> projection_ids;
	ColumnFetchState fetch_state;
	TableScanState local_storage_state;
	vector<StorageIndex> column_ids;

	unique_ptr<IndexScanState> index_state;
	Vector row_ids = Vector(LogicalType::ROW_TYPE);
};

static unique_ptr<GlobalTableFunctionState> RTreeIndexScanInitGlobal(ClientContext &context,
                                                                    TableFunctionInitInput &input) {
	fprintf(stderr,"RTreeIndexScanInitGlobal start\n");															
	auto &bind_data = input.bind_data->Cast<RTreeIndexScanBindData>();

	auto result = make_uniq<RTreeIndexScanGlobalState>();

	
	auto &local_storage = LocalStorage::Get(context, bind_data.table.catalog);
	result->column_ids.reserve(input.column_ids.size());

	// Figure out the storage column ids
	for (auto &id : input.column_ids) {
		storage_t col_id = id;
		if (id != DConstants::INVALID_INDEX) {
			col_id = bind_data.table.GetColumn(LogicalIndex(id)).StorageOid();
		}
		result->column_ids.emplace_back(col_id);
	}

	// Initialize the storage scan state
	result->local_storage_state.Initialize(result->column_ids, context, input.filters);
	local_storage.InitializeScan(bind_data.table.GetStorage(), result->local_storage_state.local_state, input.filters);

	fprintf(stderr,"InitializeScan end\n");

	// Initialize the scan state for the index
	if (bind_data.query_stbox) {
        result->index_state = bind_data.index.Cast<RTreeIndex>().InitializeScan(bind_data.query_stbox.get(), sizeof(STBox));
		fprintf(stderr,"bind_data.query_stbox InitializeScan done\n");
    }
	fprintf(stderr,"InitializeScan end\n");
	return std::move(result);
}

//-------------------------------------------------------------------------
// Execute
//-------------------------------------------------------------------------
static void RTreeIndexScanExecute(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {

	fprintf(stderr,"RTreeIndexScanExecute start\n");

	auto &bind_data = data_p.bind_data->Cast<RTreeIndexScanBindData>();
	fprintf(stderr,"RTreeIndexScanBindData done\n");

	auto &state = data_p.global_state->Cast<RTreeIndexScanGlobalState>();
	fprintf(stderr,"RTreeIndexScanGlobalState done\n");

	auto &transaction = DuckTransaction::Get(context, bind_data.table.catalog);
	fprintf(stderr," DuckTransaction::Get done\n");


	auto row_count = bind_data.index.Cast<RTreeIndex>().Scan(*state.index_state, state.row_ids);
	fprintf(stderr,"row_count Scan done\n", row_count);

	if (row_count == 0) {
		fprintf(stderr,"row_count Scan done\n, 0 row");
		// Short-circuit if the index had no more rows
		output.SetCardinality(0);
		return;
	}

	// Fetch the data from the local storage given the row ids
	if (state.projection_ids.empty()) {
		bind_data.table.GetStorage().Fetch(transaction, output, state.column_ids, state.row_ids, row_count,
		                                   state.fetch_state);
		return;
	}

	// Otherwise, we need to first fetch into our scan chunk, and then project out the result
	state.all_columns.Reset();

	// state.all_columns.Reset();
	bind_data.table.GetStorage().Fetch(transaction, state.all_columns, state.column_ids, state.row_ids, row_count,
	                                   state.fetch_state);
	fprintf(stderr,"bind_data.table.GetStorage()");

	output.ReferenceColumns(state.all_columns, state.projection_ids);
	fprintf(stderr,"output.ReferenceColumns");

}


//-------------------------------------------------------------------------
// Get Function
//-------------------------------------------------------------------------
TableFunction RTreeIndexScanFunction::GetFunction() {
	TableFunction func("mobility rtree index", {}, RTreeIndexScanExecute);
	func.init_global = RTreeIndexScanInitGlobal;
    
    // Also add bind_info if you have it
    func.get_bind_info = RTreeIndexScanBindInfo;
    
    // Set other necessary properties
    func.projection_pushdown = true;
    func.filter_pushdown = false; 
	return func;
}

// -------------------------------------------------------------------------
// Register
// -------------------------------------------------------------------------
void RTreeModule::RegisterIndexScan(DatabaseInstance &db) {
	ExtensionUtil::RegisterFunction(db, RTreeIndexScanFunction::GetFunction());
}

} // namespace duckdb