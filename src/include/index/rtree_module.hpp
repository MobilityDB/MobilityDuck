#pragma once

#include "duckdb/execution/index/bound_index.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/execution/index/index_pointer.hpp"
#include "duckdb/execution/index/fixed_size_allocator.hpp"
#include "duckdb/common/case_insensitive_map.hpp"
#include "duckdb/optimizer/matcher/expression_matcher.hpp"
#include "meos_wrapper_simple.hpp"

extern "C" {
    #include <meos.h>
}

namespace duckdb {

class TRTreeIndex : public BoundIndex {
public:
    static constexpr const char *TYPE_NAME = "TRTREE";

    TRTreeIndex(const string &name, IndexConstraintType constraint_type,
               const vector<column_t> &column_ids, TableIOManager &table_io_manager,
               const vector<unique_ptr<Expression>> &unbound_expressions,
               AttachedDatabase &db,
               const case_insensitive_map_t<Value> &options,
               const IndexStorageInfo &info);

    ~TRTreeIndex();

    static unique_ptr<BoundIndex> Create(CreateIndexInput &input) {
		auto res = make_uniq<TRTreeIndex>(input.name, input.constraint_type, input.column_ids, input.table_io_manager,
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

    unique_ptr<IndexScanState> InitializeScan(const void* query_blob, size_t blob_size, const string &operation) const;

    vector<row_t> Search(const void *query_box, RTreeSearchOp op) const;


    idx_t Scan(IndexScanState &state, Vector &result) const;

    //! Open a nearest-neighbour scan of the index around `query_blob`. Unlike InitializeScan,
    //! which materialises every hit up front, this reads the tree incrementally: the caller
    //! stops when it has enough, and the rest of the tree is never visited.
    unique_ptr<IndexScanState> InitializeNNScan(const void *query_blob, size_t blob_size) const;

    //! The next row of a nearest-neighbour scan, in order of the distance to the STORED BOX.
    //! That distance is a LOWER BOUND on the distance to the value the row holds, so it orders
    //! the candidates but does not rank them: a caller that needs the true nearest must
    //! recompute the distance and keep reading while `lower_bound` stays below the k-th exact
    //! distance it holds. Returns false once the index is exhausted.
    bool NNScanNext(IndexScanState &state, row_t &row_id, double &lower_bound) const;

    bool TryMatchDistanceFunction(const unique_ptr<Expression> &expr, vector<reference<Expression>> &bindings) const;

    //! Whether the expression is the nearest-neighbour distance between this index's column and a
    //! box of the kind the index stores. Kept apart from the matcher above: a distance an ORDER BY
    //! ranks by and a predicate the index answers are different questions, and one matcher
    //! answering both would admit `|=|` into the filter path, where the scan evaluates no filter
    //! and would drop it. Returns false for an index whose boxes no `|=|` is defined against.
    bool TryMatchNearestFunction(Expression &expr, vector<reference<Expression>> &bindings) const;

    //! The table columns this index was built over; the nearest-neighbour recheck reads the
    //! first of them to recompute a distance the query orders by but need not select.
    const vector<column_t> &GetIndexedColumns() const { return column_ids; }

    MeosType GetBboxType() const { return bbox_type_; }
    size_t GetBboxSize() const { return bbox_size_; }



private:
    case_insensitive_map_t<Value> options_;

    unique_ptr<ExpressionMatcher> function_matcher;
    unique_ptr<ExpressionMatcher> MakeFunctionMatcher() const;

    unique_ptr<ExpressionMatcher> nearest_matcher;
    unique_ptr<ExpressionMatcher> MakeNearestMatcher() const;

    RTree *rtree_;
    void *boxes;

    MeosType bbox_type_;
    size_t bbox_size_;
    LogicalType column_type_;

    size_t current_size_ = 0;
    size_t current_capacity_ = 0;
    StorageLock rwlock;
    atomic<idx_t> index_size = {0};

};

struct TRTreeModule {
	static void RegisterRTreeIndex(ExtensionLoader &loader);
    static void RegisterIndexScan(ExtensionLoader &loader);
    static void RegisterScanOptimizer(ExtensionLoader &loader);
    static void RegisterJoinOptimizer(ExtensionLoader &loader);
};

} // namespace duckdb
