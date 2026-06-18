/* MobilityDuck binding for MEOS Raquet raster chip-sampling functions.
 *
 * Two MEOS kernel functions are exposed:
 *   rasterTileValueQuadbin — sample a Raquet band BLOB along a tgeompoint
 *                            trajectory, keyed by QUADBIN cell; returns TFLOAT.
 *   trajectoryQuadbins     — list the QUADBIN cells (at a zoom level) that a
 *                            tgeompoint trajectory crosses; used as the
 *                            WHERE-clause join key against a Raquet table.
 */

#pragma once

#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

struct RasterQuadbinFunctions {
    static void Raster_tile_value_quadbin(
        DataChunk &args, ExpressionState &state, Vector &result);
    static void Trajectory_quadbins(
        DataChunk &args, ExpressionState &state, Vector &result);
    static void RegisterScalarFunctions(ExtensionLoader &loader);
};

} // namespace duckdb
