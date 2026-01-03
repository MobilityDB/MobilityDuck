#include "meos_wrapper_simple.hpp"

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
#include "geo/stbox.hpp"
#include "index/rtree_index_create_physical.hpp"
#include "time_util.hpp"


namespace duckdb {

//------------------------------------------------------------------------------
// RTreeIndex Implementation with MEOS RTree Integration
//------------------------------------------------------------------------------

RTreeIndex::RTreeIndex(const string &name, IndexConstraintType constraint_type,
                       const vector<column_t> &column_ids, TableIOManager &table_io_manager,
                       const vector<unique_ptr<Expression>> &unbound_expressions,
                       AttachedDatabase &db,
                       const case_insensitive_map_t<Value> &options,
                       const IndexStorageInfo &info)
    : BoundIndex(name, TYPE_NAME, constraint_type, column_ids, table_io_manager, 
                unbound_expressions, db), options_(options), rtree_(nullptr) {
    
    
    auto &type = unbound_expressions[0]->return_type;
    
    if (type == StboxType::STBOX()) {
        bbox_type_ = T_STBOX;
        bbox_size_ = sizeof(STBox);
        rtree_ = rtree_create_stbox();
    } else if (type == SpanTypes::TSTZSPAN()) {
        bbox_type_ = T_TSTZSPAN;
        bbox_size_ = sizeof(Span);  
        rtree_ = rtree_create_tstzspan();
    } else {
        throw InternalException("RTree index only supports STBOX and TSTZSPAN types, got: " + type.ToString());
    }
    
    if (!rtree_) {
        throw InternalException("Failed to create MEOS RTree");
    }
    
    function_matcher = MakeFunctionMatcher();
}

class RTreeIndexScanState final : public IndexScanState {
public:
    void* query_box = nullptr; 
    vector<row_t> search_results;
    idx_t current_position = 0;
    bool initialized = false;
    
    ~RTreeIndexScanState() {
        if (query_box) {
            free(query_box);
            query_box = nullptr;
        }
    }
};

RTreeIndex::~RTreeIndex() {
    if (rtree_) {
        rtree_free(rtree_);
        rtree_ = nullptr;
    }

}

PhysicalOperator &RTreeIndex::CreatePlan(PlanIndexInput &input) {
    auto &create_index = input.op;
    auto &planner = input.planner;

    vector<LogicalType> new_column_types;
    vector<unique_ptr<Expression>> select_list;
    
    for (auto &expression : create_index.expressions) {
        new_column_types.push_back(expression->return_type);
        select_list.push_back(std::move(expression));
    }
    
    // new_column_types.emplace_back(LogicalType::ROW_TYPE);
    // select_list.push_back(
    //     make_uniq<BoundReferenceExpression>(LogicalType::ROW_TYPE, create_index.info->scan_types.size() - 1));

    auto &projection = planner.Make<PhysicalProjection>(new_column_types, std::move(select_list), 
                                                       create_index.estimated_cardinality);
    projection.children.push_back(input.table_scan);


    auto &physical_create_index = planner.Make<PhysicalCreateRTreeIndex>(
        create_index.types, create_index.table, create_index.info->column_ids, 
        std::move(create_index.info), std::move(create_index.unbound_expressions), 
        create_index.estimated_cardinality);
    
    physical_create_index.children.push_back(projection);
    return physical_create_index;
    return input.table_scan;
}

//------------------------------------------------------------------------------
// Core RTree Operations using MEOS
//------------------------------------------------------------------------------
ErrorData RTreeIndex::Insert(IndexLock &lock, DataChunk &data, Vector &row_ids) {
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

ErrorData RTreeIndex::Append(IndexLock &lock, DataChunk &appended_data, Vector &row_identifiers) {
    
    DataChunk expression_result;
    expression_result.Initialize(Allocator::DefaultAllocator(), logical_types);

    ExecuteExpressions(appended_data, expression_result);

    Construct(expression_result, row_identifiers);
    
    return ErrorData();
}

void RTreeIndex::Construct(DataChunk &expression_result, Vector &row_identifiers) {
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
    
    auto vector_type = vector.GetType();
    

    void* boxes = malloc(bbox_size_ * expression_result.size());
    
    for (idx_t i = 0; i < expression_result.size(); i++) {
        if (FlatVector::IsNull(vector, i)) {
            continue; 
        }

        void *box = nullptr;
        
        if (vector_type.id() == LogicalTypeId::BLOB) {
            auto blob_data = FlatVector::GetData<string_t>(vector)[i];
            const uint8_t *data = reinterpret_cast<const uint8_t*>(blob_data.GetData());
            size_t data_size = blob_data.GetSize();
            
           
            if (data_size != bbox_size_) {
                continue;
            }
                        
            box = malloc(data_size);
            memcpy(box, data, data_size);

            if (bbox_type_ == T_STBOX) {
                STBox *stbox = (STBox*)box;
                int32_t box_srid = stbox_srid(stbox);
                if (box_srid != 0) {
                    STBox *normalized_box = stbox_set_srid(stbox, 0);
                    if (normalized_box) {
                        free(box);
                        box = normalized_box;
                    }
                }
            }
        } else { 
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
}


ErrorData RTreeIndex::BulkConstruct(STBox* boxes, const row_t* row_ids, idx_t count) {
    if (!rtree_) {
        return ErrorData("RTree not initialized");
    }

    for (idx_t i = 0; i < count; i++) {
        rtree_insert(rtree_, &boxes[i], static_cast<int64_t>(row_ids[i]));
    }

    return ErrorData();
}

void RTreeIndex::Delete(IndexLock &lock, DataChunk &entries, Vector &row_identifiers) {

    throw NotImplementedException("RTree deletion not implemented - consider rebuilding index");
}
//------------------------------------------------------------------------------
// RTree Search Operations
//------------------------------------------------------------------------------
unique_ptr<IndexScanState> RTreeIndex::InitializeScan(const void* query_blob, size_t blob_size, const string &operation) const {
    const uint8_t *data = reinterpret_cast<const uint8_t*>(query_blob);
    
    auto state = make_uniq<RTreeIndexScanState>();
    
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
        state->search_results = Search(state->query_box);
        state->initialized = true;
    } 
    
    state->current_position = 0;
    
    return std::move(state);
}

idx_t RTreeIndex::Scan(IndexScanState &state, Vector &result) const {
    auto &sstate = state.Cast<RTreeIndexScanState>();
    
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

vector<row_t> RTreeIndex::Search(const void *query_box) const {  
    vector<row_t> results;
    
    if (!rtree_ || !query_box) {
        return results;
    }

    int count = 0;
    int *ids = nullptr;
    
    try {
        ids = rtree_search(rtree_, query_box, &count);
        
        if (ids && count > 0) {
            results.reserve(count);
            for (int i = 0; i < count; i++) {
                results.push_back(static_cast<row_t>(ids[i]));
            }
        }
    } catch (...) {
        fprintf(stderr, "Exception during rtree_search\n");
    }
    
    if (ids) {
        free(ids);
    }
    
    return results;
}
//------------------------------------------------------------------------------
// Required BoundIndex Interface Methods
//------------------------------------------------------------------------------

void RTreeIndex::CommitDrop(IndexLock &index_lock) {
    if (rtree_) {
        rtree_free(rtree_);
        rtree_ = nullptr;
    }
}

bool RTreeIndex::MergeIndexes(IndexLock &state, BoundIndex &other_index) {
    return false;
}

void RTreeIndex::Vacuum(IndexLock &lock) {
}

idx_t RTreeIndex::GetInMemorySize(IndexLock &state) {
    return rtree_ ? 1024 : 0;
}

string RTreeIndex::VerifyAndToString(IndexLock &state, const bool only_verify) {
    if (!rtree_) {
        return "Stbox R-tree Index (not initialized)";
    }
    
    return "Stbox R-tree Index (MEOS-based)";
}

void RTreeIndex::VerifyAllocations(IndexLock &lock) {}

string RTreeIndex::GetConstraintViolationMessage(VerifyExistenceType verify_type, idx_t failed_index,
                                               DataChunk &input) {
    return "Stbox R-tree constraint violation (interval index does not support constraints)";
}

bool RTreeIndex::TryMatchDistanceFunction(const unique_ptr<Expression> &expr,
                                         vector<reference<Expression>> &bindings) const {

    bool match_result = function_matcher->Match(*expr, bindings);
    
    return match_result;
}

unique_ptr<ExpressionMatcher> RTreeIndex::MakeFunctionMatcher() const {
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
        index_type = StboxType::STBOX();
    } else if (bbox_type_ == T_TSTZSPAN) {
        index_type = SpanTypes::TSTZSPAN();
    } else {
        index_type = LogicalType::BLOB;
    }

    // Left operand
    auto lhs_matcher = make_uniq<ExpressionMatcher>();
    lhs_matcher->type = make_uniq<SpecificTypeMatcher>(index_type); 
    matcher->matchers.push_back(std::move(lhs_matcher));

    // Right operand
    auto rhs_matcher = make_uniq<ExpressionMatcher>();
    matcher->matchers.push_back(std::move(rhs_matcher));

    return std::move(matcher);
}

//------------------------------------------------------------------------------
// Module Registration
//------------------------------------------------------------------------------

void RTreeModule::RegisterRTreeIndex(DatabaseInstance &db) {

    IndexType index_type;
    
    index_type.name = RTreeIndex::TYPE_NAME;
    index_type.create_instance = RTreeIndex::Create;
    index_type.create_plan = RTreeIndex::CreatePlan;
    
    db.config.GetIndexTypes().RegisterIndexType(index_type);
}

} 