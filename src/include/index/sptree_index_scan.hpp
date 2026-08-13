#pragma once

#include "duckdb/function/table_function.hpp"
#include "duckdb/function/table/table_scan.hpp"

namespace duckdb {

class Index;

struct TSPTreeIndexScanBindData : public TableFunctionData {
    DuckTableEntry &table;
    TSPTreeIndex &index;
    idx_t limit;
    unique_ptr<void, void(*)(void*)> query_box;  
    size_t query_box_size;  
    string operation;
    
    TSPTreeIndexScanBindData(DuckTableEntry &table, TSPTreeIndex &index, idx_t limit, 
                           void* query_box_ptr, size_t box_size, const string &operation)
        : table(table), index(index), limit(limit), 
          query_box(query_box_ptr, free), query_box_size(box_size), operation(operation) {}

};

struct TSPTreeIndexScanFunction {
	static TableFunction GetFunction();
};

} 
