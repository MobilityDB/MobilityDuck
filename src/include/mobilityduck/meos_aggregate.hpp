#pragma once

#include <algorithm>
#include <cstdlib>
#include <new>
#include <utility>
#include <vector>

#include "duckdb/common/types/vector.hpp"
#include "duckdb/function/aggregate_function.hpp"

#include "mobilityduck/meos_thread.hpp"

namespace duckdb {

/**
 * An aggregate of the MEOS catalog over its MEOS state, as tools/codegen_duck_udfs.py
 * registers each entry of the catalog's `aggregates` section. The catalog states, per
 * CREATE AGGREGATE of MobilityDB, the public MEOS function of each role: the transition
 * folds the arguments of one row into the state, the combine joins two partial states, the
 * final answers the result. Nothing else is stated per aggregate, so this one template runs
 * every one of them from an operations struct the generator writes per catalog entry:
 *
 *   Row                    the decoded arguments of one row
 *   ARITY                  the number of SQL arguments
 *   GATHERS                true when the aggregate states no combine
 *   HAS_FINAL, KEEPS       whether it states a final, and whether that final takes its state
 *                          const, keeping it
 *   Read(fmt, i, row)      decode row i into row; false when its first argument is NULL
 *   Transition(state, row) the state with the row folded in
 *   Free(row)              release what Read decoded
 *   Combine(a, b)          the two partial states joined
 *   Final(state)           the result over the state
 *   Write(result, i, r)    the result r encoded at row i of the result vector, r released
 *   Order(row)             the start timestamp of the first argument of a row
 *
 * A combine answers in the place of its first state, returning it or freeing it for a larger
 * one (set_union_combinefn), except that a skip-list combine answers its second state when the
 * first is empty (temporal_tagg_combinefn): the state it does not answer is released. A final
 * consumes its state (set_union_finalfn, temporal_tagg_finalfn), so a state is released by
 * releasing what its final answers, except a final taking its state const, which keeps it
 * (temporal_append_finalfn answers a compact copy): that state is released after its final.
 *
 * An aggregate stating no combine is the constructor of a temporal value fed one row at a
 * time: appendInstantAgg builds a sequence from instants and appendSequenceAgg a sequence set
 * from sequences, each transition accepting a value only after those already folded in time.
 * DuckDB hands the rows of a group in no order and joins partial states, so such an aggregate
 * gathers its rows, a partial state joining another by taking its rows, and folds them when it
 * finishes, ordered by the start timestamp of their first argument: the answer MobilityDB gives
 * over the same rows ordered by time.
 */
template <class OPS>
struct MeosAggregate {
	using Row = typename OPS::Row;

	struct State {
		void *state;
		std::vector<Row> *rows;
	};

	static idx_t StateSize(const AggregateFunction &) {
		return sizeof(State);
	}

	static void Initialize(const AggregateFunction &, data_ptr_t data) {
		new (data) State {nullptr, nullptr};
	}

	// The state with the row folded in; the row is released either way
	static void Fold(State &s, Row &row) {
		try {
			s.state = OPS::Transition(s.state, row);
		} catch (...) {
			OPS::Free(row);
			throw;
		}
		OPS::Free(row);
	}

	static void Release(void *state) {
		if (!state) {
			return;
		}
		if (!OPS::HAS_FINAL || OPS::KEEPS) {
			free(state);
		} else {
			free(OPS::Final(state));
		}
	}

	static void ReleaseRows(State &s) {
		if (!s.rows) {
			return;
		}
		for (auto &row : *s.rows) {
			OPS::Free(row);
		}
		delete s.rows;
		s.rows = nullptr;
	}

	// The gathered rows folded into the state, in the order of their start timestamps
	static void FoldGathered(State &s) {
		if (!s.rows) {
			return;
		}
		auto &rows = *s.rows;
		std::stable_sort(rows.begin(), rows.end(),
		                 [](const Row &a, const Row &b) { return OPS::Order(a) < OPS::Order(b); });
		idx_t done = 0;
		try {
			for (; done < rows.size(); done++) {
				Fold(s, rows[done]);
			}
		} catch (...) {
			for (done++; done < rows.size(); done++) {
				OPS::Free(rows[done]);
			}
			delete s.rows;
			s.rows = nullptr;
			throw;
		}
		delete s.rows;
		s.rows = nullptr;
	}

	static void Update(Vector inputs[], AggregateInputData &, idx_t input_count, Vector &states, idx_t count) {
		EnsureMeosThreadInitialized();
		UnifiedVectorFormat fmt[OPS::ARITY];
		for (idx_t k = 0; k < input_count; k++) {
			inputs[k].ToUnifiedFormat(count, fmt[k]);
		}
		UnifiedVectorFormat sfmt;
		states.ToUnifiedFormat(count, sfmt);
		auto sdata = UnifiedVectorFormat::GetData<State *>(sfmt);
		for (idx_t i = 0; i < count; i++) {
			auto &s = *sdata[sfmt.sel->get_index(i)];
			Row row;
			if (!OPS::Read(fmt, i, row)) {
				continue;
			}
			if (OPS::GATHERS) {
				if (!s.rows) {
					s.rows = new std::vector<Row>();
				}
				s.rows->push_back(std::move(row));
			} else {
				Fold(s, row);
			}
		}
	}

	static void Combine(Vector &source, Vector &target, AggregateInputData &, idx_t count) {
		EnsureMeosThreadInitialized();
		auto src = FlatVector::GetData<State *>(source);
		auto dst = FlatVector::GetData<State *>(target);
		for (idx_t i = 0; i < count; i++) {
			auto &b = *src[i];
			auto &a = *dst[i];
			if (OPS::GATHERS) {
				if (!b.rows) {
					continue;
				}
				if (!a.rows) {
					a.rows = b.rows;
				} else {
					a.rows->insert(a.rows->end(), std::make_move_iterator(b.rows->begin()),
					               std::make_move_iterator(b.rows->end()));
					delete b.rows;
				}
				b.rows = nullptr;
				continue;
			}
			void *x = a.state;
			void *y = b.state;
			b.state = nullptr;
			if (!x) {
				a.state = y;
				continue;
			}
			if (!y) {
				continue;
			}
			a.state = nullptr;
			void *r = OPS::Combine(x, y);
			Release(r == y ? x : y);
			a.state = r;
		}
	}

	// The result over a state, written at row ridx of a result vector of the given type
	static void Finish(State &s, Vector &result, idx_t ridx, bool constant) {
		if (OPS::GATHERS) {
			FoldGathered(s);
		}
		void *state = s.state;
		s.state = nullptr;
		void *r = !state ? nullptr : OPS::HAS_FINAL ? OPS::Final(state) : state;
		if (state && OPS::KEEPS && r != state) {
			free(state);
		}
		if (!r) {
			if (constant) {
				ConstantVector::SetNull(result, true);
			} else {
				FlatVector::SetNull(result, ridx, true);
			}
			return;
		}
		OPS::Write(result, ridx, r);
	}

	static void Finalize(Vector &states, AggregateInputData &, Vector &result, idx_t count, idx_t offset) {
		EnsureMeosThreadInitialized();
		if (states.GetVectorType() == VectorType::CONSTANT_VECTOR) {
			result.SetVectorType(VectorType::CONSTANT_VECTOR);
			Finish(**ConstantVector::GetData<State *>(states), result, 0, true);
			return;
		}
		result.SetVectorType(VectorType::FLAT_VECTOR);
		auto sdata = FlatVector::GetData<State *>(states);
		for (idx_t i = 0; i < count; i++) {
			Finish(*sdata[i], result, i + offset, false);
		}
	}

	// May not throw: a state left by an error or never finalized is released
	static void Destroy(Vector &states, AggregateInputData &, idx_t count) {
		auto sdata = FlatVector::GetData<State *>(states);
		for (idx_t i = 0; i < count; i++) {
			auto &s = *sdata[i];
			try {
				Release(s.state);
			} catch (...) {
			}
			s.state = nullptr;
			ReleaseRows(s);
		}
	}

	static AggregateFunction Function(const vector<LogicalType> &arguments, const LogicalType &ret) {
		AggregateFunction fn(arguments, ret, StateSize, Initialize, Update, Combine, Finalize,
		                     FunctionNullHandling::SPECIAL_HANDLING, nullptr, nullptr, Destroy);
		if (!OPS::GATHERS) {
			fn.order_dependent = AggregateOrderDependent::NOT_ORDER_DEPENDENT;
		}
		return fn;
	}
};

} // namespace duckdb
