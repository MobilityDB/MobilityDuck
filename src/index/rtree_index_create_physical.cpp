#include "meos_wrapper_simple.hpp"
#include "index/rtree_index_create_physical.hpp"
#include "duckdb/catalog/catalog_entry/duck_index_entry.hpp"
#include "duckdb/catalog/catalog_entry/duck_table_entry.hpp"
#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/common/exception/transaction_exception.hpp"
#include "duckdb/main/attached_database.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/storage/storage_manager.hpp"
#include "duckdb/storage/table_io_manager.hpp"
#include "duckdb/parallel/base_pipeline_event.hpp"

#include "index/rtree_module.hpp"

namespace duckdb {

//-------------------------------------------------------------
// Physical Create RTree Index
//-------------------------------------------------------------
PhysicalCreateRTreeIndex::PhysicalCreateRTreeIndex(const vector<LogicalType> &types_p, TableCatalogEntry &table_p,
                                                 const vector<column_t> &column_ids, unique_ptr<CreateIndexInfo> info,
                                                 vector<unique_ptr<Expression>> unbound_expressions,
                                                 idx_t estimated_cardinality)

    : PhysicalOperator(PhysicalOperatorType::EXTENSION, types_p, estimated_cardinality),
      table(table_p.Cast<DuckTableEntry>()), info(std::move(info)), unbound_expressions(std::move(unbound_expressions)),
      sorted(false) {

	for (auto &column_id : column_ids) {
		storage_ids.push_back(table.GetColumns().LogicalToPhysical(LogicalIndex(column_id)).index);
	}
}


//-------------------------------------------------------------
// Global State
//-------------------------------------------------------------
class CreateRTreeIndexGlobalState final : public GlobalSinkState {
public:
	CreateRTreeIndexGlobalState(const PhysicalOperator &op_p) : op(op_p) {
	}

	const PhysicalOperator &op;
	unique_ptr<RTreeIndex> global_index;

	mutex glock;
	unique_ptr<ColumnDataCollection> collection;
	shared_ptr<ClientContext> context;
	ColumnDataParallelScanState scan_state;

	atomic<bool> is_building = {false};
	atomic<idx_t> loaded_count = {0};
	atomic<idx_t> built_count = {0};
};

unique_ptr<GlobalSinkState> PhysicalCreateRTreeIndex::GetGlobalSinkState(ClientContext &context) const {

	auto gstate = make_uniq<CreateRTreeIndexGlobalState>(*this);

	vector<LogicalType> data_types = {unbound_expressions[0]->return_type, LogicalType::ROW_TYPE};
	gstate->collection = make_uniq<ColumnDataCollection>(BufferManager::GetBufferManager(context), data_types);
	gstate->context = context.shared_from_this();

	// Create the index
	auto &storage = table.GetStorage();
	auto &table_manager = TableIOManager::Get(storage);
	auto &constraint_type = info->constraint_type;
	auto &db = storage.db;
	gstate->global_index =
	    make_uniq<RTreeIndex>(info->index_name, constraint_type, storage_ids, table_manager, unbound_expressions, db,
	                         info->options, IndexStorageInfo());

	return std::move(gstate);
}


//-------------------------------------------------------------
// Local State
//-------------------------------------------------------------
class CreateRTreeIndexLocalState final : public LocalSinkState {
public:
	unique_ptr<ColumnDataCollection> collection;
	ColumnDataAppendState append_state;
};

unique_ptr<LocalSinkState> PhysicalCreateRTreeIndex::GetLocalSinkState(ExecutionContext &context) const {
	auto state = make_uniq<CreateRTreeIndexLocalState>();

	vector<LogicalType> data_types = {unbound_expressions[0]->return_type, LogicalType::ROW_TYPE};
	state->collection = make_uniq<ColumnDataCollection>(BufferManager::GetBufferManager(context.client), data_types);
	state->collection->InitializeAppend(state->append_state);
	return std::move(state);
}

//-------------------------------------------------------------
// Sink
//-------------------------------------------------------------

SinkResultType PhysicalCreateRTreeIndex::Sink(ExecutionContext &context, DataChunk &chunk,
                                             OperatorSinkInput &input) const {

	auto &lstate = input.local_state.Cast<CreateRTreeIndexLocalState>();
	auto &gstate = input.global_state.Cast<CreateRTreeIndexGlobalState>();
	lstate.collection->Append(lstate.append_state, chunk);
	gstate.loaded_count += chunk.size();
	return SinkResultType::NEED_MORE_INPUT;
}

//-------------------------------------------------------------
// Combine
//-------------------------------------------------------------
SinkCombineResultType PhysicalCreateRTreeIndex::Combine(ExecutionContext &context,
                                                       OperatorSinkCombineInput &input) const {
	auto &gstate = input.global_state.Cast<CreateRTreeIndexGlobalState>();
	auto &lstate = input.local_state.Cast<CreateRTreeIndexLocalState>();

	if (lstate.collection->Count() == 0) {
		return SinkCombineResultType::FINISHED;
	}

	lock_guard<mutex> l(gstate.glock);
	if (!gstate.collection) {
		gstate.collection = std::move(lstate.collection);
	} else {
		gstate.collection->Combine(*lstate.collection);
	}

	return SinkCombineResultType::FINISHED;
}

//-------------------------------------------------------------
// Finalize
//-------------------------------------------------------------

class RTreeIndexConstructTask final : public ExecutorTask {
public:
	RTreeIndexConstructTask(shared_ptr<Event> event_p, ClientContext &context, CreateRTreeIndexGlobalState &gstate_p,
	                       size_t thread_id_p, const PhysicalCreateRTreeIndex &op_p)
	    : ExecutorTask(context, std::move(event_p), op_p), gstate(gstate_p), thread_id(thread_id_p),
	      local_scan_state() {
		gstate.collection->InitializeScanChunk(scan_chunk);
	}

	TaskExecutionResult ExecuteTask(TaskExecutionMode mode) override {

		auto &scan_state = gstate.scan_state;
		auto &collection = gstate.collection;

		while (collection->Scan(scan_state, local_scan_state, scan_chunk)) {

			const auto count = scan_chunk.size();

			auto &vec_vec = scan_chunk.data[0];  
			auto &rowid_vec = scan_chunk.data[1]; 

			auto vector_type = vec_vec.GetType();
			
			if (vec_vec.GetVectorType() != VectorType::FLAT_VECTOR) {
				vec_vec.Flatten(count);
			}
			if (rowid_vec.GetVectorType() != VectorType::FLAT_VECTOR) {
				rowid_vec.Flatten(count);
			}

			UnifiedVectorFormat vec_format;
			UnifiedVectorFormat rowid_format;

			vec_vec.ToUnifiedFormat(count, vec_format);
			rowid_vec.ToUnifiedFormat(count, rowid_format);

			const auto row_ptr = UnifiedVectorFormat::GetData<row_t>(rowid_format);
			STBox* boxes = (STBox*)malloc(sizeof(STBox) * count);
			if (!boxes) {
				executor.PushError(ErrorData("Failed to allocate memory for STBox array"));
				return TaskExecutionResult::TASK_ERROR;
			}

			idx_t valid_count = 0;
			vector<row_t> valid_row_ids;

			for (idx_t i = 0; i < count; i++) {
				const auto vec_idx = vec_format.sel->get_index(i);
				const auto row_idx = rowid_format.sel->get_index(i);

				const auto vec_valid = vec_format.validity.RowIsValid(vec_idx);
				const auto rowid_valid = rowid_format.validity.RowIsValid(row_idx);
				
				if (!vec_valid || !rowid_valid) {
					continue;
				}

				fprintf(stderr, "Processing row %zu (vec_idx=%zu, row_idx=%zu)\n", i, vec_idx, row_idx);

				STBox *box = nullptr;
				
				if (vector_type.id() == LogicalTypeId::BLOB) {
					
					const auto stbox_data_ptr = UnifiedVectorFormat::GetData<string_t>(vec_format);
					auto blob_data = stbox_data_ptr[vec_idx];
					const uint8_t *stbox_bytes = reinterpret_cast<const uint8_t*>(blob_data.GetData());
					size_t stbox_size = blob_data.GetSize();
					box = (STBox*)malloc(stbox_size);
					memcpy(box, stbox_bytes, stbox_size);

					int32_t box_srid = stbox_srid(box);
					
					if (box_srid != 0) {
						STBox *normalized_box = stbox_set_srid(box, 0);
						if (normalized_box) {
							free(box);
							box = normalized_box;
						}
					}
					
					// Copy to our batch array
					memcpy(&boxes[valid_count], box, sizeof(STBox));
					valid_row_ids.push_back(row_ptr[row_idx]);
					valid_count++;
					
					free(box);
					
					
				}
				else {
					free(boxes);
					executor.PushError(ErrorData("Unsupported data type for RTree index: " + vector_type.ToString()));
					return TaskExecutionResult::TASK_ERROR;
				}
			}

			// Now batch insert the valid STBoxes into the index
			if (valid_count > 0) {
				auto &rtree_index = gstate.global_index;
				
				auto result = rtree_index->BulkConstruct(boxes, valid_row_ids.data(), valid_count);
				if (result.HasError()) {
					free(boxes);
					executor.PushError(result);
					return TaskExecutionResult::TASK_ERROR;
				}
				
			}

			free(boxes);

			gstate.built_count += count;

			if (mode == TaskExecutionMode::PROCESS_PARTIAL) {
				return TaskExecutionResult::TASK_NOT_FINISHED;
			}
		}
		event->FinishTask();
		return TaskExecutionResult::TASK_FINISHED;
	}

private:
	CreateRTreeIndexGlobalState &gstate;
	size_t thread_id;

	DataChunk scan_chunk;
	ColumnDataLocalScanState local_scan_state;
};

class RTreeIndexConstructionEvent final : public BasePipelineEvent {
public:
	RTreeIndexConstructionEvent(const PhysicalCreateRTreeIndex &op_p, CreateRTreeIndexGlobalState &gstate_p,
	                           Pipeline &pipeline_p, CreateIndexInfo &info_p, const vector<column_t> &storage_ids_p,
	                           DuckTableEntry &table_p)
	    : BasePipelineEvent(pipeline_p), op(op_p), gstate(gstate_p), info(info_p), storage_ids(storage_ids_p),
	      table(table_p) {
	}

	const PhysicalCreateRTreeIndex &op;
	CreateRTreeIndexGlobalState &gstate;
	CreateIndexInfo &info;
	const vector<column_t> &storage_ids;
	DuckTableEntry &table;

public:
	//Run Schedule
	void Schedule() override {
		auto &context = pipeline->GetClientContext();

		auto &ts = TaskScheduler::GetScheduler(context);
		const auto num_threads = NumericCast<size_t>(ts.NumberOfThreads());

		vector<shared_ptr<Task>> construct_tasks;
		for (size_t tnum = 0; tnum < num_threads; tnum++) {
			construct_tasks.push_back(make_uniq<RTreeIndexConstructTask>(shared_from_this(), context, gstate, tnum, op));
		}
		SetTasks(std::move(construct_tasks));
	}

	void FinishEvent() override {

		auto &storage = table.GetStorage();


		if (!storage.IsRoot()) {
			throw TransactionException("Cannot create index on non-root transaction");
		}

		// Create the index entry in the catalog
		auto &schema = table.schema;
		info.column_ids = storage_ids;

		if (schema.GetEntry(schema.GetCatalogTransaction(*gstate.context), CatalogType::INDEX_ENTRY, info.index_name)) {
			if (info.on_conflict != OnCreateConflict::IGNORE_ON_CONFLICT) {
				throw CatalogException("Index with name \"%s\" already exists", info.index_name);
			}
		}

		const auto index_entry = schema.CreateIndex(schema.GetCatalogTransaction(*gstate.context), info, table).get();
		D_ASSERT(index_entry);
		auto &duck_index = index_entry->Cast<DuckIndexEntry>();
		duck_index.initial_index_size = gstate.global_index->Cast<BoundIndex>().GetInMemorySize();

		storage.AddIndex(std::move(gstate.global_index));
	}
};

SinkFinalizeType PhysicalCreateRTreeIndex::Finalize(Pipeline &pipeline, Event &event, ClientContext &context,
                                                   OperatorSinkFinalizeInput &input) const {
	

	auto &gstate = input.global_state.Cast<CreateRTreeIndexGlobalState>();
	auto &collection = gstate.collection;

	gstate.is_building = true;

	auto &ts = TaskScheduler::GetScheduler(context);
	collection->InitializeScan(gstate.scan_state, ColumnDataScanProperties::ALLOW_ZERO_COPY);

	auto new_event = make_shared_ptr<RTreeIndexConstructionEvent>(*this, gstate, pipeline, *info, storage_ids, table);
	event.InsertEvent(std::move(new_event));

	return SinkFinalizeType::READY;
}

ProgressData PhysicalCreateRTreeIndex::GetSinkProgress(ClientContext &context, GlobalSinkState &gstate,
                                                      ProgressData source_progress) const {
	ProgressData res;

	const auto &state = gstate.Cast<CreateRTreeIndexGlobalState>();
	// First half of the progress is appending to the collection
	if (!state.is_building) {
		res.done = state.loaded_count + 0.0;
		res.total = estimated_cardinality + estimated_cardinality;
	} else {
		res.done = state.loaded_count + state.built_count;
		res.total = state.loaded_count + state.loaded_count;
	}
	return res;
}

}
