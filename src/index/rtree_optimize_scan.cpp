#include "meos_wrapper_simple.hpp"
#include "duckdb/catalog/catalog_entry/duck_table_entry.hpp"
#include "duckdb/optimizer/optimizer_extension.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include "duckdb/planner/operator/logical_get.hpp"
#include "duckdb/planner/operator/logical_projection.hpp"
#include "duckdb/planner/operator/logical_top_n.hpp"
#include "duckdb/planner/operator/logical_filter.hpp"
#include "duckdb/planner/filter/expression_filter.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"
#include "duckdb/main/database.hpp"
#include <algorithm>

#include "index/rtree_module.hpp"
#include "index/rtree_index_scan.hpp"
#include "time_util.hpp"



namespace duckdb {

class RTreeIndexScanOptimizer : public OptimizerExtension {
public:
    RTreeIndexScanOptimizer() {
        optimize_function = Optimize;
    }

private:
    static bool TryOptimizeLogicalGet(ClientContext &context, unique_ptr<LogicalOperator> &plan) {
        
        auto &get = plan->Cast<LogicalGet>();

        if (get.function.name != "seq_scan" || !get.GetTable()->IsDuckTable()) {
            return false;
        }

        auto &duck_table = get.GetTable()->Cast<DuckTableEntry>();
        auto &table_info = *get.GetTable()->GetStorage().GetDataTableInfo();
        
        unique_ptr<RTreeIndexScanBindData> bind_data = nullptr;
        vector<reference<Expression>> bindings;

        for (auto &filter_pair : get.table_filters.filters) {
            auto &filter = filter_pair.second;
            
            table_info.GetIndexes().BindAndScan<RTreeIndex>(context, table_info, 
            [&](RTreeIndex &rtree_index) -> bool {
                bindings.clear();

                auto &expr_filter = filter->Cast<ExpressionFilter>();
                if (!rtree_index.TryMatchDistanceFunction(expr_filter.expr, bindings)) {
                    return false;
                }

                string function_name;
                if (expr_filter.expr->type == ExpressionType::BOUND_FUNCTION) {
                    auto &func_expr = expr_filter.expr->Cast<BoundFunctionExpression>();
                    function_name = func_expr.function.name;
                } else {
                    return false;
                }

                Expression *const_expr = nullptr;
                
                for (auto &binding : bindings) {
                    if (binding.get().type == ExpressionType::VALUE_CONSTANT) {
                        const_expr = &binding.get();
                    } 
                }

                if (!const_expr) {
                    return false;
                }

                const auto &constant = const_expr->Cast<BoundConstantExpression>();
                
                void *query_box = nullptr;
                size_t box_size = 0;
                
                if (constant.value.type().id() == LogicalTypeId::BLOB) {
                    
                    auto blob_data = constant.value.GetValueUnsafe<duckdb::string_t>();

                   const uint8_t *data = reinterpret_cast<const uint8_t *>(blob_data.GetDataUnsafe());
                    box_size = blob_data.GetSize();
                    
                    query_box = malloc(box_size);
                    memcpy(query_box, data, box_size);
                    
                }
                else if (constant.value.type().id() == LogicalTypeId::TIMESTAMP_TZ) {
                    auto timestamp_duckdb = constant.value.GetValueUnsafe<timestamp_tz_t>();
    
                    timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(timestamp_duckdb);
                    
                    box_size = sizeof(timestamp_tz_t);
                    query_box = malloc(box_size);
                    
                    if (query_box) {
                        memcpy(query_box, &ts_meos, box_size);
                    }
                }
                

                if (!query_box) {
                    return false;
                }

                bind_data = make_uniq<RTreeIndexScanBindData>(
                    duck_table, rtree_index, 1000, query_box, box_size, function_name);
                return true;
            });
            
            if (bind_data) {
                break;
            }
        }

        if (!bind_data) {
            return false;
        }
        auto cardinality = get.function.cardinality(context, bind_data.get());
        get.function = RTreeIndexScanFunction::GetFunction();
        get.has_estimated_cardinality = cardinality->has_estimated_cardinality;
        get.estimated_cardinality = cardinality->estimated_cardinality;
        get.bind_data = std::move(bind_data);

        if (!get.bind_data) {
            throw InternalException("bind_data is null after assignment");
        }

        return true;
    }

public:
    static bool TryOptimize(ClientContext &context, unique_ptr<LogicalOperator> &plan) {
        
        switch (plan->type) {
                
            case LogicalOperatorType::LOGICAL_GET:
                return TryOptimizeLogicalGet(context, plan);
                
            default:
                return false;
        }
    }

    static bool OptimizeRecursive(ClientContext &context, unique_ptr<LogicalOperator> &plan) {
        bool optimized = TryOptimize(context, plan);
        
        for (auto &child : plan->children) {
            optimized |= OptimizeRecursive(context, child);
        }
        
        return optimized;
    }

    static void Optimize(OptimizerExtensionInput &input, unique_ptr<LogicalOperator> &plan) {
        OptimizeRecursive(input.context, plan);
    }
};

void RTreeModule::RegisterScanOptimizer(DatabaseInstance &instance) {
    instance.config.optimizer_extensions.push_back(RTreeIndexScanOptimizer());
}

} // namespace duckdb