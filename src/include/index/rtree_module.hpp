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

    //! Constructor
    RTreeIndex(const string &name, IndexConstraintType constraint_type,
               const vector<column_t> &column_ids, TableIOManager &table_io_manager,
               const vector<unique_ptr<Expression>> &unbound_expressions,
               AttachedDatabase &db,
               const case_insensitive_map_t<Value> &options,
               const IndexStorageInfo &info);

    //! Destructor
    ~RTreeIndex();

    //! Create method for index factory
    static unique_ptr<BoundIndex> Create(CreateIndexInput &input) {
		auto res = make_uniq<RTreeIndex>(input.name, input.constraint_type, input.column_ids, input.table_io_manager,
		                                 input.unbound_expressions, input.db, input.options, input.storage_info);
		return std::move(res);
	}

    //! Create physical plan for index scan
    static PhysicalOperator &CreatePlan(PlanIndexInput &input);

    //! Insert entries into the index
    ErrorData Insert(IndexLock &lock, DataChunk &data, Vector &row_ids) override;

    ErrorData BulkConstruct(STBox* boxes, const row_t* row_ids, idx_t count) ;

    //! Delete entries from the index
    void Delete(IndexLock &lock, DataChunk &entries, Vector &row_identifiers) override;

    //! Append entries to the index
    ErrorData Append(IndexLock &lock, DataChunk &entries, Vector &row_identifiers) override;

    //! Commit a drop operation
    void CommitDrop(IndexLock &index_lock) override;

    //! Merge two indexes
    bool MergeIndexes(IndexLock &state, BoundIndex &other_index) override;

    //! Vacuum the index
    void Vacuum(IndexLock &lock) override;

    //! Get the in-memory size of the index
    idx_t GetInMemorySize(IndexLock &state) override;

    //! Verify the index and convert to string
    string VerifyAndToString(IndexLock &state, const bool only_verify) override;

    //! Verify allocations
    void VerifyAllocations(IndexLock &lock) override;

    //! Get constraint violation message
    string GetConstraintViolationMessage(VerifyExistenceType verify_type, idx_t failed_index,
                                       DataChunk &input) override;

    unique_ptr<IndexScanState> InitializeScan(const void* query_blob, size_t blob_size) const;
    //! Search for stbox overlaps
    vector<row_t> SearchStbox(const STBox *query_stbox) const;


    idx_t Scan(IndexScanState &state, Vector &result) const;

    bool TryMatchDistanceFunction(const unique_ptr<Expression> &expr, vector<reference<Expression>> &bindings) const;



private:
    //! Index options
    case_insensitive_map_t<Value> options_;
    
    unique_ptr<ExpressionMatcher> function_matcher;
    unique_ptr<ExpressionMatcher> MakeFunctionMatcher() const;

    //! MEOS RTree instance for stbox indexing
    RTree *rtree_;
    STBox *boxes;
    size_t current_size_ = 0;
    size_t current_capacity_ = 0;

};




struct RTreeModule {
	static void RegisterRTreeIndex(DatabaseInstance &instance);
    static void RegisterIndexPragmas(DatabaseInstance &instance);
    static void RegisterIndexScan(DatabaseInstance &instance);
    static void RegisterScanOptimizer(DatabaseInstance &instance);
};

} // namespace duckdb