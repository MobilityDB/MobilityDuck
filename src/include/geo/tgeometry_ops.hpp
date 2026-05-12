#pragma once

#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

// Cross-type predicate / operator registrations for tgeometry — boxops
// (overlaps / contains / contained / same / adjacent), position predicates
// (left / right / below / above / front / back / before / after and their
// over-* variants), spatial relationships (e / a / t variants of contains
// / disjoint / intersects / touches / dwithin), and the temporal distance
// operator family. Mirrors the corresponding cross-type surface that
// MobilityDB ships in 060/062/064/070/072_tgeo_*.in.sql.
struct TGeometryOps {
    static void RegisterScalarFunctions(ExtensionLoader &loader);
};

} // namespace duckdb
