// MobilityDuck `GEOGRAPHY` I/O UDFs — implementation.  See
// `doc/geography-boundary.md` for the boundary design.
//
// The `GEOGRAPHY` LogicalType is a BLOB whose bytes are the raw GSERIALIZED
// struct (varlena layout — `VARSIZE(gs)` total bytes, including the 4-byte
// varlena header).  Storing raw GSERIALIZED bytes preserves the geodetic
// flag in the type tag across the DuckDB columnar boundary, which standard
// EWKB does not carry.
//
// Include order mirrors the existing static-type pattern (see stbox_functions.cpp):
// meos_wrapper_simple.hpp first so meos.h's Interval/Timestamp typedefs land
// in C linkage before any DuckDB header pulls in the duckdb:: variants.
#include "meos_wrapper_simple.hpp"

#include "common.hpp"
#include "geo/geography.hpp"
#include "geo/geography_functions.hpp"
#include "geo/tgeogpoint.hpp"
#include "geo_util.hpp"
#include "spatial/spatial_types.hpp"
#include "tydef.hpp"

#include "duckdb/common/types/blob.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include <cstring>
#include <string>

extern "C" {
    #include <meos_internal.h>  // MEOS_FLAGS_SET_GEODETIC
}

namespace duckdb {

// ----- BLOB <-> GSERIALIZED helpers -------------------------------------

// Allocate + copy GSERIALIZED into a DuckDB BLOB string_t.  Caller owns
// `gs` and remains responsible for freeing it.  The varlena VARSIZE macro
// gives the total byte size (4-byte header + payload).
static string_t SerializeGserializedToBlob(const GSERIALIZED *gs, Vector &result) {
    size_t gs_size = VARSIZE(gs);
    uint8_t *gs_data = static_cast<uint8_t *>(malloc(gs_size));
    if (!gs_data) {
        throw InternalException("GeographyFunctions: failed to allocate %zu bytes for GEOGRAPHY blob", gs_size);
    }
    std::memcpy(gs_data, gs, gs_size);
    string_t blob(reinterpret_cast<const char *>(gs_data), gs_size);
    string_t stored = StringVector::AddStringOrBlob(result, blob);
    free(gs_data);
    return stored;
}

// Read a GEOGRAPHY BLOB into a GSERIALIZED pointer that the caller owns
// (must `free` it).  The pointer aliases a fresh heap copy of the BLOB
// payload — the original BLOB string_t may be backed by a non-owning
// vector buffer, so copying is required before MEOS functions touch it.
static GSERIALIZED *DeserializeBlobToGserialized(string_t input) {
    size_t data_size = input.GetSize();
    if (data_size < sizeof(uint32_t)) {
        throw InvalidInputException("GEOGRAPHY blob is too small to be a valid GSERIALIZED (got %zu bytes)", data_size);
    }
    uint8_t *gs_copy = static_cast<uint8_t *>(malloc(data_size));
    if (!gs_copy) {
        throw InternalException("GeographyFunctions: failed to allocate %zu bytes to deserialize GEOGRAPHY blob", data_size);
    }
    std::memcpy(gs_copy, input.GetData(), data_size);
    return reinterpret_cast<GSERIALIZED *>(gs_copy);
}

// ----- ST_GeogFromText (VARCHAR -> GEOGRAPHY) ----------------------------

void GeographyFunctions::ST_GeogFromText(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input, ValidityMask &mask, idx_t idx) -> string_t {
            std::string s(input.GetData(), input.GetSize());
            // typmod == -1: no schema-imposed modifier; geog_in parses the
            // EWKT verbatim and sets the geodetic flag based on the
            // resulting type tag + SRID.
            GSERIALIZED *gs = geog_in(s.c_str(), -1);
            if (!gs) {
                throw InvalidInputException("ST_GeogFromText: MEOS geog_in failed on `%s`", s.c_str());
            }
            string_t blob = SerializeGserializedToBlob(gs, result);
            free(gs);
            return blob;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

// ----- ST_AsText (GEOGRAPHY -> VARCHAR) ----------------------------------

void GeographyFunctions::ST_AsText(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            GSERIALIZED *gs = DeserializeBlobToGserialized(input);
            // EWKT carries the SRID prefix; ST_GeogFromText (round-trip)
            // uses the SRID to re-assert geodetic-ness, so the trip is
            // lossless wrt the geodetic flag.
            char *txt = geo_as_ewkt(gs, /*precision=*/ 6);
            if (!txt) {
                free(gs);
                throw InternalException("ST_AsText: MEOS geo_as_ewkt returned NULL");
            }
            std::string s(txt);
            string_t out = StringVector::AddString(result, s);
            free(txt);
            free(gs);
            return out;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

// ----- ST_AsBinary (GEOGRAPHY -> BLOB) -----------------------------------

void GeographyFunctions::ST_AsBinary(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            GSERIALIZED *gs = DeserializeBlobToGserialized(input);
            size_t wkb_size = 0;
            // WKB_EXTENDED variant (EWKB, default platform endianness),
            // preserving the prior geo_as_ewkb behaviour after the
            // geo_as_ewkb -> geo_as_wkb(variant) MEOS API consolidation.
            uint8_t *wkb = geo_as_wkb(gs, WKB_EXTENDED, &wkb_size);
            if (!wkb || wkb_size == 0) {
                free(gs);
                throw InternalException("ST_AsBinary: MEOS geo_as_wkb returned empty buffer");
            }
            string_t blob(reinterpret_cast<const char *>(wkb), wkb_size);
            string_t stored = StringVector::AddStringOrBlob(result, blob);
            free(wkb);
            free(gs);
            return stored;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

// ----- ST_GeogFromBinary (BLOB -> GEOGRAPHY) -----------------------------

void GeographyFunctions::ST_GeogFromBinary(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            const uint8_t *wkb = reinterpret_cast<const uint8_t *>(input.GetData());
            size_t wkb_size = input.GetSize();
            if (wkb_size == 0) {
                throw InvalidInputException("ST_GeogFromBinary: empty WKB input");
            }
            // SRID 0: defer to the WKB header's SRID (geo_from_ewkb honours
            // the SRID embedded in EWKB).  The result is a geometry-flagged
            // GSERIALIZED; we explicitly set the geodetic flag, which
            // standard EWKB does not carry.
            GSERIALIZED *gs = geo_from_ewkb(wkb, wkb_size, /*srid=*/ 0);
            if (!gs) {
                throw InvalidInputException("ST_GeogFromBinary: MEOS geo_from_ewkb failed to parse %zu-byte WKB", wkb_size);
            }
            MEOS_FLAGS_SET_GEODETIC(gs->gflags, true);
            string_t blob = SerializeGserializedToBlob(gs, result);
            free(gs);
            return blob;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

// ----- GEOMETRY <-> GEOGRAPHY casts --------------------------------------

// GEOMETRY (sgl serde) -> GEOGRAPHY (raw GSERIALIZED, geodetic flag set).
// `GeometryToGSerialized` parses the WKB the sgl serde emits via
// `WKBWriter::Write`; SRID 0 lets the WKB header carry the SRID. The
// geodetic flag is set explicitly on the resulting GSERIALIZED.
bool GeographyFunctions::Geometry_to_geography_cast(Vector &source, Vector &result,
                                                     idx_t count, CastParameters &) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t geom_blob) -> string_t {
            GSERIALIZED *gs = GeometryToGSerialized(geom_blob, /*srid=*/ 0);
            MEOS_FLAGS_SET_GEODETIC(gs->gflags, true);
            string_t blob = SerializeGserializedToBlob(gs, result);
            free(gs);
            return blob;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
    return true;
}

// GEOGRAPHY (raw GSERIALIZED) -> GEOMETRY (sgl serde). The geodetic flag
// is cleared so downstream GEOMETRY consumers see a flat geometry; SRID
// is preserved (it lives in the GSERIALIZED header).
bool GeographyFunctions::Geography_to_geometry_cast(Vector &source, Vector &result,
                                                     idx_t count, CastParameters &) {
    // A per-call arena is sufficient: each sgl serialization writes into
    // a fresh DuckDB string_t via `StringVector::EmptyString`, the arena
    // backs only the intermediate sgl geometry graph.
    ArenaAllocator arena(Allocator::DefaultAllocator());
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t geog_blob) -> string_t {
            GSERIALIZED *gs = DeserializeBlobToGserialized(geog_blob);
            MEOS_FLAGS_SET_GEODETIC(gs->gflags, false);
            string_t out = GSerializedToGeometry(gs, arena, result);
            free(gs);
            return out;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
    return true;
}

// ----- Scalar operations -------------------------------------------------

// Read a Temporal* from the binary input vector (mirrors the existing
// pattern in tgeogpoint_functions.cpp). The caller owns the returned
// pointer.
static Temporal *DeserializeBlobToTemporal(string_t input) {
    size_t data_size = input.GetSize();
    uint8_t *buf = static_cast<uint8_t *>(malloc(data_size));
    if (!buf) {
        throw InternalException("GeographyFunctions: failed to allocate %zu bytes for Temporal", data_size);
    }
    std::memcpy(buf, input.GetData(), data_size);
    return reinterpret_cast<Temporal *>(buf);
}

void GeographyFunctions::ST_Length(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, double>(
        args.data[0], result, args.size(),
        [&](string_t input) -> double {
            GSERIALIZED *gs = DeserializeBlobToGserialized(input);
            // use_spheroid=true: WGS84 ellipsoidal geodesics, matching the
            // MEOS-on-Postgres default for the geography flavour.
            double length = geog_length(gs, /*use_spheroid=*/ true);
            free(gs);
            return length;
        }
    );
}

void GeographyFunctions::ST_Area(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, double>(
        args.data[0], result, args.size(),
        [&](string_t input) -> double {
            GSERIALIZED *gs = DeserializeBlobToGserialized(input);
            double area = geog_area(gs, /*use_spheroid=*/ true);
            free(gs);
            return area;
        }
    );
}

void GeographyFunctions::EIntersects_tgeo_geog(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t temp_blob, string_t geog_blob) -> bool {
            Temporal *temp = DeserializeBlobToTemporal(temp_blob);
            GSERIALIZED *gs = DeserializeBlobToGserialized(geog_blob);
            int r = eintersects_tgeo_geo(temp, gs);
            free(gs);
            free(temp);
            return r == 1;
        }
    );
}

void GeographyFunctions::NAD_tgeo_geog(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, double>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t temp_blob, string_t geog_blob) -> double {
            Temporal *temp = DeserializeBlobToTemporal(temp_blob);
            GSERIALIZED *gs = DeserializeBlobToGserialized(geog_blob);
            double d = nad_tgeo_geo(temp, gs);
            free(gs);
            free(temp);
            return d;
        }
    );
}

// ----- Registration ------------------------------------------------------

void GeographyFunctions::RegisterScalarFunctions(ExtensionLoader &loader) {
    loader.RegisterFunction(
        ScalarFunction("ST_GeogFromText",
                       {LogicalType::VARCHAR},
                       GeographyType::GEOGRAPHY(),
                       ST_GeogFromText));
    loader.RegisterFunction(
        ScalarFunction("ST_AsText",
                       {GeographyType::GEOGRAPHY()},
                       LogicalType::VARCHAR,
                       ST_AsText));
    loader.RegisterFunction(
        ScalarFunction("ST_AsBinary",
                       {GeographyType::GEOGRAPHY()},
                       LogicalType::BLOB,
                       ST_AsBinary));
    loader.RegisterFunction(
        ScalarFunction("ST_GeogFromBinary",
                       {LogicalType::BLOB},
                       GeographyType::GEOGRAPHY(),
                       ST_GeogFromBinary));
    loader.RegisterFunction(
        ScalarFunction("ST_Length",
                       {GeographyType::GEOGRAPHY()},
                       LogicalType::DOUBLE,
                       ST_Length));
    loader.RegisterFunction(
        ScalarFunction("ST_Area",
                       {GeographyType::GEOGRAPHY()},
                       LogicalType::DOUBLE,
                       ST_Area));
    loader.RegisterFunction(
        ScalarFunction("eIntersects",
                       {TGeogpointType::TGEOGPOINT(), GeographyType::GEOGRAPHY()},
                       LogicalType::BOOLEAN,
                       EIntersects_tgeo_geog));
    loader.RegisterFunction(
        ScalarFunction("nearestApproachDistance",
                       {TGeogpointType::TGEOGPOINT(), GeographyType::GEOGRAPHY()},
                       LogicalType::DOUBLE,
                       NAD_tgeo_geog));
}

void GeographyFunctions::RegisterCastFunctions(ExtensionLoader &loader) {
    loader.RegisterCastFunction(
        GeoTypes::GEOMETRY(),
        GeographyType::GEOGRAPHY(),
        Geometry_to_geography_cast);
    loader.RegisterCastFunction(
        GeographyType::GEOGRAPHY(),
        GeoTypes::GEOMETRY(),
        Geography_to_geometry_cast);
}

} // namespace duckdb
