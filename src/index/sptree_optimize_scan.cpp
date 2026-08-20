#include "meos_wrapper_simple.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/catalog/catalog_entry/duck_table_entry.hpp"
#include "duckdb/optimizer/optimizer_extension.hpp"
#include "duckdb/planner/expression/bound_columnref_expression.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include "duckdb/planner/operator/logical_get.hpp"
#include "duckdb/planner/operator/logical_projection.hpp"
#include "duckdb/planner/operator/logical_top_n.hpp"
#include "duckdb/planner/operator/logical_filter.hpp"
#include "duckdb/planner/filter/expression_filter.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/storage/index.hpp"
#include "duckdb/storage/table/data_table_info.hpp"
#include <algorithm>

#include "index/sptree_module.hpp"
#include "index/sptree_index_scan.hpp"
#include "time_util.hpp"



namespace duckdb {

class TSPTreeIndexScanOptimizer : public OptimizerExtension {
public:
    TSPTreeIndexScanOptimizer() {
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
        
        unique_ptr<TSPTreeIndexScanBindData> bind_data = nullptr;
        // The column the matched filter tests, so the filters the index does NOT answer can be
        // told apart from it below and evaluated above the scan.
        optional_idx matched_column;
        vector<reference<Expression>> bindings;

        // 1.4 replaced TableIndexList::BindAndScan with a two-step pattern:
        // BindIndexes promotes any unbound TSPTreeIndex entries to bound, and
        // Scan iterates the bound indexes; the callback returns true to stop.
        table_info.BindIndexes(context, TSPTreeIndex::TYPE_NAME);

        for (auto &filter_pair : get.table_filters.filters) {
            auto &filter = filter_pair.second;

            table_info.GetIndexes().Scan([&](Index &index) -> bool {
                if (!index.IsBound() || index.GetIndexType() != TSPTreeIndex::TYPE_NAME) {
                    return false;
                }
                auto &sptree_index = index.Cast<TSPTreeIndex>();

                // The index bounds the column it was built over and no other. A table can carry
                // one index per column, every one of them the same type, so a filter matching the
                // SHAPE this index answers is not enough — it has to read THIS index's column, or
                // the rows come back from boxes describing a different column entirely.
                if (sptree_index.GetColumnIds()[0] != filter_pair.first) {
                    return false;
                }
                bindings.clear();

                // Only an arbitrary-expression filter can carry the `&&` the index
                // answers; `id > 20` arrives as CONSTANT_COMPARISON, `IS NOT NULL`
                // as IS_NOT_NULL. Casting one of those to ExpressionFilter throws.
                if (filter->filter_type != TableFilterType::EXPRESSION_FILTER) {
                    return false;
                }
                auto &expr_filter = filter->Cast<ExpressionFilter>();
                if (!sptree_index.TryMatchDistanceFunction(expr_filter.expr, bindings)) {
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

                bind_data = make_uniq<TSPTreeIndexScanBindData>(
                    duck_table, sptree_index, 1000, query_box, box_size, function_name);
                return true;
            });
            
            if (bind_data) {
                matched_column = filter_pair.first;
                break;
            }
        }

        if (!bind_data) {
            return false;
        }

        // The scan answers the matched filter from the index and evaluates nothing else — it
        // declares filter_pushdown = false and its Fetch applies no filters. So every OTHER
        // pushed-down filter has to be evaluated above it, or replacing the scan would drop
        // that predicate and return extra rows. Build those expressions before touching the
        // plan, so a column that cannot be referenced leaves the query on the sequential scan
        // rather than half-rewritten.
        vector<unique_ptr<Expression>> residual;
        auto original_projection = get.projection_ids;
        for (auto &filter_pair : get.table_filters.filters) {
            if (filter_pair.first == matched_column.GetIndex()) {
                continue;
            }
            // The filter keys a TABLE column; a binding addresses its POSITION among the
            // columns the get reads (LogicalGet::GetColumnBindings numbers by that position,
            // and by the projection_ids entry — itself such a position — when projecting).
            optional_idx position;
            for (idx_t i = 0; i < get.GetColumnIds().size(); i++) {
                if (get.GetColumnIds()[i].GetPrimaryIndex() == filter_pair.first) {
                    position = i;
                    break;
                }
            }
            if (!position.IsValid()) {
                return false;
            }
            // A filtered column the query does not select is not in the projection, and the
            // filter above has to read it. Add it, and record the original projection so the
            // filter can drop it again — the extra column must not reach the parent.
            if (!original_projection.empty() &&
                std::find(get.projection_ids.begin(), get.projection_ids.end(),
                          position.GetIndex()) == get.projection_ids.end()) {
                get.projection_ids.push_back(position.GetIndex());
            }
            auto column_type = get.GetColumnType(ColumnIndex(filter_pair.first));
            auto column_ref = make_uniq<BoundColumnRefExpression>(
                std::move(column_type), ColumnBinding(get.table_index, position.GetIndex()));
            residual.push_back(filter_pair.second->ToExpression(*column_ref));
        }

        auto cardinality = get.function.cardinality(context, bind_data.get());
        get.function = TSPTreeIndexScanFunction::GetFunction();
        get.has_estimated_cardinality = cardinality->has_estimated_cardinality;
        get.estimated_cardinality = cardinality->estimated_cardinality;
        get.bind_data = std::move(bind_data);

        if (!get.bind_data) {
            throw InternalException("bind_data is null after assignment");
        }

        if (!residual.empty()) {
            // The index now answers the matched filter, so the scan must stop carrying the
            // others: they move into a filter above it, which is where they are evaluated.
            for (auto it = get.table_filters.filters.begin();
                 it != get.table_filters.filters.end();) {
                it = (it->first == matched_column.GetIndex()) ? std::next(it)
                                                              : get.table_filters.filters.erase(it);
            }
            auto filter_op = make_uniq<LogicalFilter>();
            filter_op->expressions = std::move(residual);
            // Emit what the query asked for, not the columns a predicate needed to read.
            if (!original_projection.empty()) {
                for (idx_t i = 0; i < original_projection.size(); i++) {
                    filter_op->projection_map.push_back(i);
                }
            }
            filter_op->children.push_back(std::move(plan));
            filter_op->ResolveOperatorTypes();
            plan = std::move(filter_op);
        }

        return true;
    }

    static LogicalGet *ResolveOrderedGet(LogicalTopN &top_n, Expression *&order_expr) {
        if (top_n.children.size() != 1) {
            return nullptr;
        }
        order_expr = top_n.orders[0].expression.get();
        auto *child = top_n.children[0].get();

        if (child->type == LogicalOperatorType::LOGICAL_PROJECTION) {
            auto &projection = child->Cast<LogicalProjection>();
            if (projection.children.size() != 1 ||
                order_expr->type != ExpressionType::BOUND_COLUMN_REF) {
                return nullptr;
            }
            const auto &binding = order_expr->Cast<BoundColumnRefExpression>().binding;
            if (binding.table_index != projection.table_index ||
                binding.column_index >= projection.expressions.size()) {
                return nullptr;
            }
            order_expr = projection.expressions[binding.column_index].get();
            child = projection.children[0].get();
        }

        return child->type == LogicalOperatorType::LOGICAL_GET ? &child->Cast<LogicalGet>()
                                                               : nullptr;
    }

    //! `ORDER BY <indexed column> |=| <constant> LIMIT k` reads the index nearest-first instead of
    //! ranking the whole table. The TopN stays above the scan: the index answers with the k rows
    //! it proved nearest, and the TopN puts them in order for the parent.
    static bool TryOptimizeLogicalTopN(ClientContext &context, unique_ptr<LogicalOperator> &plan) {
        auto &top_n = plan->Cast<LogicalTopN>();

        // One ascending distance is what an index can answer. A second key is not a tie-break the
        // index can honour either: it decides among rows the k-th distance ties with, and the k
        // rows the index proves nearest need not be the k that key would have chosen.
        if (top_n.orders.size() != 1 || top_n.orders[0].type == OrderType::DESCENDING ||
            top_n.limit == 0) {
            return false;
        }

        Expression *order_expr = nullptr;
        auto *get_ptr = ResolveOrderedGet(top_n, order_expr);
        if (!get_ptr) {
            return false;
        }
        auto &get = *get_ptr;
        if (get.function.name != "seq_scan" || !get.GetTable()->IsDuckTable()) {
            return false;
        }
        // The index scan evaluates no filter, so a pushed-down predicate would be dropped and the
        // query would answer over rows it should not see. Leave those on the sequential scan.
        if (!get.table_filters.filters.empty()) {
            return false;
        }

        auto &duck_table = get.GetTable()->Cast<DuckTableEntry>();
        auto &table_info = *get.GetTable()->GetStorage().GetDataTableInfo();
        table_info.BindIndexes(context, TSPTreeIndex::TYPE_NAME);

        unique_ptr<TSPTreeIndexScanBindData> bind_data = nullptr;
        vector<reference<Expression>> bindings;

        table_info.GetIndexes().Scan([&](Index &index) -> bool {
            if (!index.IsBound() || index.GetIndexType() != TSPTreeIndex::TYPE_NAME) {
                return false;
            }
            auto &sptree_index = index.Cast<TSPTreeIndex>();
            bindings.clear();
            if (!sptree_index.TryMatchNearestFunction(*order_expr, bindings)) {
                return false;
            }

            // The distance has to be measured from the column THIS index was built over: a second
            // column of the same type would match the shape while the index says nothing about it.
            // The index covers exactly one column, which its own constructor enforces.
            const auto indexed_column = sptree_index.GetColumnIds()[0];
            optional_idx indexed_position;
            for (idx_t i = 0; i < get.GetColumnIds().size(); i++) {
                if (get.GetColumnIds()[i].GetPrimaryIndex() == indexed_column) {
                    indexed_position = i;
                    break;
                }
            }
            if (!indexed_position.IsValid()) {
                return false;
            }

            // The operand that is not the column has to be known before the scan opens.
            Expression *const_expr = nullptr;
            bool reads_indexed_column = false;
            for (auto &binding : bindings) {
                auto &operand = binding.get();
                if (operand.type == ExpressionType::VALUE_CONSTANT) {
                    const_expr = &operand;
                } else if (operand.type == ExpressionType::BOUND_COLUMN_REF) {
                    const auto &column = operand.Cast<BoundColumnRefExpression>().binding;
                    reads_indexed_column |= column.table_index == get.table_index &&
                                            column.column_index == indexed_position.GetIndex();
                }
            }
            if (!const_expr || !reads_indexed_column) {
                return false;
            }

            // The matcher already required the box operand's type, so the constant is a box of the
            // kind the index stores rather than some other blob.
            const auto &constant = const_expr->Cast<BoundConstantExpression>();
            auto blob_data = constant.value.GetValueUnsafe<duckdb::string_t>();
            const size_t box_size = blob_data.GetSize();
            void *query_box = malloc(box_size);
            if (!query_box) {
                return false;
            }
            memcpy(query_box, blob_data.GetDataUnsafe(), box_size);

            // The rows the parent skips still have to be found, so the scan answers offset + limit.
            bind_data = make_uniq<TSPTreeIndexScanBindData>(
                duck_table, sptree_index, top_n.offset + top_n.limit, query_box, box_size,
                TSPTreeIndexScanBindData::NN_OPERATION);
            return true;
        });

        if (!bind_data) {
            return false;
        }

        // The scan returns the rows it proved nearest and no others, so what it answers is known
        // exactly and there is no cardinality to estimate.
        get.estimated_cardinality = bind_data->limit;
        get.has_estimated_cardinality = true;
        get.function = TSPTreeIndexScanFunction::GetFunction();
        get.bind_data = std::move(bind_data);
        return true;
    }

public:
    static bool TryOptimize(ClientContext &context, unique_ptr<LogicalOperator> &plan) {
        
        switch (plan->type) {
                
            case LogicalOperatorType::LOGICAL_GET:
                return TryOptimizeLogicalGet(context, plan);

            case LogicalOperatorType::LOGICAL_TOP_N:
                return TryOptimizeLogicalTopN(context, plan);

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

void TSPTreeModule::RegisterScanOptimizer(ExtensionLoader &loader) {
    loader.GetDatabaseInstance().config.optimizer_extensions.push_back(TSPTreeIndexScanOptimizer());
}

} // namespace duckdb
