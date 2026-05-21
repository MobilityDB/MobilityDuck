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
            // NULL endian => default platform endianness (NDR / little
            // on x86_64 / arm64).
            uint8_t *wkb = geo_as_ewkb(gs, /*endian=*/ nullptr, &wkb_size);
            if (!wkb || wkb_size == 0) {
                free(gs);
                throw InternalException("ST_AsBinary: MEOS geo_as_ewkb returned empty buffer");
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
}

} // namespace duckdb
