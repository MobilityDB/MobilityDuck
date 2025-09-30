#include "meos_wrapper_simple.hpp"
#include "duckdb/catalog/catalog_entry/duck_table_entry.hpp"
#include "duckdb/optimizer/optimizer_extension.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include "duckdb/planner/operator/logical_get.hpp"
#include "duckdb/planner/operator/logical_projection.hpp"
#include "duckdb/planner/operator/logical_top_n.hpp"
#include "duckdb/planner/operator/logical_filter.hpp"
#include "duckdb/planner/filter/expression_filter.hpp"

#include "duckdb/main/database.hpp"
#include <algorithm>

#include "index/rtree_module.hpp"
#include "index/rtree_index_scan.hpp"


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
                
                unique_ptr<STBox> query_stbox = nullptr;
                
                if (constant.value.type().id() == LogicalTypeId::BLOB) {
                    
                    auto blob_data = constant.value.GetValueUnsafe<duckdb::string_t>();

                    const uint8_t *stbox_data = reinterpret_cast<const uint8_t *>(blob_data.GetDataUnsafe());
                    size_t stbox_size = blob_data.GetSize();

                    STBox *box = nullptr;
                    
                    box = (STBox*)malloc(stbox_size);
                    
                    memcpy(box, stbox_data, stbox_size);

                    query_stbox = unique_ptr<STBox>(box);
                    
                }

                if (!query_stbox) {
                    return false;
                }

                bind_data = make_uniq<RTreeIndexScanBindData>(
                    duck_table, rtree_index, 1000, std::move(query_stbox));
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
        // fprintf(stderr, "Done OptimizeRecursive\n");
    }
};

void RTreeModule::RegisterScanOptimizer(DatabaseInstance &instance) {
    instance.config.optimizer_extensions.push_back(RTreeIndexScanOptimizer());
}

} // namespace duckdb