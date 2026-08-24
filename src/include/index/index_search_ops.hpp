#pragma once

#include "duckdb.hpp"
#include "meos_wrapper_simple.hpp"

namespace duckdb {

//! The axis a bounding-box operator compares along. An operator that compares whole extents is
//! answered by every box type, while an ordering operator is answered only by a box carrying the
//! axis it orders along: a time box has no horizontal extent to be left of, and only a
//! spatiotemporal box carries a vertical or a depth one.
enum IndexOperatorAxis {
	INDEX_AXIS_EXTENT,
	INDEX_AXIS_HORIZONTAL,
	INDEX_AXIS_VERTICAL,
	INDEX_AXIS_DEPTH,
	INDEX_AXIS_TIME,
};

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
	//! The axis the operator compares along, which decides the box types answering it
	IndexOperatorAxis axis;
};

//! Every operator an index answers. An operator absent from this table is answered by a scan.
//!
//! The ordering operators are the negated duals of one another under an exchange of operands --
//! `a << b` holds exactly when `b >> a` -- so the commuted column of each is its opposite. The set
//! mirrors the operators the PostgreSQL operator classes of the same box types index, so a
//! predicate that reaches an index here reaches one there.
//!
//! A bounding-box predicate is registered under two spellings, a symbol and a word, and both are
//! named here so that a query reaches the index however it is written. The temporal orderings are
//! the exception: they carry no symbol, `<<#` and its siblings not being operators DuckDB lexes,
//! so the word is the only spelling to route.
static constexpr IndexOperatorEntry INDEX_OPERATORS[] = {
    {"&&", INDEX_OVERLAPS, INDEX_OVERLAPS, INDEX_AXIS_EXTENT},
    {"overlaps", INDEX_OVERLAPS, INDEX_OVERLAPS, INDEX_AXIS_EXTENT},
    {"@>", INDEX_CONTAINS, INDEX_CONTAINED_BY, INDEX_AXIS_EXTENT},
    {"contains", INDEX_CONTAINS, INDEX_CONTAINED_BY, INDEX_AXIS_EXTENT},
    {"<@", INDEX_CONTAINED_BY, INDEX_CONTAINS, INDEX_AXIS_EXTENT},
    {"contained", INDEX_CONTAINED_BY, INDEX_CONTAINS, INDEX_AXIS_EXTENT},

    {"<<", INDEX_LEFT, INDEX_RIGHT, INDEX_AXIS_HORIZONTAL},
    {"left", INDEX_LEFT, INDEX_RIGHT, INDEX_AXIS_HORIZONTAL},
    {"&<", INDEX_OVERLEFT, INDEX_OVERRIGHT, INDEX_AXIS_HORIZONTAL},
    {"overleft", INDEX_OVERLEFT, INDEX_OVERRIGHT, INDEX_AXIS_HORIZONTAL},
    {">>", INDEX_RIGHT, INDEX_LEFT, INDEX_AXIS_HORIZONTAL},
    {"right", INDEX_RIGHT, INDEX_LEFT, INDEX_AXIS_HORIZONTAL},
    {"&>", INDEX_OVERRIGHT, INDEX_OVERLEFT, INDEX_AXIS_HORIZONTAL},
    {"overright", INDEX_OVERRIGHT, INDEX_OVERLEFT, INDEX_AXIS_HORIZONTAL},

    {"<<|", INDEX_BELOW, INDEX_ABOVE, INDEX_AXIS_VERTICAL},
    {"below", INDEX_BELOW, INDEX_ABOVE, INDEX_AXIS_VERTICAL},
    {"&<|", INDEX_OVERBELOW, INDEX_OVERABOVE, INDEX_AXIS_VERTICAL},
    {"overbelow", INDEX_OVERBELOW, INDEX_OVERABOVE, INDEX_AXIS_VERTICAL},
    {"|>>", INDEX_ABOVE, INDEX_BELOW, INDEX_AXIS_VERTICAL},
    {"above", INDEX_ABOVE, INDEX_BELOW, INDEX_AXIS_VERTICAL},
    {"|&>", INDEX_OVERABOVE, INDEX_OVERBELOW, INDEX_AXIS_VERTICAL},
    {"overabove", INDEX_OVERABOVE, INDEX_OVERBELOW, INDEX_AXIS_VERTICAL},

    {"<</", INDEX_FRONT, INDEX_BACK, INDEX_AXIS_DEPTH},
    {"front", INDEX_FRONT, INDEX_BACK, INDEX_AXIS_DEPTH},
    {"&</", INDEX_OVERFRONT, INDEX_OVERBACK, INDEX_AXIS_DEPTH},
    {"overfront", INDEX_OVERFRONT, INDEX_OVERBACK, INDEX_AXIS_DEPTH},
    {"/>>", INDEX_BACK, INDEX_FRONT, INDEX_AXIS_DEPTH},
    {"back", INDEX_BACK, INDEX_FRONT, INDEX_AXIS_DEPTH},
    {"/&>", INDEX_OVERBACK, INDEX_OVERFRONT, INDEX_AXIS_DEPTH},
    {"overback", INDEX_OVERBACK, INDEX_OVERFRONT, INDEX_AXIS_DEPTH},

    {"before", INDEX_BEFORE, INDEX_AFTER, INDEX_AXIS_TIME},
    {"overbefore", INDEX_OVERBEFORE, INDEX_OVERAFTER, INDEX_AXIS_TIME},
    {"after", INDEX_AFTER, INDEX_BEFORE, INDEX_AXIS_TIME},
    {"overafter", INDEX_OVERAFTER, INDEX_OVERBEFORE, INDEX_AXIS_TIME},
};

//! Return true if a box of `bbox_type` carries `axis`, and so answers the operators ordering
//! along it. A spatiotemporal box carries every axis; a temporal box carries a value extent and a
//! time one; a span carries the single extent of whatever it spans, which is a time extent for the
//! span types whose values are instants and a horizontal one for the rest.
inline bool IndexBboxHasAxis(MeosType bbox_type, IndexOperatorAxis axis) {
	if (axis == INDEX_AXIS_EXTENT) {
		return true;
	}
	switch (bbox_type) {
	case T_STBOX:
		return true;
	case T_TBOX:
		return axis == INDEX_AXIS_HORIZONTAL || axis == INDEX_AXIS_TIME;
	case T_TSTZSPAN:
	case T_DATESPAN:
		return axis == INDEX_AXIS_TIME;
	case T_INTSPAN:
	case T_BIGINTSPAN:
	case T_FLOATSPAN:
		return axis == INDEX_AXIS_HORIZONTAL;
	default:
		return false;
	}
}

//! Return the operation `name` asks of an index over `bbox_type`, false when such an index answers
//! no such operator. `query_on_left` names the operand order: true when the query is the left
//! argument.
inline bool IndexSearchOpFromName(const string &name, MeosType bbox_type, bool query_on_left,
                                  IndexSearchOp &result) {
	for (auto &entry : INDEX_OPERATORS) {
		if (name == entry.name && IndexBboxHasAxis(bbox_type, entry.axis)) {
			result = query_on_left ? entry.commuted : entry.direct;
			return true;
		}
	}
	return false;
}

//! The operator names an index over `bbox_type` answers. Overlap and containment compare whole
//! extents, so every box type answers those; an ordering operator is answered only where the box
//! carries the axis it orders along.
inline unordered_set<string> IndexOperatorNames(MeosType bbox_type) {
	unordered_set<string> names;
	for (auto &entry : INDEX_OPERATORS) {
		if (IndexBboxHasAxis(bbox_type, entry.axis)) {
			names.insert(entry.name);
		}
	}
	return names;
}

} // namespace duckdb
