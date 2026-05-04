#pragma once

#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

// Single-tile getter functions: given a (value, timestamp, point) and a tile
// grid configuration (size + origin), return the single tile/box that
// contains the input.
//
// These complement the LIST<TBOX>/LIST<STBOX> emitters (`valueTiles`,
// `timeTiles`, `valueTimeTiles`, `spaceTiles`, ...) by exposing the
// per-input lookup form that MobilityDB ships at the SQL surface.
struct SingleTileGetters {
    static void RegisterScalarFunctions(ExtensionLoader &loader);
};

} // namespace duckdb
