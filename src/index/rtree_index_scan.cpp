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

#include <limits>
#include <queue>
#include <utility>

#include "index/rtree_module.hpp"
#include "temporal/temporal_blob.hpp"
#include "index/rtree_index_scan.hpp"

namespace duckdb {

BindInfo TRTreeIndexScanBindInfo(const optional_ptr<FunctionData> bind_data_p) {
	auto &bind_data = bind_data_p->Cast<TRTreeIndexScanBindData>();
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

	//! A nearest-neighbour scan ranks its candidates before it can emit any of them, so the rows
	//! are gathered once and then handed out in chunks.
	vector<row_t> nearest;
	idx_t nearest_position = 0;
	bool nearest_gathered = false;
};

//! The distance the query orders by, measured against the value a row holds rather than the box
//! the index stored for it. The index knows which of the two it built, and every `|=|` overload
//! over a box funnels into one of them — the temporal value carries its own type.
static double ExactDistance(MeosType bbox_type, const string_t &value, const void *query_box) {
	Temporal *temp = BlobToTemporal(value);
	if (!temp) {
		return std::numeric_limits<double>::max();
	}
	const double distance =
	    bbox_type == T_TBOX
	        ? nad_tnumber_tbox(temp, static_cast<const TBox *>(query_box))
	        : nad_tgeo_stbox(temp, static_cast<const STBox *>(query_box));
	free(temp);
	return distance;
}

//! The k nearest rows, in order. The cursor yields candidates by a bound on the distance, so a
//! candidate is ranked by recomputing the distance to the value the row holds, and reading stops
//! once the bound passes the k-th distance held — every unread candidate is at least that far.
static void GatherNearest(ClientContext &context, const TRTreeIndexScanBindData &bind_data,
                          RTreeIndexScanGlobalState &state) {
	auto &rtree_index = bind_data.index.Cast<TRTreeIndex>();
	auto &transaction = DuckTransaction::Get(context, bind_data.table.catalog);
	const idx_t k = bind_data.limit;

	// The recheck reads the INDEXED column, which the query need not select, so it fetches on its
	// own column list rather than through the plan's projection.
	const auto indexed_column = rtree_index.GetColumnIds()[0];
	vector<StorageIndex> fetch_columns;
	fetch_columns.emplace_back(
	    bind_data.table.GetColumn(LogicalIndex(indexed_column)).StorageOid());
	DataChunk value_chunk;
	value_chunk.Initialize(Allocator::Get(context),
	                       {bind_data.table.GetColumn(LogicalIndex(indexed_column)).Type()});
	Vector candidate_ids(LogicalType::ROW_TYPE);
	ColumnFetchState fetch_state;

	// Ordered worst-first, so the k-th distance held is always at the front.
	std::priority_queue<std::pair<double, row_t>> best;
	row_t row_id = 0;
	double bound = 0;
	while (k > 0 && rtree_index.NNScanNext(*state.index_state, row_id, bound)) {
		if (best.size() >= k && bound >= best.top().first) {
			break;
		}
		FlatVector::GetData<row_t>(candidate_ids)[0] = row_id;
		value_chunk.Reset();
		bind_data.table.GetStorage().Fetch(transaction, value_chunk, fetch_columns, candidate_ids, 1,
		                                   fetch_state);
		if (value_chunk.size() == 0 || FlatVector::IsNull(value_chunk.data[0], 0)) {
			continue;
		}
		const auto value = FlatVector::GetData<string_t>(value_chunk.data[0])[0];
		best.emplace(ExactDistance(rtree_index.GetBboxType(), value, bind_data.query_box.get()),
		             row_id);
		if (best.size() > k) {
			best.pop();
		}
	}

	state.nearest.resize(best.size());
	for (idx_t i = best.size(); i > 0; i--) {
		state.nearest[i - 1] = best.top().second;
		best.pop();
	}
	state.nearest_gathered = true;
}

static unique_ptr<GlobalTableFunctionState> RTreeIndexScanInitGlobal(ClientContext &context,
                                                                    TableFunctionInitInput &input) {												
	auto &bind_data = input.bind_data->Cast<TRTreeIndexScanBindData>();

	auto result = make_uniq<RTreeIndexScanGlobalState>();

	
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
		auto &rtree_index = bind_data.index.Cast<TRTreeIndex>();
		// A nearest-neighbour query reads the tree incrementally; every other operation
		// materialises its hits up front.
		result->index_state = bind_data.operation == TRTreeIndexScanBindData::NN_OPERATION
		    ? rtree_index.InitializeNNScan(bind_data.query_box.get(), bind_data.query_box_size)
		    : rtree_index.InitializeScan(bind_data.query_box.get(), bind_data.query_box_size,
		                                 bind_data.operation);
	}

	return std::move(result);
}

//-------------------------------------------------------------------------
// Execute
//-------------------------------------------------------------------------
static void RTreeIndexScanExecute(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {

	auto &bind_data = data_p.bind_data->Cast<TRTreeIndexScanBindData>();

	auto &state = data_p.global_state->Cast<RTreeIndexScanGlobalState>();

	auto &transaction = DuckTransaction::Get(context, bind_data.table.catalog);

	idx_t row_count = 0;
	if (bind_data.operation == TRTreeIndexScanBindData::NN_OPERATION) {
		if (!state.nearest_gathered) {
			GatherNearest(context, bind_data, state);
		}
		const auto row_ids = FlatVector::GetData<row_t>(state.row_ids);
		while (state.nearest_position < state.nearest.size() && row_count < STANDARD_VECTOR_SIZE) {
			row_ids[row_count++] = state.nearest[state.nearest_position++];
		}
	} else {
		row_count = bind_data.index.Cast<TRTreeIndex>().Scan(*state.index_state, state.row_ids);
	}

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
TableFunction TRTreeIndexScanFunction::GetFunction() {
	TableFunction func("mobility rtree index", {}, RTreeIndexScanExecute);
	func.init_global = RTreeIndexScanInitGlobal;
    
    func.get_bind_info = TRTreeIndexScanBindInfo;
    
    func.projection_pushdown = true;
    func.filter_pushdown = false; 
	return func;
}

// -------------------------------------------------------------------------
// Register
// -------------------------------------------------------------------------
void TRTreeModule::RegisterIndexScan(ExtensionLoader &loader) {
	loader.RegisterFunction( TRTreeIndexScanFunction::GetFunction());
}

} 
