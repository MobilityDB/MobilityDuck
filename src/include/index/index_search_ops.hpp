#pragma once

#include "duckdb.hpp"
#include "meos_wrapper_simple.hpp"
#include "generated/index_search_ops.hpp"

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

//! The axis the MEOS search `op` compares along. The overlap, containment, equality and adjacency
//! searches compare whole extents; each ordering search, strict or overlapping, compares along
//! the one axis it names.
inline IndexOperatorAxis IndexSearchAxis(IndexSearchOp op) {
	switch (op) {
	case INDEX_OVERLAPS:
	case INDEX_CONTAINS:
	case INDEX_CONTAINED_BY:
	case INDEX_SAME:
	case INDEX_ADJACENT:
		return INDEX_AXIS_EXTENT;
	case INDEX_LEFT:
	case INDEX_OVERLEFT:
	case INDEX_RIGHT:
	case INDEX_OVERRIGHT:
		return INDEX_AXIS_HORIZONTAL;
	case INDEX_BELOW:
	case INDEX_OVERBELOW:
	case INDEX_ABOVE:
	case INDEX_OVERABOVE:
		return INDEX_AXIS_VERTICAL;
	case INDEX_FRONT:
	case INDEX_OVERFRONT:
	case INDEX_BACK:
	case INDEX_OVERBACK:
		return INDEX_AXIS_DEPTH;
	case INDEX_BEFORE:
	case INDEX_OVERBEFORE:
	case INDEX_AFTER:
	case INDEX_OVERAFTER:
		return INDEX_AXIS_TIME;
	default:
		throw InternalException("IndexSearchAxis: unknown IndexSearchOp %d", (int) op);
	}
}

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

//! Return the search `name` asks of an index over `bbox_type`, false when such an index answers no
//! such operator. `name` is an operator symbol or the SQL name of a function backing it, as the
//! generated INDEX_OPERATORS states them. `query_on_left` names the operand order: true when the
//! query is the left argument, which asks for the commuted search, and an operator with none is
//! answered by a scan.
inline bool IndexSearchOpFromName(const string &name, MeosType bbox_type, bool query_on_left,
                                  IndexSearchOp &result) {
	for (auto &entry : INDEX_OPERATORS) {
		if (name != entry.name || !IndexBboxHasAxis(bbox_type, IndexSearchAxis(entry.direct))) {
			continue;
		}
		if (query_on_left && !entry.has_commuted) {
			return false;
		}
		result = query_on_left ? entry.commuted : entry.direct;
		return true;
	}
	return false;
}

//! The operator names an index over `bbox_type` answers in at least one operand order: overlap and
//! containment compare whole extents, so every box type answers those, and an ordering operator is
//! answered only where the box carries the axis it orders along.
inline unordered_set<string> IndexOperatorNames(MeosType bbox_type) {
	unordered_set<string> names;
	for (auto &entry : INDEX_OPERATORS) {
		if (IndexBboxHasAxis(bbox_type, IndexSearchAxis(entry.direct))) {
			names.insert(entry.name);
		}
	}
	return names;
}

} // namespace duckdb
