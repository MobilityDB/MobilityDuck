#pragma once

#include "duckdb/execution/index/bound_index.hpp"
#include "duckdb_version_compat.hpp"
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

class TSPTreeIndex : public BoundIndex {
public:
    static constexpr const char *TYPE_NAME = "TSPTREE";

    TSPTreeIndex(const string &name, IndexConstraintType constraint_type,
               const vector<column_t> &column_ids, TableIOManager &table_io_manager,
               const vector<unique_ptr<Expression>> &unbound_expressions,
               AttachedDatabase &db,
               const case_insensitive_map_t<Value> &options,
               const IndexStorageInfo &info);

    ~TSPTreeIndex();

    static unique_ptr<BoundIndex> Create(CreateIndexInput &input) {
		auto res = make_uniq<TSPTreeIndex>(input.name, input.constraint_type, input.column_ids, input.table_io_manager,
		                                 input.unbound_expressions, input.db, input.options, input.storage_info);
		return std::move(res);
	}

    static PhysicalOperator &CreatePlan(PlanIndexInput &input);

    ErrorData Insert(IndexLock &lock, DataChunk &data, Vector &row_ids) override;

    ErrorData BulkConstruct(STBox* boxes, const row_t* row_ids, idx_t count) ;

    void Delete(IndexLock &lock, DataChunk &entries, Vector &row_identifiers) override;

    ErrorData Append(IndexLock &lock, DataChunk &entries, Vector &row_identifiers) override;

    void Construct(DataChunk &expression_result, Vector &row_identifiers);

    //! Gather what Construct derives instead of inserting it, so that the whole
    //! entry set reaches the tree in one bulk build. Index creation scans the
    //! table in chunks; a tree grown chunk by chunk takes the depth its entries
    //! arrived in, which for an ordered column is one level per entry.
    void BeginBulkConstruct();
    //! Build the tree from everything gathered since BeginBulkConstruct.
    ErrorData FinishBulkConstruct();

    //! Commit a drop operation
    void CommitDrop(IndexLock &index_lock) override;

    bool MergeIndexes(IndexLock &state, BoundIndex &other_index) override;

    void Vacuum(IndexLock &lock) override;

    idx_t GetInMemorySize(IndexLock &state) override;

#if MOBILITYDUCK_DUCKDB_AT_LEAST(1, 5)
    // BoundIndex splits the one entry point in two from v1.5.
    void Verify(IndexLock &state) override;
    string ToString(IndexLock &state, bool display_ascii = false) override;
#else
    string VerifyAndToString(IndexLock &state, const bool only_verify) override;
#endif

    void VerifyAllocations(IndexLock &lock) override;

    void VerifyBuffers(IndexLock &lock) override;

    //! Writes the index to the database file, and returns the information
    //! required to read it back.
    IndexStorageInfo SerializeToDisk(QueryContext context, const case_insensitive_map_t<Value> &options) override;

    //! Returns the index buffers and the information required to read them back,
    //! for serialization to the WAL.
    IndexStorageInfo SerializeToWAL(const case_insensitive_map_t<Value> &options) override;

    string GetConstraintViolationMessage(VerifyExistenceType verify_type, idx_t failed_index,
                                       DataChunk &input) override;

    //! Open a scan of the index for `operation` against `query_blob`. `query_on_left` names the
    //! operand order of the predicate the scan answers, which decides the operation an operator
    //! whose two sides play different roles asks the index for (see index_search_ops.hpp).
    unique_ptr<IndexScanState> InitializeScan(const void* query_blob, size_t blob_size,
                                              const string &operation, bool query_on_left) const;

    vector<row_t> Search(const void *query_box, IndexSearchOp op) const;

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


    idx_t Scan(IndexScanState &state, Vector &result) const;

    bool TryMatchDistanceFunction(const unique_ptr<Expression> &expr, vector<reference<Expression>> &bindings) const;

    //! Whether the expression is the nearest-neighbour distance between this index's column and a
    //! box of the kind the index stores. Kept apart from the matcher above: a distance an ORDER BY
    //! ranks by and a predicate the index answers are different questions, and one matcher
    //! answering both would admit `|=|` into the filter path, where the scan evaluates no filter
    //! and would drop it. Returns false for an index whose boxes no `|=|` is defined against.
    bool TryMatchNearestFunction(Expression &expr, vector<reference<Expression>> &bindings) const;

    MeosType GetBboxType() const { return bbox_type_; }
    size_t GetBboxSize() const { return bbox_size_; }



private:
    //! The MEOS space-partitioning tree is an opaque C structure without a (de)serialization
    //! API, so the index cannot be persisted node by node the way the ART is.
    //! Instead, we keep the (bounding box, row id) pairs that were fed to the tree
    //! in a chain of fixed-size segments allocated through DuckDB's own
    //! FixedSizeAllocator. Those segments are what is written to the database file
    //! and to the WAL, and the MEOS tree is rebuilt from them on load.
    //!
    //! A segment starts with an 8-byte IndexPointer to the next segment (zero
    //! metadata terminates the chain), followed by an 8-byte entry count, and then
    //! the entries themselves. One entry is a row id followed by the raw bytes of
    //! its bounding box.
    static constexpr idx_t ENTRY_SEGMENT_HEADER_SIZE = 2 * sizeof(idx_t);
    //! The size we aim for when packing entries into a segment.
    static constexpr idx_t ENTRY_SEGMENT_TARGET_SIZE = 4096;

    //! Sets up the entry allocator and, if info holds a previously serialized
    //! chain, replays the entries into the (empty) MEOS tree.
    void InitEntryStorage(const IndexStorageInfo &info);
    //! Appends one (bounding box, row id) pair to the segment chain.
    void RecordEntry(const void *box, row_t row_id);
    //! Re-inserts all serialized entries into the MEOS tree.
    void ReplayEntries();
    //! Tombstones the persisted entries of the given row ids.
    void TombstoneEntries(const unordered_set<row_t> &removed);
    //! Marks a persisted entry as deleted. Real row ids are never negative.
    static constexpr row_t TOMBSTONE_ROW_ID = -1;
    //! The part of the serialization that the disk and the WAL path share.
    IndexStorageInfo PrepareSerialize(const case_insensitive_map_t<Value> &options);

    case_insensitive_map_t<Value> options_;

    unique_ptr<ExpressionMatcher> function_matcher;
    unique_ptr<ExpressionMatcher> MakeFunctionMatcher() const;

    unique_ptr<ExpressionMatcher> nearest_matcher;
    unique_ptr<ExpressionMatcher> MakeNearestMatcher() const;

    SPTree *sptree_;
    //! Which space-partitioning structure the tree uses, from the `kind` create
    //! option: quadtree divides on every dimension at once, kdtree on one
    //! dimension per level. Both answer the same queries over the same boxes.
    SPTreeKind kind_;

    MeosType bbox_type_;
    size_t bbox_size_;
    LogicalType column_type_;

    //! Row ids deleted since the tree was built. MEOS has no removal entry
    //! point, so the row stays in the tree and every search filters it out.
    unordered_set<row_t> deleted_;
    //! Serialized (bounding box, row id) entries backing the MEOS tree.
    unique_ptr<FixedSizeAllocator> entry_allocator_;
    //! First and last segment of the entry chain.
    IndexPointer entry_head_;
    IndexPointer entry_tail_;
    //! The size of one entry, of one segment, and the entries per segment.
    bool bulk_construct_ = false;
    vector<data_t> bulk_boxes_;
    vector<int64_t> bulk_ids_;
    idx_t entry_size_ = 0;
    idx_t entries_per_segment_ = 0;
    idx_t segment_size_ = 0;
    //! The number of entries in the last segment, and in the whole chain.
    idx_t tail_count_ = 0;
    idx_t entry_count_ = 0;

    size_t current_size_ = 0;
    size_t current_capacity_ = 0;
    StorageLock rwlock;
    atomic<idx_t> index_size = {0};

};

struct TSPTreeModule {
	static void RegisterSPTreeIndex(ExtensionLoader &loader);
    static void RegisterIndexScan(ExtensionLoader &loader);
    static void RegisterScanOptimizer(ExtensionLoader &loader);
    static void RegisterJoinOptimizer(ExtensionLoader &loader);
};

} // namespace duckdb
