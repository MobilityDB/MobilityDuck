#pragma once

#include "meos_wrapper_simple.hpp"
#include "duckdb/common/allocator.hpp"
#include "duckdb/common/vector.hpp"
#include "duckdb/storage/arena_allocator.hpp"
#include "duckdb/execution/expression_executor_state.hpp"
#include "duckdb_version_compat.hpp"
#if !MOBILITYDUCK_DUCKDB_AT_LEAST(1, 5)
#include "spatial/geometry/geometry_serialization.hpp"
#include "spatial/geometry/sgl.hpp"
#endif

namespace duckdb {

inline GSERIALIZED* GeometryToGSerialized(string_t geometry_blob, int32_t srid) {
    vector<data_t> wkb_buffer;
    MobilityDuckGeometryToWKB(geometry_blob, wkb_buffer);
    if (wkb_buffer.empty()) {
        throw InvalidInputException("Failed to convert GEOMETRY to WKB: buffer is empty");
    }
    const uint8_t *wkb_data = wkb_buffer.data();
    size_t wkb_size = wkb_buffer.size();
    GSERIALIZED *gs = geo_from_ewkb(wkb_data, wkb_size, (int32)srid);
    if (!gs) {
        throw InvalidInputException("Failed to parse WKB into a geometry");
    }
    return gs;
}

inline string_t GSerializedToGeometry(const GSERIALIZED *gs, ArenaAllocator &arena, Vector &result) {
    if (!gs) {
        throw InvalidInputException("Null GSERIALIZED input");
    }

    size_t ewkb_size = 0;
    auto *ewkb_data = geo_as_ewkb(gs, NULL, &ewkb_size);
    if (!ewkb_data || ewkb_size == 0) {
        throw InvalidInputException("Failed to convert GSERIALIZED to EWKB");
    }

#if MOBILITYDUCK_DUCKDB_AT_LEAST(1, 5)
    // DuckDB core reads EWKB itself: it drops the SRID and writes the
    // little-endian ISO WKB its GEOMETRY type stores, so a loadable extension
    // needs none of the spatial extension's code for this conversion.
    (void)arena;
    string_t blob;
    const string_t ewkb(const_char_ptr_cast(ewkb_data), static_cast<uint32_t>(ewkb_size));
    try {
        Geometry::FromBinary(ewkb, blob, result, /*strict=*/true);
    } catch (...) {
        free(ewkb_data);
        throw;
    }
    free(ewkb_data);
    return blob;
#else
    GeometryAllocator alloc(arena);
    sgl::wkb_reader reader(alloc);
    reader.set_allow_mixed_zm(true);
    reader.set_nan_as_empty(true);

    sgl::geometry geom;
    if (!reader.try_parse(geom, reinterpret_cast<const char *>(ewkb_data), ewkb_size)) {
        const auto error = reader.get_error_message();
        free(ewkb_data);
        throw InvalidInputException("Could not parse EWKB input: %s", error);
    }

    if (reader.parsed_mixed_zm()) {
        sgl::ops::force_zm(alloc, geom, reader.parsed_any_z(), reader.parsed_any_m(), 0, 0);
    }

    const auto size = Serde::GetRequiredSize(geom);
    auto blob = StringVector::EmptyString(result, size);
    Serde::Serialize(geom, blob.GetDataWriteable(), size);
    blob.Finalize();

    free(ewkb_data);
    return blob;
#endif
}

inline string_t GSerializedToGeometry(const GSERIALIZED *gs, ExpressionState &state, Vector &result) {
    ArenaAllocator arena(BufferAllocator::Get(state.GetContext()));
    return GSerializedToGeometry(gs, arena, result);
}

} // namespace duckdb
