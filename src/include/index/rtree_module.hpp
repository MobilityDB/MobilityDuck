#pragma once

#include "duckdb/execution/index/bound_index.hpp"
#include "duckdb/execution/index/index_pointer.hpp"
#include "duckdb/execution/index/fixed_size_allocator.hpp"
#include "duckdb/common/case_insensitive_map.hpp"
#include "duckdb/optimizer/matcher/expression_matcher.hpp"

extern "C" {
    #include <meos.h>
}

namespace duckdb {

class RTreeIndex : public BoundIndex {
public:
    static constexpr const char *TYPE_NAME = "TRTREE";

    RTreeIndex(const string &name, IndexConstraintType constraint_type,
               const vector<column_t> &column_ids, TableIOManager &table_io_manager,
               const vector<unique_ptr<Expression>> &unbound_expressions,
               AttachedDatabase &db,
               const case_insensitive_map_t<Value> &options,
               const IndexStorageInfo &info);

    ~RTreeIndex();

    static unique_ptr<BoundIndex> Create(CreateIndexInput &input) {
		auto res = make_uniq<RTreeIndex>(input.name, input.constraint_type, input.column_ids, input.table_io_manager,
		                                 input.unbound_expressions, input.db, input.options, input.storage_info);
		return std::move(res);
	}

    static PhysicalOperator &CreatePlan(PlanIndexInput &input);

    ErrorData Insert(IndexLock &lock, DataChunk &data, Vector &row_ids) override;

    ErrorData BulkConstruct(STBox* boxes, const row_t* row_ids, idx_t count) ;

    void Delete(IndexLock &lock, DataChunk &entries, Vector &row_identifiers) override;

    ErrorData Append(IndexLock &lock, DataChunk &entries, Vector &row_identifiers) override;

    void Construct(DataChunk &expression_result, Vector &row_identifiers);

    //! Commit a drop operation
    void CommitDrop(IndexLock &index_lock) override;

    bool MergeIndexes(IndexLock &state, BoundIndex &other_index) override;

    void Vacuum(IndexLock &lock) override;

    idx_t GetInMemorySize(IndexLock &state) override;

    string VerifyAndToString(IndexLock &state, const bool only_verify) override;

    void VerifyAllocations(IndexLock &lock) override;

    string GetConstraintViolationMessage(VerifyExistenceType verify_type, idx_t failed_index,
                                       DataChunk &input) override;

    unique_ptr<IndexScanState> InitializeScan(const void* query_blob, size_t blob_size) const;

    vector<row_t> SearchStbox(const STBox *query_stbox) const;


    idx_t Scan(IndexScanState &state, Vector &result) const;

    bool TryMatchDistanceFunction(const unique_ptr<Expression> &expr, vector<reference<Expression>> &bindings) const;



private:
    case_insensitive_map_t<Value> options_;
    
    unique_ptr<ExpressionMatcher> function_matcher;
    unique_ptr<ExpressionMatcher> MakeFunctionMatcher() const;

    RTree *rtree_;
    STBox *boxes;
    size_t current_size_ = 0;
    size_t current_capacity_ = 0;
    StorageLock rwlock;
    atomic<idx_t> index_size = {0};

};

struct RTreeModule {
	static void RegisterRTreeIndex(DatabaseInstance &instance);
    static void RegisterIndexScan(DatabaseInstance &instance);
    static void RegisterScanOptimizer(DatabaseInstance &instance);
};

} // namespace duckdb