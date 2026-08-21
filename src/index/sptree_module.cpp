#include "meos_wrapper_simple.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include "duckdb/execution/index/fixed_size_allocator.hpp"
#include "duckdb/execution/index/index_pointer.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckdb/main/extension.hpp"
#include "duckdb/common/serializer/binary_deserializer.hpp"
#include "duckdb/common/serializer/binary_serializer.hpp"
#include "duckdb/execution/index/fixed_size_allocator.hpp"
#include "duckdb/storage/table/scan_state.hpp"
#include "duckdb/storage/table_io_manager.hpp"
#include "duckdb/main/database.hpp"

#include "duckdb/execution/index/bound_index.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/execution/operator/scan/physical_table_scan.hpp"
#include "duckdb/planner/operator/logical_create_index.hpp"
#include "duckdb/planner/expression/bound_reference_expression.hpp"
#include "duckdb/planner/expression/bound_operator_expression.hpp"
#include "duckdb/parser/parsed_data/create_index_info.hpp"
#include "duckdb/execution/operator/projection/physical_projection.hpp"
#include "duckdb/execution/operator/filter/physical_filter.hpp"
#include "duckdb/common/case_insensitive_map.hpp"
#include "duckdb/optimizer/matcher/expression_matcher.hpp"
#include "index/sptree_module.hpp"
#include "index/sptree_index_scan.hpp"
#include "geo/stbox.hpp"
#include "geo/tgeompoint.hpp"
#include "geo/tgeometry.hpp"
#include "geo/tgeography.hpp"
#include "geo/tgeogpoint.hpp"
#include "temporal/span.hpp"
#include "temporal/tbox.hpp"
#include "temporal/temporal.hpp"
#include "index/sptree_index_create_physical.hpp"
#include "time_util.hpp"


namespace duckdb {

//------------------------------------------------------------------------------
// SPTreeIndex Implementation with MEOS SPTree Integration
//------------------------------------------------------------------------------

TSPTreeIndex::TSPTreeIndex(const string &name, IndexConstraintType constraint_type,
                       const vector<column_t> &column_ids, TableIOManager &table_io_manager,
                       const vector<unique_ptr<Expression>> &unbound_expressions,
                       AttachedDatabase &db,
                       const case_insensitive_map_t<Value> &options,
                       const IndexStorageInfo &info)
    : BoundIndex(name, TYPE_NAME, constraint_type, column_ids, table_io_manager, 
                unbound_expressions, db), options_(options), sptree_(nullptr) {
    
    
    // The `kind` create option selects the space-partitioning structure. The two
    // kinds answer the same queries over the same MEOS tree and differ in how a
    // node divides its region, so the choice is a performance one.
    kind_ = SPTREE_QUADTREE;
    auto kind_entry = options_.find("kind");
    if (kind_entry != options_.end()) {
        auto kind_name = StringUtil::Lower(kind_entry->second.ToString());
        if (kind_name == "quadtree") {
            kind_ = SPTREE_QUADTREE;
        } else if (kind_name == "kdtree") {
            kind_ = SPTREE_KDTREE;
        } else {
            throw BinderException(
                "TSPTREE index kind must be quadtree or kdtree. Got: " + kind_name);
        }
    }

    // The tree holds ONE bounding box per row and takes it from the first indexed expression, so
    // a second column would be accepted and then never stored: every query reading it would be
    // answered from a box that says nothing about it, and the answer is wrong rather than slow.
    if (unbound_expressions.size() != 1) {
        throw BinderException(
            "A TSPTREE index covers a single column, and this one names " +
            std::to_string(unbound_expressions.size()) + ". Create one index per column.");
    }

    auto &type = unbound_expressions[0]->return_type;
    column_type_ = type;

    // space-partitioning tree's bbox type is determined by the type of the indexed column
    
    if (type == StboxType::stbox()) {
        bbox_type_ = T_STBOX;
        bbox_size_ = sizeof(STBox);
        sptree_ = sptree_create_stbox(kind_);
    } else if (type == SpanTypes::tstzspan()) {
        bbox_type_ = T_TSTZSPAN;
        bbox_size_ = sizeof(Span);  
        sptree_ = sptree_create_tstzspan(kind_);
    } else if (type == TboxType::tbox()) {
        bbox_type_ = T_TBOX;
        bbox_size_ = sizeof(TBox);
        sptree_ = sptree_create_tbox(kind_);
    } else if (type == SpanTypes::intspan()) {
        bbox_type_ = T_INTSPAN;
        bbox_size_ = sizeof(Span);
        sptree_ = sptree_create_intspan(kind_);
    } else if (type == SpanTypes::bigintspan()) {
        bbox_type_ = T_BIGINTSPAN;
        bbox_size_ = sizeof(Span);
        sptree_ = sptree_create_bigintspan(kind_);
    } else if (type == SpanTypes::floatspan()) {
        bbox_type_ = T_FLOATSPAN;
        bbox_size_ = sizeof(Span);
        sptree_ = sptree_create_floatspan(kind_);
    } else if (type == SpanTypes::datespan()) {
        bbox_type_ = T_DATESPAN;
        bbox_size_ = sizeof(Span);
        sptree_ = sptree_create_datespan(kind_);
    } else if (type == TemporalTypes::tint() || type == TemporalTypes::tfloat()) {
        // Temporal numbers: the bounding box is a tbox.
        bbox_type_ = T_TBOX;
        bbox_size_ = sizeof(TBox);
        sptree_ = sptree_create_tbox(kind_);
    } else if (type == TemporalTypes::tbool() || type == TemporalTypes::ttext()) {
        // Non-numeric, non-spatial temporals: the bounding box is the time span.
        bbox_type_ = T_TSTZSPAN;
        bbox_size_ = sizeof(Span);
        sptree_ = sptree_create_tstzspan(kind_);
    } else if (type == TgeompointType::tgeompoint() ||
               type == TGeometryTypes::tgeometry() ||
               type == TGeographyTypes::tgeography() ||
               type == TGeogpointType::tgeogpoint()) {
        // Temporal spatial types: the bounding box is an stbox.
        bbox_type_ = T_STBOX;
        bbox_size_ = sizeof(STBox);
        sptree_ = sptree_create_stbox(kind_);
    } else {
        throw BinderException(
            "TSPTREE index supports stbox, tbox, the five span types, and the "
            "temporal types (tint, tfloat, tbool, ttext, tgeompoint, tgeogpoint, "
            "tgeometry, tgeography). Got: " + type.ToString());
    }
    
    if (!sptree_) {
        throw InternalException("Failed to create MEOS SPTree");
    }

    // Set up the storage backing the tree, and rebuild the tree if this index
    // is being bound from a persisted database or from the WAL. The destructor
    // does not run when a constructor throws, so free the tree ourselves.
    try {
        InitEntryStorage(info);
    } catch (...) {
        sptree_free(sptree_);
        sptree_ = nullptr;
        throw;
    }

    function_matcher = MakeFunctionMatcher();
    nearest_matcher = MakeNearestMatcher();
}

//! A nearest-neighbour scan holds the tree open: the cursor keeps the priority queue of the
//! best-first traversal, so each call resumes where the last stopped instead of restarting.
class TSPTreeNNScanState final : public IndexScanState {
public:
    void *query_box = nullptr;
    SPNNCursor *cursor = nullptr;

    ~TSPTreeNNScanState() {
        if (cursor) {
            sptree_nn_cursor_close(cursor);
            cursor = nullptr;
        }
        if (query_box) {
            free(query_box);
            query_box = nullptr;
        }
    }
};

class TSPTreeIndexScanState final : public IndexScanState {
public:
    void* query_box = nullptr;
    vector<row_t> search_results;
    idx_t current_position = 0;
    bool initialized = false;

    ~TSPTreeIndexScanState() {
        if (query_box) {
            free(query_box);
            query_box = nullptr;
        }
    }
};

TSPTreeIndex::~TSPTreeIndex() {
    if (sptree_) {
        sptree_free(sptree_);
        sptree_ = nullptr;
    }

}

//------------------------------------------------------------------------------
// Persistence of the indexed (bounding box, row id) entries
//------------------------------------------------------------------------------

void TSPTreeIndex::InitEntryStorage(const IndexStorageInfo &info) {
    entry_size_ = sizeof(row_t) + bbox_size_;
    entries_per_segment_ =
        MaxValue<idx_t>(1, (ENTRY_SEGMENT_TARGET_SIZE - ENTRY_SEGMENT_HEADER_SIZE) / entry_size_);
    segment_size_ = ENTRY_SEGMENT_HEADER_SIZE + entries_per_segment_ * entry_size_;

    auto &block_manager = table_io_manager.GetIndexBlockManager();
    entry_allocator_ = make_uniq<FixedSizeAllocator>(segment_size_, block_manager);

    if (!info.IsValid()) {
        // Either a brand-new index, or one that was serialized while empty.
        return;
    }
    if (info.allocator_infos.size() != 1) {
        throw SerializationException("TSPTREE index \"%s\" holds %llu allocators, expected exactly one", name,
                                     info.allocator_infos.size());
    }

    // FixedSizeAllocator::Init adopts the stored segment size without recomputing
    // the buffer layout, so a mismatch has to be rejected here rather than read
    // back as garbage. It can only happen if the bounding box layout of the build
    // that wrote the database differs from the one of this build.
    if (info.allocator_infos[0].segment_size != segment_size_) {
        throw SerializationException(
            "TSPTREE index \"%s\" was written with a segment size of %llu, but this build uses %llu", name,
            info.allocator_infos[0].segment_size, segment_size_);
    }

    entry_allocator_->Init(info.allocator_infos[0]);
    entry_head_.Set(info.root);
    ReplayEntries();
}

void TSPTreeIndex::ReplayEntries() {
    vector<data_t> box(bbox_size_);

    auto ptr = entry_head_;
    while (ptr.HasMetadata()) {
        const auto segment = entry_allocator_->Get(ptr, false);

        idx_t next;
        idx_t count;
        memcpy(&next, segment, sizeof(idx_t));
        memcpy(&count, segment + sizeof(idx_t), sizeof(idx_t));

        if (count > entries_per_segment_) {
            throw SerializationException("TSPTREE index \"%s\" holds a segment with %llu entries, at most %llu fit",
                                         name, count, entries_per_segment_);
        }

        for (idx_t i = 0; i < count; i++) {
            const auto entry = segment + ENTRY_SEGMENT_HEADER_SIZE + i * entry_size_;
            row_t row_id;
            memcpy(&row_id, entry, sizeof(row_t));
            if (row_id == TOMBSTONE_ROW_ID) {
                // Deleted before this index was written; do not resurrect it.
                continue;
            }
            memcpy(box.data(), entry + sizeof(row_t), bbox_size_);
            sptree_insert(sptree_, box.data(), row_id);
        }

        entry_count_ += count;
        entry_tail_ = ptr;
        tail_count_ = count;
        ptr.Set(next);
    }
}

void TSPTreeIndex::RecordEntry(const void *box, row_t row_id) {
    if (!entry_tail_.HasMetadata() || tail_count_ == entries_per_segment_) {
        auto ptr = entry_allocator_->New();
        // The metadata byte is what distinguishes a segment pointer from the
        // zeroed pointer that terminates the chain.
        ptr.SetMetadata(1);

        if (entry_tail_.HasMetadata()) {
            const idx_t next = ptr.Get();
            memcpy(entry_allocator_->Get(entry_tail_), &next, sizeof(idx_t));
        } else {
            entry_head_ = ptr;
        }

        const idx_t header[2] = {0, 0};
        memcpy(entry_allocator_->Get(ptr), header, sizeof(header));

        entry_tail_ = ptr;
        tail_count_ = 0;
    }

    const auto segment = entry_allocator_->Get(entry_tail_);
    const auto entry = segment + ENTRY_SEGMENT_HEADER_SIZE + tail_count_ * entry_size_;
    memcpy(entry, &row_id, sizeof(row_t));
    memcpy(entry + sizeof(row_t), box, bbox_size_);

    // DuckDB can hand out a row id again once the old row is vacuumed away, so
    // an id being indexed now is live regardless of what happened to it before.
    deleted_.erase(row_id);

    tail_count_++;
    memcpy(segment + sizeof(idx_t), &tail_count_, sizeof(idx_t));
    entry_count_++;
}

IndexStorageInfo TSPTreeIndex::PrepareSerialize(const case_insensitive_map_t<Value> &options) {
    IndexStorageInfo info(name);
    info.root = entry_head_.Get();
    info.options = options;

    entry_allocator_->RemoveEmptyBuffers();
    return info;
}

IndexStorageInfo TSPTreeIndex::SerializeToDisk(QueryContext context, const case_insensitive_map_t<Value> &options) {
    auto info = PrepareSerialize(options);

    // Write the segments to the database file as partial blocks.
    auto &block_manager = table_io_manager.GetIndexBlockManager();
    PartialBlockManager partial_block_manager(context, block_manager, PartialBlockType::FULL_CHECKPOINT);
    entry_allocator_->SerializeBuffers(partial_block_manager);
    partial_block_manager.FlushPartialBlocks();

    info.allocator_infos.push_back(entry_allocator_->GetInfo());
    return info;
}

IndexStorageInfo TSPTreeIndex::SerializeToWAL(const case_insensitive_map_t<Value> &options) {
    auto info = PrepareSerialize(options);

    // Hand the raw buffers over to the WAL.
    info.buffers.push_back(entry_allocator_->InitSerializationToWAL());
    info.allocator_infos.push_back(entry_allocator_->GetInfo());
    return info;
}

PhysicalOperator &TSPTreeIndex::CreatePlan(PlanIndexInput &input) {
    auto &create_index = input.op;
    auto &planner = input.planner;

    vector<LogicalType> new_column_types;
    vector<unique_ptr<Expression>> select_list;
    
    for (auto &expression : create_index.expressions) {
        new_column_types.push_back(expression->return_type);
        select_list.push_back(std::move(expression));
    }
    
    LogicalType row_type = LogicalType::ROW_TYPE;
    new_column_types.push_back(row_type);
    select_list.push_back(
        make_uniq<BoundReferenceExpression>(row_type, create_index.info->scan_types.size() - 1)
    );

    auto &projection = planner.Make<PhysicalProjection>(new_column_types, std::move(select_list),
                                                       create_index.estimated_cardinality);
    projection.children.push_back(input.table_scan);

    auto &physical_create_index = planner.Make<PhysicalCreateTSPTreeIndex>(
        create_index.types, create_index.table, create_index.info->column_ids,
        std::move(create_index.info), std::move(create_index.unbound_expressions),
        create_index.estimated_cardinality);

    physical_create_index.children.push_back(projection);
    return physical_create_index;
}

//------------------------------------------------------------------------------
// Core SPTree Operations using MEOS
//------------------------------------------------------------------------------
ErrorData TSPTreeIndex::Insert(IndexLock &lock, DataChunk &data, Vector &row_ids) {
    // Inserting and appending are the same operation for this index: both add
    // the bounding boxes of the chunk to the tree.
    return Append(lock, data, row_ids);
}

ErrorData TSPTreeIndex::Append(IndexLock &lock, DataChunk &appended_data, Vector &row_identifiers) {
    
    DataChunk expression_result;
    expression_result.Initialize(Allocator::DefaultAllocator(), logical_types);

    ExecuteExpressions(appended_data, expression_result);

    Construct(expression_result, row_identifiers);
    
    return ErrorData();
}

void TSPTreeIndex::Construct(DataChunk &expression_result, Vector &row_identifiers) {
    if (!sptree_) {
        throw InternalException("SPTree not initialized");
    }

    if (expression_result.size() == 0 || expression_result.ColumnCount() == 0) {
        return;
    }

    auto &data_vector = expression_result.data[0];
    auto row_data = FlatVector::GetData<row_t>(row_identifiers);

    if (data_vector.GetVectorType() != VectorType::FLAT_VECTOR) {
        data_vector.Flatten(expression_result.size());
    }

    // True when the indexed column holds a Temporal value (the bbox is derived
    // per row at insert time). False when the column already holds a span / tbox
    // / stbox blob whose bytes are the bbox itself.
    const bool indexes_temporal =
        column_type_ == TemporalTypes::tint() ||
        column_type_ == TemporalTypes::tfloat() ||
        column_type_ == TemporalTypes::tbool() ||
        column_type_ == TemporalTypes::ttext() ||
        column_type_ == TgeompointType::tgeompoint() ||
        column_type_ == TGeometryTypes::tgeometry() ||
        column_type_ == TGeographyTypes::tgeography() ||
        column_type_ == TGeogpointType::tgeogpoint();

    // The bounding box of the row currently being indexed. It is both handed to
    // the MEOS tree and recorded for persistence, so that reloading the index
    // rebuilds exactly the tree we have here.
    //
    // Sized for the LARGEST bounding box rather than for bbox_size_: MEOS writes
    // through temporal_set_bbox into a bboxunion big enough for any type, and
    // sizing to the column's own box would turn a type/tree mismatch into a heap
    // overflow instead of a wrong answer. Only bbox_size_ bytes are ever handed
    // to the tree or recorded.
    vector<data_t> box(MaxValue<idx_t>(sizeof(STBox), MaxValue<idx_t>(sizeof(TBox), sizeof(Span))));

    for (idx_t i = 0; i < expression_result.size(); i++) {
        if (FlatVector::IsNull(data_vector, i)) {
            continue;
        }

        if (data_vector.GetType().id() != LogicalTypeId::BLOB) {
            continue;
        }

        auto blob_data = FlatVector::GetData<string_t>(data_vector)[i];
        const uint8_t *data = reinterpret_cast<const uint8_t *>(blob_data.GetData());
        size_t data_size = blob_data.GetSize();

        if (indexes_temporal) {
            const Temporal *temp = reinterpret_cast<const Temporal *>(data);
            if (bbox_type_ == T_STBOX) {
                STBox *stbox = tspatial_to_stbox(temp);
                if (!stbox) {
                    continue;
                }
                if (stbox_srid(stbox) != 0) {
                    STBox *normalized = stbox_set_srid(stbox, 0);
                    if (normalized) {
                        free(stbox);
                        stbox = normalized;
                    }
                }
                memcpy(box.data(), stbox, bbox_size_);
                free(stbox);
            } else {
                // The bounding box sptree_insert_temporal would have computed.
                memset(box.data(), 0, bbox_size_);
                temporal_set_bbox(temp, box.data());
            }
        } else {
            if (data_size != bbox_size_) {
                continue;
            }
            memcpy(box.data(), data, bbox_size_);

            if (bbox_type_ == T_STBOX) {
                STBox *stbox = (STBox *) box.data();
                if (stbox_srid(stbox) != 0) {
                    STBox *normalized = stbox_set_srid(stbox, 0);
                    if (normalized) {
                        memcpy(box.data(), normalized, bbox_size_);
                        free(normalized);
                    }
                }
            }
        }

        sptree_insert(sptree_, box.data(), row_data[i]);
        RecordEntry(box.data(), row_data[i]);
    }
}


ErrorData TSPTreeIndex::BulkConstruct(STBox* boxes, const row_t* row_ids, idx_t count) {
    if (!sptree_) {
        return ErrorData("SPTree not initialized");
    }

    for (idx_t i = 0; i < count; i++) {
        sptree_insert(sptree_, &boxes[i], row_ids[i]);
        RecordEntry(&boxes[i], row_ids[i]);
    }

    return ErrorData();
}

void TSPTreeIndex::Delete(IndexLock &lock, DataChunk &entries, Vector &row_identifiers) {
    if (entries.size() == 0) {
        return;
    }
    if (row_identifiers.GetVectorType() != VectorType::FLAT_VECTOR) {
        row_identifiers.Flatten(entries.size());
    }
    const auto row_data = FlatVector::GetData<row_t>(row_identifiers);

    // The MEOS tree exposes no removal entry point, so a deleted row cannot be
    // taken out of the tree we hold. Two things happen instead: the row id is
    // remembered so that every later search filters it out, and its persisted
    // entry is tombstoned so that reloading the index does not put it back.
    unordered_set<row_t> removed;
    for (idx_t i = 0; i < entries.size(); i++) {
        if (FlatVector::IsNull(row_identifiers, i)) {
            continue;
        }
        deleted_.insert(row_data[i]);
        removed.insert(row_data[i]);
    }
    if (removed.empty()) {
        return;
    }
    TombstoneEntries(removed);
}

void TSPTreeIndex::TombstoneEntries(const unordered_set<row_t> &removed) {
    // One pass over the chain per DELETE statement, rather than keeping a row
    // id to slot map: deletes are far rarer than searches on this index, and
    // the map would cost memory for every indexed row to speed up the rare one.
    auto ptr = entry_head_;
    while (ptr.HasMetadata()) {
        const auto segment = entry_allocator_->Get(ptr);

        idx_t next;
        idx_t count;
        memcpy(&next, segment, sizeof(idx_t));
        memcpy(&count, segment + sizeof(idx_t), sizeof(idx_t));

        for (idx_t i = 0; i < count; i++) {
            const auto entry = segment + ENTRY_SEGMENT_HEADER_SIZE + i * entry_size_;
            row_t row_id;
            memcpy(&row_id, entry, sizeof(row_t));
            if (removed.find(row_id) != removed.end()) {
                const row_t tombstone = TOMBSTONE_ROW_ID;
                memcpy(entry, &tombstone, sizeof(row_t));
            }
        }
        ptr.Set(next);
    }
}
//------------------------------------------------------------------------------
// SPTree Search Operations
//------------------------------------------------------------------------------
unique_ptr<IndexScanState> TSPTreeIndex::InitializeScan(const void* query_blob, size_t blob_size, const string &operation) const {
    const uint8_t *data = reinterpret_cast<const uint8_t*>(query_blob);
    
    auto state = make_uniq<TSPTreeIndexScanState>();
    
    if (operation == "@>" && bbox_type_ == T_TSTZSPAN) {
        if (blob_size != sizeof(timestamp_tz_t)) {
            throw InvalidInputException("Invalid query box size for @> operation. Expected " + 
                                      std::to_string(sizeof(timestamp_tz_t)) + 
                                      ", got " + std::to_string(blob_size));
        }
        
        timestamp_tz_t timestamp;
        memcpy(&timestamp, data, sizeof(timestamp_tz_t));
        TimestampTz meos_timestamp = static_cast<TimestampTz>(timestamp.value);
        Datum timestamp_datum = (Datum)meos_timestamp;
        
        state->query_box = malloc(sizeof(Span));
        memset(state->query_box, 0, sizeof(Span));
        
        Span *point_span = static_cast<Span*>(state->query_box);
        point_span->lower = timestamp_datum;
        point_span->upper = timestamp_datum;
        point_span->lower_inc = true;
        point_span->upper_inc = true;
        point_span->spantype = T_TSTZSPAN;
        point_span->basetype = T_TIMESTAMPTZ;  
        
        
    } else if (operation == "&&") {
        
        state->query_box = malloc(blob_size);
        memcpy(state->query_box, data, blob_size);

        if (bbox_type_ == T_STBOX) {
            STBox *stbox = (STBox*)state->query_box;
            int32_t query_srid = stbox_srid(stbox);
            if (query_srid != 0) {
                STBox *normalized_query = stbox_set_srid(stbox, 0);
                if (normalized_query) {
                    free(state->query_box);
                    state->query_box = malloc(blob_size);
                    memcpy(state->query_box, normalized_query, blob_size);
                    free(normalized_query);
                }
            }
        }
        
    } else {
        throw InvalidInputException("Unsupported R-Tree operation: " + operation + 
                                  " for bbox_type: " + std::to_string(bbox_type_));
    }
    
    if (sptree_) {
        /* MEOS sptree_search: @> uses containment, && uses overlap (see RTreeSearchOp in meos.h). */
        const RTreeSearchOp search_op = (operation == "@>") ? RTREE_CONTAINS : RTREE_OVERLAPS;
        state->search_results = Search(state->query_box, search_op);
        state->initialized = true;
    } 
    
    state->current_position = 0;
    
    return std::move(state);
}

idx_t TSPTreeIndex::Scan(IndexScanState &state, Vector &result) const {
    auto &sstate = state.Cast<TSPTreeIndexScanState>();
    
    if (!sstate.initialized || sstate.search_results.empty()) {
        return 0;
    }

    const auto row_ids = FlatVector::GetData<row_t>(result);
    
    idx_t output_idx = 0;
    const idx_t max_output = STANDARD_VECTOR_SIZE;

    while (sstate.current_position < sstate.search_results.size() && 
           output_idx < max_output) {
        
        row_ids[output_idx] = sstate.search_results[sstate.current_position];
        output_idx++;
        sstate.current_position++;
    }
    
    return output_idx;
}

unique_ptr<IndexScanState> TSPTreeIndex::InitializeNNScan(const void *query_blob, size_t blob_size) const {
    auto state = make_uniq<TSPTreeNNScanState>();

    state->query_box = malloc(blob_size);
    if (!state->query_box) {
        throw InvalidInputException("Out of memory for the nearest-neighbour query box");
    }
    memcpy(state->query_box, query_blob, blob_size);

    // The index stores its boxes with the SRID cleared, so a query box carrying one would be a
    // mixed-SRID comparison in MEOS rather than a distance. Probe exactly as the insert stored.
    if (bbox_type_ == T_STBOX) {
        STBox *query = static_cast<STBox *>(state->query_box);
        if (stbox_srid(query) != 0) {
            STBox *normalized = stbox_set_srid(query, 0);
            if (normalized) {
                memcpy(state->query_box, normalized, blob_size);
                free(normalized);
            }
        }
    }

    if (sptree_) {
        state->cursor = sptree_nn_cursor_open(sptree_, state->query_box);
    }
    return std::move(state);
}

bool TSPTreeIndex::NNScanNext(IndexScanState &state, row_t &row_id, double &lower_bound) const {
    auto &nstate = state.Cast<TSPTreeNNScanState>();
    if (!nstate.cursor) {
        return false;
    }
    int64 id = 0;
    double distance = 0;
    if (!sptree_nn_cursor_next(nstate.cursor, &id, &distance)) {
        return false;
    }
    row_id = static_cast<row_t>(id);
    lower_bound = distance;
    return true;
}

vector<row_t> TSPTreeIndex::Search(const void *query_box, RTreeSearchOp op) const {
    vector<row_t> results;
    
    if (!sptree_ || !query_box) {
        return results;
    }

    MeosArray *ids = meos_array_create(sizeof(int64));

    try {
        int count = sptree_search(sptree_, op, query_box, ids);

        if (count > 0) {
            results.reserve(count);
            for (int i = 0; i < count; i++) {
                const auto row_id = static_cast<row_t>(*(int64 *) meos_array_get(ids, i));
                // Deleted rows are still in the tree; they are filtered here.
                if (deleted_.find(row_id) != deleted_.end()) {
                    continue;
                }
                results.push_back(row_id);
            }
        }
    } catch (...) {
        fprintf(stderr, "Exception during sptree_search\n");
    }

    meos_array_destroy(ids);
    
    return results;
}
//------------------------------------------------------------------------------
// Required BoundIndex Interface Methods
//------------------------------------------------------------------------------

void TSPTreeIndex::CommitDrop(IndexLock &index_lock) {
    if (sptree_) {
        sptree_free(sptree_);
        sptree_ = nullptr;
    }
    if (entry_allocator_) {
        entry_allocator_->Reset();
    }
    deleted_.clear();
    entry_head_.Clear();
    entry_tail_.Clear();
    tail_count_ = 0;
    entry_count_ = 0;
}

bool TSPTreeIndex::MergeIndexes(IndexLock &state, BoundIndex &other_index) {
    return false;
}

void TSPTreeIndex::Vacuum(IndexLock &lock) {
}

idx_t TSPTreeIndex::GetInMemorySize(IndexLock &state) {
    return sptree_ ? 1024 : 0;
}

string TSPTreeIndex::VerifyAndToString(IndexLock &state, const bool only_verify) {
    if (!sptree_) {
        return "Stbox space-partitioning tree Index (not initialized)";
    }
    
    return "Stbox space-partitioning tree Index (MEOS-based)";
}

void TSPTreeIndex::VerifyAllocations(IndexLock &lock) {}

void TSPTreeIndex::VerifyBuffers(IndexLock &lock) {
    entry_allocator_->VerifyBuffers();
}

string TSPTreeIndex::GetConstraintViolationMessage(VerifyExistenceType verify_type, idx_t failed_index,
                                               DataChunk &input) {
    return "Stbox space-partitioning tree constraint violation (interval index does not support constraints)";
}

bool TSPTreeIndex::TryMatchDistanceFunction(const unique_ptr<Expression> &expr,
                                         vector<reference<Expression>> &bindings) const {

    bool match_result = function_matcher->Match(*expr, bindings);

    return match_result;
}

bool TSPTreeIndex::TryMatchNearestFunction(Expression &expr,
                                           vector<reference<Expression>> &bindings) const {
    return nearest_matcher && nearest_matcher->Match(expr, bindings);
}

unique_ptr<ExpressionMatcher> TSPTreeIndex::MakeNearestMatcher() const {
    // `|=|` measures a distance to a box, and only the two box kinds below are boxes it is
    // defined against; an index over a span stores no box a distance can be taken from.
    LogicalType box_type;
    if (bbox_type_ == T_STBOX) {
        box_type = StboxType::stbox();
    } else if (bbox_type_ == T_TBOX) {
        box_type = TboxType::tbox();
    } else {
        return nullptr;
    }

    auto matcher = make_uniq<FunctionExpressionMatcher>();
    matcher->function = make_uniq<SpecificFunctionMatcher>(TSPTreeIndexScanBindData::NN_OPERATION);
    matcher->expr_type = make_uniq<SpecificExpressionTypeMatcher>(ExpressionType::BOUND_FUNCTION);
    matcher->policy = SetMatcher::Policy::UNORDERED;

    // The indexed column on one side, the box on the other. The box operand is matched on its
    // TYPE, not merely on being a constant: `|=|` is defined between two temporal values as well,
    // and those are blobs too, so an unconstrained operand would let the scan read a temporal
    // value's bytes as a box.
    auto column_matcher = make_uniq<ExpressionMatcher>();
    column_matcher->type = make_uniq<SpecificTypeMatcher>(column_type_);
    matcher->matchers.push_back(std::move(column_matcher));

    auto box_matcher = make_uniq<ExpressionMatcher>();
    box_matcher->type = make_uniq<SpecificTypeMatcher>(box_type);
    matcher->matchers.push_back(std::move(box_matcher));

    return std::move(matcher);
}

unique_ptr<ExpressionMatcher> TSPTreeIndex::MakeFunctionMatcher() const {
    unordered_set<string> supported_functions;

    if (bbox_type_ == T_STBOX) {
        supported_functions = {"&&"};
    } else if (bbox_type_ == T_TSTZSPAN) {
        supported_functions = {"&&", "@>"};
    } else {
        supported_functions = {"&&"};
    }

    auto matcher = make_uniq<FunctionExpressionMatcher>();
    matcher->function = make_uniq<ManyFunctionMatcher>(supported_functions);
    matcher->expr_type = make_uniq<SpecificExpressionTypeMatcher>(ExpressionType::BOUND_FUNCTION);
    matcher->policy = SetMatcher::Policy::UNORDERED;

    LogicalType index_type;
    if (bbox_type_ == T_STBOX) {
        index_type = StboxType::stbox();
    } else if (bbox_type_ == T_TSTZSPAN) {
        index_type = SpanTypes::tstzspan();
    } else {
        index_type = LogicalType::BLOB;
    }

    // Left operand
    auto lhs_matcher = make_uniq<ExpressionMatcher>();
    lhs_matcher->type = make_uniq<SpecificTypeMatcher>(column_type_);
    matcher->matchers.push_back(std::move(lhs_matcher));

    // Right operand
    auto rhs_matcher = make_uniq<ExpressionMatcher>();
    matcher->matchers.push_back(std::move(rhs_matcher));

    return std::move(matcher);
}

//------------------------------------------------------------------------------
// Module Registration
//------------------------------------------------------------------------------

void TSPTreeModule::RegisterSPTreeIndex(ExtensionLoader &loader) {

    IndexType index_type;

    index_type.name = TSPTreeIndex::TYPE_NAME;
    index_type.create_instance = TSPTreeIndex::Create;
    index_type.create_plan = TSPTreeIndex::CreatePlan;

    loader.GetDatabaseInstance().config.GetIndexTypes().RegisterIndexType(index_type);
}

} 
