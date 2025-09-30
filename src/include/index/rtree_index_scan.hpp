#pragma once

#include "duckdb/function/table_function.hpp"
#include "duckdb/function/table/table_scan.hpp"

namespace duckdb {

class Index;

struct RTreeIndexScanBindData : public TableFunctionData {
    DuckTableEntry &table;
    RTreeIndex &index;
    idx_t limit;
    unique_ptr<STBox> query_stbox; 
    
    RTreeIndexScanBindData(DuckTableEntry &table, RTreeIndex &index, idx_t limit, 
                           unique_ptr<STBox> query_stbox)
        : table(table), index(index), limit(limit), query_stbox(std::move(query_stbox)) {}
};

struct RTreeIndexScanFunction {
	static TableFunction GetFunction();
};

} 