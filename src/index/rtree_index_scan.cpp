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

#include "index/rtree_module.hpp"
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
	vector<LogicalType> scanned_types;
	ColumnFetchState fetch_state;
	TableScanState local_storage_state;
	vector<StorageIndex> column_ids;

	unique_ptr<IndexScanState> index_state;
	// rowid is BIGINT. Use the LogicalTypeId enumerator, never the
	// LogicalType::ROW_TYPE static const member: ODR-using it from this
	// extension TU emits a second definition that clashes with libduckdb
	// ("multiple definition of duckdb::LogicalType::ROW_TYPE" at link).
	Vector row_ids = Vector(LogicalType(LogicalTypeId::BIGINT));
};

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
			result->scanned_types.push_back(bind_data.table.GetColumn(LogicalIndex(id)).Type());
		} else {
			// rowid column: BIGINT (see row_ids note re: ROW_TYPE ODR clash)
			result->scanned_types.emplace_back(LogicalType(LogicalTypeId::BIGINT));
		}
		result->column_ids.emplace_back(col_id);
	}

	// Honour projection pushdown exactly like DuckDB's table_scan: when the
	// optimizer removes filter-only columns, fetch the full scanned set into
	// all_columns (which MUST be initialized with the scanned column types,
	// else Fetch hits "Expected vector of type X, found Y") and reference
	// the projected subset out of it.
	if (input.CanRemoveFilterColumns()) {
		result->projection_ids = input.projection_ids;
		result->all_columns.Initialize(context, result->scanned_types);
	}

	// Initialize the storage scan state
	result->local_storage_state.Initialize(result->column_ids, context, input.filters);
	local_storage.InitializeScan(bind_data.table.GetStorage(), result->local_storage_state.local_state, input.filters);

	// Initialize index scan - works for both STBOX and TSTZSPAN
	if (bind_data.query_box) {
        result->index_state = bind_data.index.Cast<TRTreeIndex>().InitializeScan(
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
static void RTreeIndexScanExecute(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {

	auto &bind_data = data_p.bind_data->Cast<TRTreeIndexScanBindData>();

	auto &state = data_p.global_state->Cast<RTreeIndexScanGlobalState>();

	auto &transaction = DuckTransaction::Get(context, bind_data.table.catalog);

	auto row_count = bind_data.index.Cast<TRTreeIndex>().Scan(*state.index_state, state.row_ids);

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

// The R-tree probe narrows rows by bounding-box overlap only. For an exact
// predicate (&&, @>) that is the answer; for a lossy predicate (the
// spatial-rel functions, whose bbox is only a superset) the original
// predicate must still be evaluated or the scan would emit false positives.
// Reporting that this scan can apply no pushed expression filter makes
// DuckDB keep every pushed predicate as an exact recheck PhysicalFilter
// directly above the scan (execution/physical_plan/plan_get.cpp rebuilds it
// via ExpressionFilter::ToExpression). This is the lossy-index-always-
// rechecks contract of PostGIS GiST and MobilityDB's tspatial_supportfn:
// the index is a prefilter, the recheck is correctness.
static bool RTreeIndexScanSupportsPushdownType(const FunctionData &, idx_t) {
	return false;
}

TableFunction TRTreeIndexScanFunction::GetFunction() {
	TableFunction func("mobility rtree index", {}, RTreeIndexScanExecute);
	func.init_global = RTreeIndexScanInitGlobal;

    func.get_bind_info = TRTreeIndexScanBindInfo;

    func.projection_pushdown = true;
    func.filter_pushdown = false;
    func.supports_pushdown_type = RTreeIndexScanSupportsPushdownType;
	return func;
}

// -------------------------------------------------------------------------
// Register
// -------------------------------------------------------------------------
void TRTreeModule::RegisterIndexScan(ExtensionLoader &loader) {
	loader.RegisterFunction( TRTreeIndexScanFunction::GetFunction());
}

} 
