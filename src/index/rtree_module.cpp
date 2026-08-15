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
#include "index/rtree_module.hpp"
#include "index/rtree_index_scan.hpp"
#include "geo/stbox.hpp"
#include "geo/tgeompoint.hpp"
#include "geo/tgeometry.hpp"
#include "geo/tgeography.hpp"
#include "geo/tgeogpoint.hpp"
#include "temporal/span.hpp"
#include "temporal/tbox.hpp"
#include "temporal/temporal.hpp"
#include "index/rtree_index_create_physical.hpp"
#include "time_util.hpp"


namespace duckdb {

//------------------------------------------------------------------------------
// RTreeIndex Implementation with MEOS RTree Integration
//------------------------------------------------------------------------------

TRTreeIndex::TRTreeIndex(const string &name, IndexConstraintType constraint_type,
                       const vector<column_t> &column_ids, TableIOManager &table_io_manager,
                       const vector<unique_ptr<Expression>> &unbound_expressions,
                       AttachedDatabase &db,
                       const case_insensitive_map_t<Value> &options,
                       const IndexStorageInfo &info)
    : BoundIndex(name, TYPE_NAME, constraint_type, column_ids, table_io_manager, 
                unbound_expressions, db), options_(options), rtree_(nullptr) {
    
    
    auto &type = unbound_expressions[0]->return_type;
    column_type_ = type;

    // R-tree's bbox type is determined by the type of the indexed column
    
    if (type == StboxType::stbox()) {
        bbox_type_ = T_STBOX;
        bbox_size_ = sizeof(STBox);
        rtree_ = rtree_create_stbox();
    } else if (type == SpanTypes::tstzspan()) {
        bbox_type_ = T_TSTZSPAN;
        bbox_size_ = sizeof(Span);  
        rtree_ = rtree_create_tstzspan();
    } else if (type == TboxType::tbox()) {
        bbox_type_ = T_TBOX;
        bbox_size_ = sizeof(TBox);
        rtree_ = rtree_create_tbox();
    } else if (type == SpanTypes::intspan()) {
        bbox_type_ = T_INTSPAN;
        bbox_size_ = sizeof(Span);
        rtree_ = rtree_create_intspan();
    } else if (type == SpanTypes::bigintspan()) {
        bbox_type_ = T_BIGINTSPAN;
        bbox_size_ = sizeof(Span);
        rtree_ = rtree_create_bigintspan();
    } else if (type == SpanTypes::floatspan()) {
        bbox_type_ = T_FLOATSPAN;
        bbox_size_ = sizeof(Span);
        rtree_ = rtree_create_floatspan();
    } else if (type == SpanTypes::datespan()) {
        bbox_type_ = T_DATESPAN;
        bbox_size_ = sizeof(Span);
        rtree_ = rtree_create_datespan();
    } else if (type == TemporalTypes::tint() || type == TemporalTypes::tfloat()) {
        // Temporal numbers: the bounding box is a tbox.
        bbox_type_ = T_TBOX;
        bbox_size_ = sizeof(TBox);
        rtree_ = rtree_create_tbox();
    } else if (type == TemporalTypes::tbool() || type == TemporalTypes::ttext()) {
        // Non-numeric, non-spatial temporals: the bounding box is the time span.
        bbox_type_ = T_TSTZSPAN;
        bbox_size_ = sizeof(Span);
        rtree_ = rtree_create_tstzspan();
    } else if (type == TgeompointType::tgeompoint() ||
               type == TGeometryTypes::tgeometry() ||
               type == TGeographyTypes::tgeography() ||
               type == TGeogpointType::tgeogpoint()) {
        // Temporal spatial types: the bounding box is an stbox.
        bbox_type_ = T_STBOX;
        bbox_size_ = sizeof(STBox);
        rtree_ = rtree_create_stbox();
    } else {
        throw BinderException(
            "TRTREE index supports stbox, tbox, the five span types, and the "
            "temporal types (tint, tfloat, tbool, ttext, tgeompoint, tgeogpoint, "
            "tgeometry, tgeography). Got: " + type.ToString());
    }
    
    if (!rtree_) {
        throw InternalException("Failed to create MEOS RTree");
    }
    
    function_matcher = MakeFunctionMatcher();
    nearest_matcher = MakeNearestMatcher();
}

class TRTreeIndexScanState final : public IndexScanState {
public:
    void* query_box = nullptr;
    vector<row_t> search_results;
    idx_t current_position = 0;
    bool initialized = false;

    ~TRTreeIndexScanState() {
        if (query_box) {
            free(query_box);
            query_box = nullptr;
        }
    }
};

//! A nearest-neighbour scan holds the tree open: the cursor keeps the priority queue of the
//! best-first traversal, so each call resumes where the last stopped instead of restarting.
class TRTreeNNScanState final : public IndexScanState {
public:
    void *query_box = nullptr;
    RTreeNNCursor *cursor = nullptr;

    ~TRTreeNNScanState() {
        if (cursor) {
            rtree_nn_cursor_close(cursor);
            cursor = nullptr;
        }
        if (query_box) {
            free(query_box);
            query_box = nullptr;
        }
    }
};

TRTreeIndex::~TRTreeIndex() {
    if (rtree_) {
        rtree_free(rtree_);
        rtree_ = nullptr;
    }

}

PhysicalOperator &TRTreeIndex::CreatePlan(PlanIndexInput &input) {
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

    auto &physical_create_index = planner.Make<PhysicalCreateTRTreeIndex>(
        create_index.types, create_index.table, create_index.info->column_ids,
        std::move(create_index.info), std::move(create_index.unbound_expressions),
        create_index.estimated_cardinality);

    physical_create_index.children.push_back(projection);
    return physical_create_index;
}

//------------------------------------------------------------------------------
// Core RTree Operations using MEOS
//------------------------------------------------------------------------------
ErrorData TRTreeIndex::Insert(IndexLock &lock, DataChunk &data, Vector &row_ids) {
    if (!rtree_) {
        return ErrorData("RTree not initialized");
    }
    
    if (data.size() == 0 || data.ColumnCount() == 0) {
        return ErrorData(); 
    }
    DataChunk expression_result;
    expression_result.Initialize(Allocator::DefaultAllocator(), logical_types);
    
    ExecuteExpressions(data, expression_result);
    
    auto &stbox_vector = expression_result.data[0]; 
    auto row_data = FlatVector::GetData<row_t>(row_ids);

    if (stbox_vector.GetVectorType() != VectorType::FLAT_VECTOR) {
        stbox_vector.Flatten(expression_result.size());
    }
    
    auto vector_type = stbox_vector.GetType();
    
    boxes = (STBox*)malloc(sizeof(STBox) * expression_result.size());
    
    for (idx_t i = 0; i < expression_result.size(); i++) {
        if (FlatVector::IsNull(stbox_vector, i)) {
            continue; 
        }
        
        STBox *box = nullptr;
        
        if (vector_type.id() == LogicalTypeId::BLOB ) {
            auto blob_data = FlatVector::GetData<string_t>(stbox_vector)[i];

            std::string s = blob_data.GetString();
            const uint8_t *stbox_data = reinterpret_cast<const uint8_t*>(blob_data.GetData());
            size_t stbox_size = blob_data.GetSize();
                        
            box = (STBox*)malloc(stbox_size);
            
            memcpy(box, stbox_data, stbox_size);
            
            int32_t box_srid = stbox_srid(box);
            if (box_srid != 0) {
                STBox *normalized_box = stbox_set_srid(box, 0);
                if (normalized_box) {
                    free(box);
                    box = normalized_box;
                }
            }
        } 
        else { 
            continue;
        }
        
        if (box == nullptr) {
            continue;
        }
        
        void* target = (char*)boxes + (i * bbox_size_);
        memcpy(target, box, bbox_size_);
        
        rtree_insert(rtree_, target, static_cast<int64_t>(row_data[i]));
        
        free(box);
    }

    free(boxes);
    
    return ErrorData();
}

ErrorData TRTreeIndex::Append(IndexLock &lock, DataChunk &appended_data, Vector &row_identifiers) {
    
    DataChunk expression_result;
    expression_result.Initialize(Allocator::DefaultAllocator(), logical_types);

    ExecuteExpressions(appended_data, expression_result);

    Construct(expression_result, row_identifiers);
    
    return ErrorData();
}

void TRTreeIndex::Construct(DataChunk &expression_result, Vector &row_identifiers) {
    if (!rtree_) {
        throw InternalException("RTree not initialized");
    }
    
    if (expression_result.size() == 0 || expression_result.ColumnCount() == 0) {
        return; 
    }
    
    auto &vector = expression_result.data[0];
    auto row_data = FlatVector::GetData<row_t>(row_identifiers);

    if (vector.GetVectorType() != VectorType::FLAT_VECTOR) {
        vector.Flatten(expression_result.size());
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

    void *boxes = indexes_temporal ? nullptr : malloc(bbox_size_ * expression_result.size());
    
    for (idx_t i = 0; i < expression_result.size(); i++) {
        if (FlatVector::IsNull(vector, i)) {
            continue;
        }

        if (vector.GetType().id() != LogicalTypeId::BLOB) {
            continue;
        }

        auto blob_data = FlatVector::GetData<string_t>(vector)[i];
        const uint8_t *data = reinterpret_cast<const uint8_t *>(blob_data.GetData());
        size_t data_size = blob_data.GetSize();

        if (indexes_temporal) {
            const Temporal *temp = reinterpret_cast<const Temporal *>(data);
            if (bbox_type_ == T_STBOX) {
                STBox *box = tspatial_to_stbox(temp);
                if (!box) {
                    continue;
                }

                if (stbox_srid(box) != 0) {
                    STBox *normalized = stbox_set_srid(box, 0);
                    if (normalized) {
                        free(box);
                        box = normalized;
                    }
                }
                rtree_insert(rtree_, box, static_cast<int>(row_data[i]));
                free(box);
            } else {
                rtree_insert_temporal(rtree_, temp, static_cast<int>(row_data[i]));
            }
            continue;
        }

        if (data_size != bbox_size_) {
            continue;
        }

        void *box = malloc(data_size);
        memcpy(box, data, data_size);

        if (bbox_type_ == T_STBOX) {
            STBox *stbox = (STBox *) box;
            int32_t box_srid = stbox_srid(stbox);
            if (box_srid != 0) {
                STBox *normalized_box = stbox_set_srid(stbox, 0);
                if (normalized_box) {
                    free(box);
                    box = normalized_box;
                }
            }
        }

        void *target = (char *) boxes + (i * bbox_size_);
        memcpy(target, box, bbox_size_);
        rtree_insert(rtree_, target, static_cast<int>(row_data[i]));
        free(box);
    }

    if (boxes) free(boxes);
}


ErrorData TRTreeIndex::BulkConstruct(STBox* boxes, const row_t* row_ids, idx_t count) {
    if (!rtree_) {
        return ErrorData("RTree not initialized");
    }

    for (idx_t i = 0; i < count; i++) {
        rtree_insert(rtree_, &boxes[i], static_cast<int64_t>(row_ids[i]));
    }

    return ErrorData();
}

void TRTreeIndex::Delete(IndexLock &lock, DataChunk &entries, Vector &row_identifiers) {

    throw NotImplementedException("RTree deletion not implemented - consider rebuilding index");
}
//------------------------------------------------------------------------------
// RTree Search Operations
//------------------------------------------------------------------------------
unique_ptr<IndexScanState> TRTreeIndex::InitializeScan(const void* query_blob, size_t blob_size, const string &operation) const {
    const uint8_t *data = reinterpret_cast<const uint8_t*>(query_blob);
    
    auto state = make_uniq<TRTreeIndexScanState>();
    
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
    
    if (rtree_) {
        /* MEOS rtree_search: @> uses containment, && uses overlap (see RTreeSearchOp in meos.h). */
        const RTreeSearchOp search_op = (operation == "@>") ? RTREE_CONTAINS : RTREE_OVERLAPS;
        state->search_results = Search(state->query_box, search_op);
        state->initialized = true;
    } 
    
    state->current_position = 0;
    
    return std::move(state);
}

idx_t TRTreeIndex::Scan(IndexScanState &state, Vector &result) const {
    auto &sstate = state.Cast<TRTreeIndexScanState>();
    
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

unique_ptr<IndexScanState> TRTreeIndex::InitializeNNScan(const void *query_blob, size_t blob_size) const {
    auto state = make_uniq<TRTreeNNScanState>();

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

    if (rtree_) {
        state->cursor = rtree_nn_cursor_open(rtree_, state->query_box);
    }
    return std::move(state);
}

bool TRTreeIndex::NNScanNext(IndexScanState &state, row_t &row_id, double &lower_bound) const {
    auto &nstate = state.Cast<TRTreeNNScanState>();
    if (!nstate.cursor) {
        return false;
    }
    int id = 0;
    double distance = 0;
    if (!rtree_nn_cursor_next(nstate.cursor, &id, &distance)) {
        return false;
    }
    row_id = static_cast<row_t>(id);
    lower_bound = distance;
    return true;
}

vector<row_t> TRTreeIndex::Search(const void *query_box, RTreeSearchOp op) const {
    vector<row_t> results;
    
    if (!rtree_ || !query_box) {
        return results;
    }

    MeosArray *ids = meos_array_create(sizeof(int));

    try {
        int count = rtree_search(rtree_, op, query_box, ids);

        if (count > 0) {
            results.reserve(count);
            for (int i = 0; i < count; i++) {
                results.push_back(static_cast<row_t>(*(int *) meos_array_get(ids, i)));
            }
        }
    } catch (...) {
        fprintf(stderr, "Exception during rtree_search\n");
    }

    meos_array_destroy(ids);
    
    return results;
}
//------------------------------------------------------------------------------
// Required BoundIndex Interface Methods
//------------------------------------------------------------------------------

void TRTreeIndex::CommitDrop(IndexLock &index_lock) {
    if (rtree_) {
        rtree_free(rtree_);
        rtree_ = nullptr;
    }
}

bool TRTreeIndex::MergeIndexes(IndexLock &state, BoundIndex &other_index) {
    return false;
}

void TRTreeIndex::Vacuum(IndexLock &lock) {
}

idx_t TRTreeIndex::GetInMemorySize(IndexLock &state) {
    return rtree_ ? 1024 : 0;
}

string TRTreeIndex::VerifyAndToString(IndexLock &state, const bool only_verify) {
    if (!rtree_) {
        return "Stbox R-tree Index (not initialized)";
    }
    
    return "Stbox R-tree Index (MEOS-based)";
}

void TRTreeIndex::VerifyAllocations(IndexLock &lock) {}

string TRTreeIndex::GetConstraintViolationMessage(VerifyExistenceType verify_type, idx_t failed_index,
                                               DataChunk &input) {
    return "Stbox R-tree constraint violation (interval index does not support constraints)";
}

bool TRTreeIndex::TryMatchDistanceFunction(const unique_ptr<Expression> &expr,
                                         vector<reference<Expression>> &bindings) const {

    bool match_result = function_matcher->Match(*expr, bindings);
    
    return match_result;
}

bool TRTreeIndex::TryMatchNearestFunction(Expression &expr,
                                          vector<reference<Expression>> &bindings) const {
    return nearest_matcher && nearest_matcher->Match(expr, bindings);
}

unique_ptr<ExpressionMatcher> TRTreeIndex::MakeNearestMatcher() const {
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
    matcher->function = make_uniq<SpecificFunctionMatcher>(TRTreeIndexScanBindData::NN_OPERATION);
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

unique_ptr<ExpressionMatcher> TRTreeIndex::MakeFunctionMatcher() const {
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

void TRTreeModule::RegisterRTreeIndex(ExtensionLoader &loader) {

    IndexType index_type;

    index_type.name = TRTreeIndex::TYPE_NAME;
    index_type.create_instance = TRTreeIndex::Create;
    index_type.create_plan = TRTreeIndex::CreatePlan;

    loader.GetDatabaseInstance().config.GetIndexTypes().RegisterIndexType(index_type);
}

} 
