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

namespace duckdb {

//-------------------------------------------------------------------------
// BIND - Define the output schema for the info function
//-------------------------------------------------------------------------
static unique_ptr<FunctionData> RTreeIndexInfoBind(ClientContext &context, TableFunctionBindInput &input,
                                                   vector<LogicalType> &return_types, vector<string> &names) {
	names.emplace_back("catalog_name");
	return_types.emplace_back(LogicalType::VARCHAR);

	names.emplace_back("schema_name");
	return_types.emplace_back(LogicalType::VARCHAR);

	names.emplace_back("index_name");
	return_types.emplace_back(LogicalType::VARCHAR);

	names.emplace_back("table_name");
	return_types.emplace_back(LogicalType::VARCHAR);

	names.emplace_back("type");
	return_types.emplace_back(LogicalType::VARCHAR);

	names.emplace_back("memory_usage");
	return_types.emplace_back(LogicalType::BIGINT);

	return nullptr;
}

//-------------------------------------------------------------------------
// INIT GLOBAL - Initialize the state for scanning indexes
//-------------------------------------------------------------------------
struct RTreeIndexInfoGlobalState : public GlobalTableFunctionState {
	idx_t offset = 0;
	vector<reference<IndexCatalogEntry>> entries;
};

static unique_ptr<GlobalTableFunctionState> RTreeIndexInfoInitGlobal(ClientContext &context,
                                                                     TableFunctionInitInput &input) {
	auto result = make_uniq<RTreeIndexInfoGlobalState>();

	auto schemas = Catalog::GetAllSchemas(context);
	for (auto &schema : schemas) {
		schema.get().Scan(context, CatalogType::INDEX_ENTRY, [&](CatalogEntry &entry) {
			auto &index_entry = entry.Cast<IndexCatalogEntry>();

			if (index_entry.index_type == RTreeIndex::TYPE_NAME || 
			    index_entry.index_type == "TRTREE") {
				result->entries.push_back(index_entry);
			}
		});
	}
	
	
	return std::move(result);
}

//-------------------------------------------------------------------------
// EXECUTE - Output information about RTree indexes
//-------------------------------------------------------------------------
static void RTreeIndexInfoExecute(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &data = data_p.global_state->Cast<RTreeIndexInfoGlobalState>();
	if (data.offset >= data.entries.size()) {
		return;
	}

	idx_t row = 0;
	while (data.offset < data.entries.size() && row < STANDARD_VECTOR_SIZE) {
		auto &index_entry = data.entries[data.offset++].get();
		auto &table_entry = index_entry.schema.catalog.GetEntry<TableCatalogEntry>(context, index_entry.GetSchemaName(),
		                                                                           index_entry.GetTableName());

		idx_t col = 0;
		output.data[col++].SetValue(row, Value(index_entry.catalog.GetName()));
		output.data[col++].SetValue(row, Value(index_entry.schema.name));
		output.data[col++].SetValue(row, Value(index_entry.name));
		output.data[col++].SetValue(row, Value(table_entry.name));
		output.data[col++].SetValue(row, Value("TRTREE (Stbox)"));
		output.data[col++].SetValue(row, Value::BIGINT(8192)); 

		row++;
	}
	output.SetCardinality(row);
}

//-------------------------------------------------------------------------
// Helper function to find RTree index by name
//-------------------------------------------------------------------------
static optional_ptr<RTreeIndex> TryGetRTreeIndex(ClientContext &context, const string &index_name) {
	auto qname = QualifiedName::Parse(index_name);

	Binder::BindSchemaOrCatalog(context, qname.catalog, qname.schema);
	auto &index_entry = Catalog::GetEntry(context, CatalogType::INDEX_ENTRY, qname.catalog, qname.schema, qname.name)
	                        .Cast<IndexCatalogEntry>();
	auto &table_entry = Catalog::GetEntry(context, CatalogType::TABLE_ENTRY, qname.catalog, index_entry.GetSchemaName(),
	                                      index_entry.GetTableName())
	                        .Cast<TableCatalogEntry>();

	auto &storage = table_entry.GetStorage();
	RTreeIndex *rtree_index = nullptr;

	auto &table_info = *storage.GetDataTableInfo();
	table_info.GetIndexes().BindAndScan<RTreeIndex>(context, table_info, [&](RTreeIndex &index) {
		if (index.name == index_name) {
			rtree_index = &index;
			return true;
		}
		return false;
	});

	return rtree_index;
}

//-------------------------------------------------------------------------
// Vacuum PRAGMA 
//-------------------------------------------------------------------------
static void VacuumRTreeIndexPragma(ClientContext &context, const FunctionParameters &parameters) {
	if (parameters.values.size() != 1) {
		throw BinderException("Expected one argument for rtree_vacuum_index");
	}
	auto &param = parameters.values[0];
	if (param.type() != LogicalType::VARCHAR) {
		throw BinderException("Expected a string argument for rtree_vacuum_index");
	}
	auto index_name = param.GetValue<string>();

	auto rtree_index = TryGetRTreeIndex(context, index_name);
	if (!rtree_index) {
		throw BinderException("RTree Index %s not found", index_name);
	}
}

static unique_ptr<FunctionData> DebugAllIndexesBind(ClientContext &context, TableFunctionBindInput &input,
                                                    vector<LogicalType> &return_types, vector<string> &names) {
	names.emplace_back("catalog_name");
	return_types.emplace_back(LogicalType::VARCHAR);
	
	names.emplace_back("schema_name");
	return_types.emplace_back(LogicalType::VARCHAR);
	
	names.emplace_back("index_name");
	return_types.emplace_back(LogicalType::VARCHAR);
	
	names.emplace_back("table_name");
	return_types.emplace_back(LogicalType::VARCHAR);
	
	names.emplace_back("index_type");
	return_types.emplace_back(LogicalType::VARCHAR);
	
	return nullptr;
}

struct DebugAllIndexesGlobalState : public GlobalTableFunctionState {
	idx_t offset = 0;
	vector<reference<IndexCatalogEntry>> entries;
};

static unique_ptr<GlobalTableFunctionState> DebugAllIndexesInitGlobal(ClientContext &context,
                                                                      TableFunctionInitInput &input) {
	auto result = make_uniq<DebugAllIndexesGlobalState>();

	auto schemas = Catalog::GetAllSchemas(context);
	for (auto &schema : schemas) {
		schema.get().Scan(context, CatalogType::INDEX_ENTRY, [&](CatalogEntry &entry) {
			auto &index_entry = entry.Cast<IndexCatalogEntry>();
			result->entries.push_back(index_entry);
		});
	}
	
	return std::move(result);
}

static void DebugAllIndexesExecute(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &data = data_p.global_state->Cast<DebugAllIndexesGlobalState>();
	if (data.offset >= data.entries.size()) {
		return;
	}

	idx_t row = 0;
	while (data.offset < data.entries.size() && row < STANDARD_VECTOR_SIZE) {
		auto &index_entry = data.entries[data.offset++].get();
		auto &table_entry = index_entry.schema.catalog.GetEntry<TableCatalogEntry>(context, index_entry.GetSchemaName(),
		                                                                           index_entry.GetTableName());

		idx_t col = 0;
		output.data[col++].SetValue(row, Value(index_entry.catalog.GetName()));
		output.data[col++].SetValue(row, Value(index_entry.schema.name));
		output.data[col++].SetValue(row, Value(index_entry.name));
		output.data[col++].SetValue(row, Value(table_entry.name));
		output.data[col++].SetValue(row, Value(index_entry.index_type));

		row++;
	}
	output.SetCardinality(row);
}


//-------------------------------------------------------------------------
// Register all pragma functions
//-------------------------------------------------------------------------
// void RTreeModule::RegisterIndexPragmas(DatabaseInstance &db) {
// 	ExtensionUtil::RegisterFunction(
// 	    db, PragmaFunction::PragmaCall("rtree_vacuum_index", VacuumRTreeIndexPragma, {LogicalType::VARCHAR}));

// 	TableFunction info_function("pragma_rtree_index_info", {}, RTreeIndexInfoExecute, RTreeIndexInfoBind,
// 	                            RTreeIndexInfoInitGlobal);
// 	ExtensionUtil::RegisterFunction(db, info_function);

//     TableFunction debug_function("debug_all_indexes", {}, DebugAllIndexesExecute, DebugAllIndexesBind,
// 	                            DebugAllIndexesInitGlobal);
// 	ExtensionUtil::RegisterFunction(db, debug_function);
// }

} 