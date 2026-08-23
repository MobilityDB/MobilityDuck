#pragma once

#include "duckdb.hpp"
#include "meos_wrapper_simple.hpp"

namespace duckdb {

//! A bounding-box operator an in-memory index answers, and the MEOS operation it asks the index
//! for. A predicate is read as `column <name> query`, so an operator whose two sides play
//! different roles asks for a different operation once the query sits on the left: `a @> b` with
//! the indexed column on the left looks for stored boxes that CONTAIN the query, and the same
//! operator with the column on the right looks for stored boxes CONTAINED BY it. Both directions
//! are named here so that a caller reads the operation off the operand order rather than
//! assuming one.
struct IndexOperatorEntry {
	const char *name;
	//! The operation of `column <name> query`
	IndexSearchOp direct;
	//! The operation of `query <name> column`
	IndexSearchOp commuted;
};

//! Every operator an index answers. An operator absent from this table is answered by a scan.
static constexpr IndexOperatorEntry INDEX_OPERATORS[] = {
    {"&&", INDEX_OVERLAPS, INDEX_OVERLAPS},
    {"@>", INDEX_CONTAINS, INDEX_CONTAINED_BY},
    {"<@", INDEX_CONTAINED_BY, INDEX_CONTAINS},
};

//! Return the operation `name` asks of an index, false when an index answers no such operator.
//! `query_on_left` names the operand order: true when the query is the left argument.
inline bool IndexSearchOpFromName(const string &name, bool query_on_left, IndexSearchOp &result) {
	for (auto &entry : INDEX_OPERATORS) {
		if (name == entry.name) {
			result = query_on_left ? entry.commuted : entry.direct;
			return true;
		}
	}
	return false;
}

//! The operator names an index over `bbox_type` answers. Containment and overlap compare whole
//! extents, so they mean the same thing for a time box as for a space-time one and every box
//! type answers the same set.
inline unordered_set<string> IndexOperatorNames(MeosType bbox_type) {
	(void)bbox_type;
	unordered_set<string> names;
	for (auto &entry : INDEX_OPERATORS) {
		names.insert(entry.name);
	}
	return names;
}

} // namespace duckdb
