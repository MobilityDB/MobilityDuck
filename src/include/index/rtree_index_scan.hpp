#pragma once

#include "duckdb/function/table_function.hpp"
#include "duckdb/function/table/table_scan.hpp"

namespace duckdb {

class Index;

struct TRTreeIndexScanBindData : public TableFunctionData {
    //! The operation of a nearest-neighbour scan, which reads the index incrementally and ranks
    //! its candidates by recomputing the distance; every other operation materialises its hits.
    static constexpr const char *NN_OPERATION = "|=|";

    DuckTableEntry &table;
    TRTreeIndex &index;
    idx_t limit;
    unique_ptr<void, void(*)(void*)> query_box;  
    size_t query_box_size;  
    string operation;
    
    TRTreeIndexScanBindData(DuckTableEntry &table, TRTreeIndex &index, idx_t limit, 
                           void* query_box_ptr, size_t box_size, const string &operation)
        : table(table), index(index), limit(limit), 
          query_box(query_box_ptr, free), query_box_size(box_size), operation(operation) {}

};

struct TRTreeIndexScanFunction {
	static TableFunction GetFunction();
};

} 
