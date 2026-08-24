/* A bounding-box join answered by probing a TSPTREE index once per row of the
 * other side.
 *
 * Without this, `WHERE a.g && b.g` plans as a blockwise nested-loop join over
 * two sequential scans: every pair of rows is compared. The index answers the
 * same predicate from one descent per probe row, but the scan-side index path
 * cannot serve it — that path binds a query box during planning, and here the
 * box is a column of the other side, known only at execution time.
 *
 * The indexed side is reached through the index and a row fetch rather than a
 * child scan, so this operator carries a single child: the probe side.
 */
#include "index/sptree_index_join.hpp"

#include "duckdb/execution/physical_plan_generator.hpp"
#include "duckdb/optimizer/optimizer_extension.hpp"
#include "duckdb/planner/expression/bound_columnref_expression.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"
#include "duckdb/planner/operator/logical_any_join.hpp"
#include "duckdb/planner/operator/logical_get.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/storage/data_table.hpp"
#include "duckdb/storage/table/data_table_info.hpp"
#include "duckdb/storage/table/scan_state.hpp"
#include "duckdb/storage/index.hpp"
#include "duckdb/transaction/duck_transaction.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"

#include "geo/stbox.hpp"
#include "geo/tgeompoint.hpp"
#include "geo/tgeometry.hpp"
#include "geo/tgeography.hpp"
#include "geo/tgeogpoint.hpp"

extern "C" {
    #include <meos.h>
}

namespace duckdb {

//===--------------------------------------------------------------------===//
// Logical
//===--------------------------------------------------------------------===//

void LogicalSPTreeIndexJoin::ResolveTypes() {
	types = children[0]->types;
	for (auto &t : index_types) {
		types.push_back(t);
	}
}

vector<ColumnBinding> LogicalSPTreeIndexJoin::GetColumnBindings() {
	auto result = children[0]->GetColumnBindings();
	for (auto &b : index_bindings) {
		result.push_back(b);
	}
	return result;
}

PhysicalOperator &LogicalSPTreeIndexJoin::CreatePlan(ClientContext &context, PhysicalPlanGenerator &planner) {
	auto &child = planner.CreatePlan(*children[0]);
	auto &op = planner.Make<PhysicalSPTreeIndexJoin>(types, index_table, index, probe_column, probe_is_temporal,
	                                                index_columns, estimated_cardinality);
	auto &join = op.Cast<PhysicalSPTreeIndexJoin>();
	join.children.push_back(child);
	return op;
}

//===--------------------------------------------------------------------===//
// Physical
//===--------------------------------------------------------------------===//

PhysicalSPTreeIndexJoin::PhysicalSPTreeIndexJoin(PhysicalPlan &physical_plan, vector<LogicalType> types_p,
                                               DuckTableEntry &index_table_p, TSPTreeIndex &index_p,
                                               idx_t probe_column_p, bool probe_is_temporal_p,
                                               vector<idx_t> index_columns_p, idx_t estimated_cardinality)
    : PhysicalOperator(physical_plan, PhysicalOperatorType::EXTENSION, std::move(types_p), estimated_cardinality),
      index_table(index_table_p), index(index_p), probe_column(probe_column_p),
      probe_is_temporal(probe_is_temporal_p), index_columns(std::move(index_columns_p)) {
	probe_column_count = types.size() - index_columns.size();
}

class SPTreeIndexJoinState : public OperatorState {
public:
	//! Row of the incoming chunk being probed; carried across calls when one
	//! probe row matches more rows than fit in a single output chunk.
	idx_t probe_row = 0;
	//! Matches of the current probe row not yet emitted.
	vector<row_t> matches;
	idx_t match_offset = 0;
	//! Row ids fetched in one go, and the chunk they land in.
	Vector row_ids = Vector(LogicalType::ROW_TYPE);
	DataChunk fetched;
	ColumnFetchState fetch_state;
	vector<StorageIndex> storage_ids;
	//! All entries point at the probe row, so one copy repeats it across its matches.
	SelectionVector repeat_sel = SelectionVector(STANDARD_VECTOR_SIZE);
};

unique_ptr<OperatorState> PhysicalSPTreeIndexJoin::GetOperatorState(ExecutionContext &context) const {
	auto state = make_uniq<SPTreeIndexJoinState>();
	auto &columns = index_table.GetColumns();
	vector<LogicalType> fetch_types;
	for (auto &id : index_columns) {
		state->storage_ids.emplace_back(columns.GetColumn(LogicalIndex(id)).StorageOid());
		fetch_types.push_back(columns.GetColumn(LogicalIndex(id)).Type());
	}
	state->fetched.Initialize(context.client, fetch_types);
	return std::move(state);
}

OperatorResultType PhysicalSPTreeIndexJoin::Execute(ExecutionContext &context, DataChunk &input, DataChunk &chunk,
                                                   GlobalOperatorState &, OperatorState &state_p) const {
	auto &state = state_p.Cast<SPTreeIndexJoinState>();
	auto &transaction = DuckTransaction::Get(context.client, index_table.catalog);
	auto &probe_vec = input.data[probe_column];
	probe_vec.Flatten(input.size());
	auto probe_data = FlatVector::GetData<string_t>(probe_vec);
	auto &probe_validity = FlatVector::Validity(probe_vec);

	idx_t out = 0;
	while (state.probe_row < input.size()) {
		/* Probe the index once for this row, unless resuming a row whose
		 * matches did not fit in the previous output chunk. */
		if (state.match_offset == 0 && state.matches.empty()) {
			if (! probe_validity.RowIsValid(state.probe_row)) {
				state.probe_row++;
				continue;
			}
			string_t blob = probe_data[state.probe_row];
			/* Probe with the value converted exactly as the index converted the
			 * rows it holds (sptree_module.cpp): a Temporal column is indexed by
			 * its derived stbox with the SRID normalized away, so probing with
			 * the raw Temporal bytes would read them as a box. */
			if (probe_is_temporal) {
				auto temp = reinterpret_cast<const Temporal *>(blob.GetData());
				STBox *box = tspatial_to_stbox(temp);
				if (! box) {
					state.probe_row++;
					continue;
				}
				if (stbox_srid(box) != 0) {
					STBox *normalized = stbox_set_srid(box, 0);
					if (normalized) {
						free(box);
						box = normalized;
					}
				}
				state.matches = index.Search(box, INDEX_OVERLAPS);
				free(box);
			} else {
				/* An stbox column already holds the box, but the index stores its
				 * rows with the SRID normalized away and MEOS refuses to compare
				 * boxes whose SRIDs differ — so the probe box is normalized too,
				 * as the scan path does for its query box. */
				auto probe_box = reinterpret_cast<const STBox *>(blob.GetData());
				if (MEOS_FLAGS_GET_X(probe_box->flags) && stbox_srid(probe_box) != 0) {
					STBox *normalized = stbox_set_srid(probe_box, 0);
					if (! normalized) {
						state.probe_row++;
						continue;
					}
					state.matches = index.Search(normalized, INDEX_OVERLAPS);
					free(normalized);
				} else {
					state.matches = index.Search(probe_box, INDEX_OVERLAPS);
				}
			}
			if (state.matches.empty()) {
				state.probe_row++;
				continue;
			}
		}

		idx_t remaining = state.matches.size() - state.match_offset;
		idx_t take = MinValue<idx_t>(remaining, STANDARD_VECTOR_SIZE - out);
		if (take == 0) {
			chunk.SetCardinality(out);
			return OperatorResultType::HAVE_MORE_OUTPUT;
		}

		/* Fetch the matching rows of the indexed table in one call. */
		auto ids = FlatVector::GetData<row_t>(state.row_ids);
		for (idx_t k = 0; k < take; k++) {
			ids[k] = state.matches[state.match_offset + k];
		}
		state.fetched.Reset();
		index_table.GetStorage().Fetch(transaction, state.fetched, state.storage_ids, state.row_ids, take,
		                               state.fetch_state);

		/* The probe row repeats across its matches; the indexed columns follow.
		 * Both copies go through the vector layer: a per-cell GetValue/SetValue
		 * round trip would dominate the timings this operator exists to improve. */
		for (idx_t k = 0; k < take; k++) {
			state.repeat_sel.set_index(k, state.probe_row);
		}
		for (idx_t c = 0; c < probe_column_count; c++) {
			VectorOperations::Copy(input.data[c], chunk.data[c], state.repeat_sel, take, 0, out);
		}
		for (idx_t c = 0; c < index_columns.size(); c++) {
			VectorOperations::Copy(state.fetched.data[c], chunk.data[probe_column_count + c], take, 0, out);
		}

		out += take;
		state.match_offset += take;
		if (state.match_offset == state.matches.size()) {
			state.matches.clear();
			state.match_offset = 0;
			state.probe_row++;
		}
		if (out == STANDARD_VECTOR_SIZE) {
			chunk.SetCardinality(out);
			return OperatorResultType::HAVE_MORE_OUTPUT;
		}
	}

	chunk.SetCardinality(out);
	state.probe_row = 0;
	return OperatorResultType::NEED_MORE_INPUT;
}

//===--------------------------------------------------------------------===//
// Optimizer: recognise `a.col && b.col` over a table carrying a TSPTREE index
//===--------------------------------------------------------------------===//

class TSPTreeIndexJoinOptimizer : public OptimizerExtension {
public:
	TSPTreeIndexJoinOptimizer() {
		optimize_function = Optimize;
	}

private:
	//! The get directly under a join side, when that side is a plain scan.
	static optional_ptr<LogicalGet> GetScan(LogicalOperator &op) {
		if (op.type != LogicalOperatorType::LOGICAL_GET) {
			return nullptr;
		}
		auto &get = op.Cast<LogicalGet>();
		if (! get.GetTable() || ! get.GetTable()->IsDuckTable()) {
			return nullptr;
		}
		return get;
	}

	//! Whether a probe value of this type can be turned into the stbox the index
	//! holds. An stbox column already is one; the four temporal spatial types
	//! derive one, exactly as the index did when it inserted its rows
	//! (sptree_module.cpp:100-107). Everything else is refused: `tspatial_to_stbox`
	//! is meaningless on a tint, whose box is a tbox.
	static bool ProbeYieldsStbox(const LogicalType &type, bool &is_temporal) {
		if (type == StboxType::stbox()) {
			is_temporal = false;
			return true;
		}
		if (type == TgeompointType::tgeompoint() || type == TGeogpointType::tgeogpoint() ||
		    type == TGeometryTypes::tgeometry() || type == TGeographyTypes::tgeography()) {
			is_temporal = true;
			return true;
		}
		return false;
	}

	//! Build the rewrite with `indexed` served by its index and `probe` kept as
	//! the child, or report that this pairing cannot be served.
	static bool TryBuild(ClientContext &context, unique_ptr<LogicalOperator> &plan, LogicalAnyJoin &join,
	                     idx_t indexed_side, LogicalGet &indexed_get, const BoundColumnRefExpression &indexed_ref,
	                     const BoundColumnRefExpression &probe_ref) {
		/* The indexed side stops being a scan, so any filter pushed into it would
		 * simply be lost — the same silent wrong-rows failure the scan optimizer
		 * guards against by requiring a single filter. */
		if (! indexed_get.table_filters.filters.empty()) {
			return false;
		}
		bool probe_is_temporal = false;
		if (! ProbeYieldsStbox(probe_ref.return_type, probe_is_temporal)) {
			return false;
		}

		auto &table = indexed_get.GetTable()->Cast<DuckTableEntry>();
		auto &table_info = *indexed_get.GetTable()->GetStorage().GetDataTableInfo();
		auto &columns = table.GetColumns();
		auto &col_ids = indexed_get.GetColumnIds();
		if (indexed_ref.binding.column_index >= col_ids.size()) {
			return false;
		}

		/* Row ids have no column entry to fetch by, so a side carrying one is
		 * left to the nested-loop join. */
		vector<LogicalType> index_types;
		vector<idx_t> index_columns;
		for (auto &ci : col_ids) {
			if (ci.IsRowIdColumn()) {
				return false;
			}
			index_columns.push_back(ci.GetPrimaryIndex());
			index_types.push_back(columns.GetColumn(LogicalIndex(ci.GetPrimaryIndex())).Type());
		}

		auto storage_id = columns.GetColumn(LogicalIndex(col_ids[indexed_ref.binding.column_index].GetPrimaryIndex()))
		                      .StorageOid();

		table_info.BindIndexes(context, TSPTreeIndex::TYPE_NAME);
		optional_ptr<TSPTreeIndex> found;
		table_info.GetIndexes().Scan([&](Index &index) -> bool {
			if (! index.IsBound() || index.GetIndexType() != TSPTreeIndex::TYPE_NAME) {
				return false;
			}
			auto &bound = index.Cast<TSPTreeIndex>();
			auto &ids = bound.GetColumnIds();
			if (ids.size() != 1 || ids[0] != storage_id || bound.GetBboxType() != T_STBOX) {
				return false;
			}
			found = bound;
			return true;
		});
		if (! found) {
			return false;
		}

		auto index_bindings = indexed_get.GetColumnBindings();
		if (index_bindings.size() != index_types.size()) {
			return false;
		}

		auto probe_side = std::move(join.children[1 - indexed_side]);
		auto estimated = join.estimated_cardinality;
		auto result = make_uniq<LogicalSPTreeIndexJoin>(table, *found, probe_ref.binding.column_index,
		                                               probe_is_temporal, std::move(index_types),
		                                               std::move(index_columns), std::move(index_bindings));
		result->children.push_back(std::move(probe_side));
		result->estimated_cardinality = estimated;
		result->ResolveOperatorTypes();
		plan = std::move(result);
		return true;
	}

	static bool TryOptimizeJoin(ClientContext &context, unique_ptr<LogicalOperator> &plan) {
		auto &join = plan->Cast<LogicalAnyJoin>();
		if (join.join_type != JoinType::INNER || ! join.condition) {
			return false;
		}
		/* Only a bare `a && b` qualifies. A conjunction arrives as
		 * BoundConjunctionExpression, so the extra predicate cannot be dropped
		 * here the way it silently was in the scan path. */
		if (join.condition->type != ExpressionType::BOUND_FUNCTION) {
			return false;
		}
		auto &func = join.condition->Cast<BoundFunctionExpression>();
		if (func.function.name != "&&" || func.children.size() != 2) {
			return false;
		}
		if (func.children[0]->type != ExpressionType::BOUND_COLUMN_REF ||
		    func.children[1]->type != ExpressionType::BOUND_COLUMN_REF) {
			return false;
		}
		if (join.children.size() != 2) {
			return false;
		}
		optional_ptr<LogicalGet> gets[2] = {GetScan(*join.children[0]), GetScan(*join.children[1])};
		if (! gets[0] || ! gets[1]) {
			return false;
		}

		/* Attach each operand to the side it reads from. */
		const BoundColumnRefExpression *refs[2] = {nullptr, nullptr};
		for (auto &child : func.children) {
			auto &ref = child->Cast<BoundColumnRefExpression>();
			for (idx_t s = 0; s < 2; s++) {
				if (ref.binding.table_index == gets[s]->table_index) {
					refs[s] = &ref;
				}
			}
		}
		if (! refs[0] || ! refs[1]) {
			return false;
		}

		/* Index the larger side: the descent replaces a scan of it, so the saving
		 * grows with the rows it removes, while the other side is walked once. */
		idx_t first = gets[0]->estimated_cardinality >= gets[1]->estimated_cardinality ? 0 : 1;
		for (idx_t attempt = 0; attempt < 2; attempt++) {
			idx_t indexed = attempt == 0 ? first : 1 - first;
			if (TryBuild(context, plan, join, indexed, *gets[indexed], *refs[indexed], *refs[1 - indexed])) {
				return true;
			}
		}
		return false;
	}

public:
	static bool TryOptimize(ClientContext &context, unique_ptr<LogicalOperator> &plan) {
		if (plan->type == LogicalOperatorType::LOGICAL_ANY_JOIN) {
			return TryOptimizeJoin(context, plan);
		}
		return false;
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

void TSPTreeModule::RegisterJoinOptimizer(ExtensionLoader &loader) {
	loader.GetDatabaseInstance().config.optimizer_extensions.push_back(TSPTreeIndexJoinOptimizer());
}

} // namespace duckdb
