#define DUCKDB_EXTENSION_MAIN

#include <cstdio>
#include "mobilityduck_extension.hpp"
#include "temporal/spanset.hpp"
#include "temporal/set.hpp"
#include "geo/geoset.hpp"
#include "temporal/temporal_functions.hpp"
#include "temporal/temporal.hpp"
#include "temporal/tbox.hpp"
#include "geo/stbox.hpp"
#include "geo/tgeompoint.hpp"
#include "geo/tgeogpoint.hpp"
#include "duckdb.hpp"
#include "geo/tgeometry.hpp"
#include "geo/tgeometry_ops.hpp"
#include "geo/tgeography.hpp"
#include "geo/tgeography_ops.hpp"
#include "pointcloud/pcschema.hpp"
#include "pointcloud/tpcbox.hpp"
#include "pointcloud/tpcpoint.hpp"
#include "pointcloud/tpcpatch.hpp"
#include "geo/tgeogpoint.hpp"
#include "geo/tgeogpoint_ops.hpp"
#include "quadbin/tquadbin.hpp"
#include "s2cell/ts2cell.hpp"
#include "raster/raquet.hpp"
#include "raster/raster.hpp"
#include "raster/raster_boundary.hpp"
#include "h3/th3index.hpp"
#include "cbuffer/tcbuffer.hpp"
#include "json/tjsonb.hpp"
#include "npoint/tnpoint.hpp"
#include "pose/tpose.hpp"
#include "rgeo/trgeometry.hpp"
#include "posechain/tposechain.hpp"
#include "temporal/span.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "index/rtree_module.hpp"
#include "index/sptree_module.hpp"
#include "temporal/temporal_parquet.hpp"

#include <mutex>
#include <exception>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <string>

#if defined(_WIN32)
  #include <sys/types.h>
  #include <sys/stat.h>
  #include <io.h>
  #define stat _stat
#else
  #include <sys/types.h>
  #include <sys/stat.h>
  #include <unistd.h>
#endif

extern "C" {
    #include <stdarg.h>
    #include <meos.h>
    #include <proj.h>
}

// OpenSSL linked through vcpkg
#include <openssl/opensslv.h>

#include "mobilityduck/meos_registration.hpp"

namespace duckdb {
#include "spatial_ref_sys_csv.inc"
#include "ways_csv.inc"

// =====================================================================
// 2. Utility: version scalar functions
// =====================================================================

inline void MobilityduckOpenSSLVersionScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &name_vector = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(name_vector, result, args.size(), [&](string_t name) {
		return StringVector::AddString(
		    result,
		    "Mobilityduck " + name.GetString() +
		        ", my linked OpenSSL version is " + OPENSSL_VERSION_TEXT
		);
	});
}

// The commit libmeos is built from. CMake reads it from the port that builds
// it, so the version reported is the one linked. The fallback names no commit:
// a SHA held here is a second copy of the pin, and only the port's is authoritative.
#ifndef MOBILITYDUCK_MEOS_PIN
#define MOBILITYDUCK_MEOS_PIN "unknown"
#endif

inline std::string MobilityduckShortVersion() {
#ifdef EXT_VERSION_MOBILITYDUCK
	return std::string("MobilityDuck ") + EXT_VERSION_MOBILITYDUCK;
#else
	return std::string("MobilityDuck");
#endif
}

inline std::string MobilityduckFullVersion() {
	std::string out = MobilityduckShortVersion();
	out += ", linked MEOS ";
	out += MOBILITYDUCK_MEOS_PIN;
	out += ", DuckDB ";
	out += DuckDB::LibraryVersion();
	out += ", PROJ ";
	out += std::to_string(PROJ_VERSION_MAJOR) + "." +
	       std::to_string(PROJ_VERSION_MINOR) + "." +
	       std::to_string(PROJ_VERSION_PATCH);
	// OPENSSL_VERSION_TEXT already starts with "OpenSSL", so just append it.
	out += ", ";
	out += OPENSSL_VERSION_TEXT;
	return out;
}

inline void MobilityduckVersionScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
	const std::string s = MobilityduckShortVersion();
	result.Reference(Value(s));
}

inline void MobilityduckFullVersionScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
	const std::string s = MobilityduckFullVersion();
	result.Reference(Value(s));
}

// =====================================================================
// 3. SRID CSV configuration for MEOS (embedded bytes)
// =====================================================================

#ifndef MDUCK_SRID_ENV_NAME
  #define MDUCK_SRID_ENV_NAME "SPATIAL_REF_SYS"
#endif

#ifndef MDUCK_WAYS_ENV_NAME
  #define MDUCK_WAYS_ENV_NAME "WAYS_CSV"
#endif

static bool file_exists(const char *p) {
    struct stat st {};
    return p && *p && (stat(p, &st) == 0);
}

static bool file_exists(const std::string &p) {
    return file_exists(p.c_str());
}

// A file in the system's directory for scratch files: TMPDIR (then TMP, TEMP)
// on POSIX, the user's scratch directory on Windows.
static std::string TempFilePath(const char *name) {
	return (std::filesystem::temp_directory_path() / name).string();
}

static std::string EnsureEmbeddedSridCsvOnDisk() {
    std::string path = TempFilePath("mobilityduck_spatial_ref_sys.csv");

    if (!file_exists(path)) {
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            throw duckdb::IOException(
                "mobilityduck: cannot write embedded spatial_ref_sys.csv to " + path
            );
        }
        out.write(
            reinterpret_cast<const char *>(spatial_ref_sys_csv),
            static_cast<std::streamsize>(spatial_ref_sys_csv_len)
        );
        out.close();
    }
    return path;
}

// Configure MEOS exactly once with a CSV path. No exception leaves the
// std::call_once: a failure is kept and rethrown to every caller, since a
// call_once whose function throws leaves later callers waiting on the flag
// under MinGW's runtime.
static void ConfigureMeosSridCsvOnce() {
    static std::once_flag once;
    static std::exception_ptr failure;
    std::call_once(once, [] {
        try {
            const char *env_path = std::getenv(MDUCK_SRID_ENV_NAME);
            const char *chosen   = nullptr;

            if (env_path && *env_path && file_exists(env_path)) {
                chosen = env_path;
            } else {
                static std::string embedded_path = EnsureEmbeddedSridCsvOnDisk();
                chosen = embedded_path.c_str();
            }

            if (chosen) {
                meos_set_spatial_ref_sys_csv(chosen);
            }
        } catch (...) {
            failure = std::current_exception();
        }
    });
    if (failure) {
        std::rethrow_exception(failure);
    }
}

// The network-point (npoint) family carries no SRID/geometry of its own: a route
// identifier is resolved against the `ways` network CSV (npoint_srid()=get_srid_ways(),
// route_geom() reads the CSV). Ship the canonical example `ways` network embedded and
// point MEOS at it, exactly as ConfigureMeosSridCsvOnce does for spatial_ref_sys.
static std::string EnsureEmbeddedWaysCsvOnDisk() {
    std::string path = TempFilePath("mobilityduck_ways.csv");

    if (!file_exists(path)) {
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            throw duckdb::IOException(
                "mobilityduck: cannot write embedded ways.csv to " + path
            );
        }
        out.write(
            reinterpret_cast<const char *>(ways_csv),
            static_cast<std::streamsize>(ways_csv_len)
        );
        out.close();
    }
    return path;
}

static void ConfigureMeosWaysCsvOnce() {
    static std::once_flag once;
    static std::exception_ptr failure;
    std::call_once(once, [] {
        try {
            const char *env_path = std::getenv(MDUCK_WAYS_ENV_NAME);
            const char *chosen   = nullptr;

            if (env_path && *env_path && file_exists(env_path)) {
                chosen = env_path;
            } else {
                static std::string embedded_path = EnsureEmbeddedWaysCsvOnDisk();
                chosen = embedded_path.c_str();
            }

            if (chosen) {
                meos_set_ways_csv(chosen);
            }
        } catch (...) {
            failure = std::current_exception();
        }
    });
    if (failure) {
        std::rethrow_exception(failure);
    }
}

// =====================================================================
// 4. MEOS error handler: translate MEOS_ERROR to a DuckDB exception
// =====================================================================

// MEOS's default handler calls exit(EXIT_FAILURE) on errlevel == ERROR,
// which would tear down the whole DuckDB process on any invalid input.
// Throw a DuckDB exception instead. Numeric levels follow PostgreSQL's
// elog values carried through via <postgres.h>: ERROR / FATAL / PANIC
// are fatal (>= 21); lower levels (WARNING / NOTICE / INFO) are
// informational and ignored.
static constexpr int MEOS_ERRLEVEL_ERROR = 21;

extern "C" void MobilityduckMeosErrorHandler(int errlevel, int errcode, const char *errmsg) {
    (void) errcode;
    if (errlevel >= MEOS_ERRLEVEL_ERROR) {
        throw duckdb::InvalidInputException(errmsg ? errmsg : "MEOS error");
    }
}

// =====================================================================
// 5. Extension load logic
// =====================================================================

// Defined in the catalog-generated src/generated/generated_temporal_udfs.cpp.
void RegisterGeneratedTemporalUdfs(ExtensionLoader &loader);
// Defined in the catalog-generated src/generated/generated_aggregates.cpp.
void RegisterGeneratedAggregates(ExtensionLoader &loader);

static void LoadInternal(ExtensionLoader &loader) {
	// Configure MEOS SRID CSV once (env / embedded)
	ConfigureMeosSridCsvOnce();

	// Configure the MEOS `ways` network CSV once (env / embedded) so the npoint
	// family resolves route SRID/geometry (npoint carries no SRID of its own).
	ConfigureMeosWaysCsvOnce();

	// Initialize MEOS once and install our error handler so MEOS errors
	// become DuckDB exceptions instead of exit()ing the process.
	static std::once_flag meos_init_flag;
    std::call_once(meos_init_flag, []() {
        meos_initialize();
        /* MEOS starts in MEOS_DEFAULT_TIMEZONE (meos_thread.hpp) and, from the
         * first MEOS call of a query on, reads and writes timestamps in the
         * zone of the session running it. */
        meos_initialize_timezone(MEOS_DEFAULT_TIMEZONE);
        meos_initialize_error_handler(&MobilityduckMeosErrorHandler);
    });

    // A new session starts in MEOS_DEFAULT_TIMEZONE, so a bare TIMESTAMPTZ and
    // a temporal value print in the same zone; `SET TimeZone` moves both, MEOS
    // following the session on each thread (meos_thread.hpp). The named zone
    // needs ICU.
    //
    // If ICU cannot be auto-loaded (no on-disk copy AND no network egress:
    // CI docker images, edge/musl deployments, offline installs), degrade
    // gracefully instead of failing the whole extension load.  Tests that
    // assert Brussels display stage ICU locally via the Makefile's stage_icu.
    auto &db = loader.GetDatabaseInstance();
    try {
        ExtensionHelper::AutoLoadExtension(db, "icu");
        auto &config = DBConfig::GetConfig(db);
        config.SetOptionByName("TimeZone", Value(MEOS_DEFAULT_TIMEZONE));
    } catch (const std::exception &e) {
        // ICU unavailable: the session states no TimeZone, so MEOS stays in
        // MEOS_DEFAULT_TIMEZONE while a bare TIMESTAMPTZ prints in UTC.
        fprintf(stderr,
                "mobilityduck: ICU not available (%s); session timezone left "
                "at default instead of %s.\n", e.what(), MEOS_DEFAULT_TIMEZONE);
    }


	// Register scalar function: mobilityduckOpenSSLVersion
	auto mobilityduck_openssl_version_scalar_function =
	    ScalarFunction(
	        "mobilityduckOpenSSLVersion",
	        {LogicalType::VARCHAR},
	        LogicalType::VARCHAR,
	        MobilityduckOpenSSLVersionScalarFun
	    );
	duckdb::RegisterMeosFunction(loader,  mobilityduck_openssl_version_scalar_function);

	duckdb::RegisterMeosFunction(loader, ScalarFunction(
		"mobilityduckVersion", {}, LogicalType::VARCHAR,
		MobilityduckVersionScalarFun));
	duckdb::RegisterMeosFunction(loader, ScalarFunction(
		"mobilityduckFullVersion", {}, LogicalType::VARCHAR,
		MobilityduckFullVersionScalarFun));
	// Portable-SQL aliases — MobilityDB publishes `mobilitydbVersion` and
	// `mobilitydbFullVersion`, so a query that probes for the engine version
	// runs unchanged across both. The `mobilitydb_*` spellings this extension
	// already publishes stay registered beside them.
	duckdb::RegisterMeosFunctionAs(loader, ScalarFunction(
		"mobilitydbVersion", {}, LogicalType::VARCHAR,
		MobilityduckVersionScalarFun), {"mobilitydb_version"});
	duckdb::RegisterMeosFunctionAs(loader, ScalarFunction(
		"mobilitydbFullVersion", {}, LogicalType::VARCHAR,
		MobilityduckFullVersionScalarFun), {"mobilitydb_full_version"});

	// Temporal and related types/functions
	TemporalTypes::RegisterTypes(loader);
	TemporalTypes::RegisterCastFunctions(loader);
	TemporalTypes::RegisterScalarFunctions(loader);

	TboxType::RegisterType(loader);
	TboxType::RegisterCastFunctions(loader);
	TboxType::RegisterScalarFunctions(loader);

	StboxType::RegisterType(loader);
	StboxType::RegisterCastFunctions(loader);
	StboxType::RegisterScalarFunctions(loader);

	SpanTypes::RegisterTypes(loader);
	SpanTypes::RegisterCastFunctions(loader);

	TgeompointType::RegisterType(loader);
	TgeompointType::RegisterCastFunctions(loader);
	TgeompointType::RegisterScalarFunctions(loader);

	TgeogpointType::RegisterType(loader);
	TgeogpointType::RegisterCastFunctions(loader);
	TgeogpointType::RegisterScalarFunctions(loader);

	TGeometryTypes::RegisterScalarFunctions(loader);
	TGeometryTypes::RegisterTypes(loader);
	TGeometryTypes::RegisterCastFunctions(loader);
	TGeometryOps::RegisterScalarFunctions(loader);

	TGeographyTypes::RegisterTypes(loader);
	TGeographyTypes::RegisterScalarFunctions(loader);
	TGeographyTypes::RegisterCastFunctions(loader);
	TGeographyOps::RegisterScalarFunctions(loader);

	TGeogpointType::RegisterScalarFunctions(loader);
	TGeogpointType::RegisterCastFunctions(loader);
	TGeogpointOps::RegisterScalarFunctions(loader);

	// What a point cloud value's pcid names, stated as rows of two tables a
	// database holds, reaches the engine here (requires the MEOS POINTCLOUD
	// module), so a coordinate of such a value is answerable.
	PcschemaFunctions::RegisterScalarFunctions(loader);

	// Bounding box of a temporal point cloud (requires the MEOS POINTCLOUD module).
	TpcboxType::RegisterType(loader);
	TpcboxType::RegisterCastFunctions(loader);

	// Extended temporal type tpcpoint (requires the MEOS POINTCLOUD module).
	TPcpointTypes::RegisterTypes(loader);
	TPcpointTypes::RegisterCastFunctions(loader);
	TPcpointTypes::RegisterScalarFunctions(loader);

	// Extended temporal type tpcpatch (requires the MEOS POINTCLOUD module).
	TPcpatchTypes::RegisterTypes(loader);
	TPcpatchTypes::RegisterCastFunctions(loader);
	TPcpatchTypes::RegisterScalarFunctions(loader);

	SetTypes::RegisterTypes(loader);
	SetTypes::RegisterCastFunctions(loader);

	SpatialSetType::RegisterTypes(loader);
	SpatialSetType::RegisterCastFunctions(loader);

	QuadbinTypes::RegisterTypes(loader);
	QuadbinTypes::RegisterCastFunctions(loader);

	S2cellTypes::RegisterTypes(loader);
	S2cellTypes::RegisterCastFunctions(loader);

	// Raster tiles addressed by a QUADBIN cell; registered after the quadbin,
	// tgeompoint and stbox types its surface refers to.
	RaquetTypes::RegisterTypes(loader);
	RaquetTypes::RegisterCastFunctions(loader);

	// MEOS's raster: type registration and text casts are the hand layer, its
	// function surface is generated.
	RasterTypes::RegisterTypes(loader);
	RasterTypes::RegisterCastFunctions(loader);
	// The boundary to DuckDB's raster extension (duckdb-raster): a raster from
	// a tile, the datacube and the metadata of a raster, the statistics.
	RasterBoundary::Register(loader);

	// H3 cell index types (h3index/th3index). Type registration + casts are the
	// hand layer; the H3 function surface is generated (no RegisterScalarFunctions).
	H3indexTypes::RegisterTypes(loader);
	H3indexTypes::RegisterCastFunctions(loader);

	CbufferTypes::RegisterTypes(loader);
	CbufferTypes::RegisterCastFunctions(loader);
	NpointTypes::RegisterTypes(loader);
	NpointTypes::RegisterCastFunctions(loader);
	PoseTypes::RegisterTypes(loader);
	PoseTypes::RegisterCastFunctions(loader);
	// The temporal rigid geometry, after the pose type its base value is.
	TrgeometryTypes::RegisterTypes(loader);
	TrgeometryTypes::RegisterCastFunctions(loader);
	// The pose chain, after the pose type its links are.
	PosechainTypes::RegisterTypes(loader);
	PosechainTypes::RegisterCastFunctions(loader);

	SpansetTypes::RegisterTypes(loader);
	SpansetTypes::RegisterCastFunctions(loader);

	TJsonbTypes::RegisterTypes(loader);
	TJsonbTypes::RegisterCastFunctions(loader);
	TJsonbTypes::RegisterScalarFunctions(loader);

	TRTreeModule::RegisterRTreeIndex(loader);
	TRTreeModule::RegisterIndexScan(loader);
	TRTreeModule::RegisterScanOptimizer(loader);
	TRTreeModule::RegisterJoinOptimizer(loader);

	TSPTreeModule::RegisterSPTreeIndex(loader);
	TSPTreeModule::RegisterIndexScan(loader);
	TSPTreeModule::RegisterScanOptimizer(loader);
	TSPTreeModule::RegisterJoinOptimizer(loader);

	// Catalog-driven scalar UDFs generated from the MEOS-API catalog, organized by
	// doxygen @ingroup group. Registered last, after every type accessor exists.
	RegisterGeneratedTemporalUdfs(loader);
	// The aggregates of the MEOS catalog, over the same types.
	RegisterGeneratedAggregates(loader);
	// TemporalParquet footer helper for COPY ... TO '*.parquet' KV_METADATA.
	TemporalParquetFunctions::Register(loader);
}

// MobilityDuck links no spatial extension: it loads the one DuckDB publishes
// that the user has installed, the way LoadInternal loads ICU, and reports
// rather than fails when none is available. Its object never names
// ExtensionHelper::LoadExtension, which a linker that keeps every function of an
// object (MinGW's) would otherwise fail to resolve.
static void LoadSpatial(DatabaseInstance &db) {
	try {
		ExtensionHelper::AutoLoadExtension(db, "spatial");
	} catch (const std::exception &e) {
		fprintf(stderr,
		        "mobilityduck: spatial extension not available (%s); its "
		        "functions are not loaded.\n", e.what());
	}
}

// DuckDB runs extension optimizers in the order they register, so MobilityDuck
// registers its own before it loads the spatial extension, and its index joins
// take a && join ahead of spatial's join rewrite.
void MobilityduckExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
	LoadSpatial(loader.GetDatabaseInstance());
}

std::string MobilityduckExtension::Name() {
	return "mobilityduck";
}

std::string MobilityduckExtension::Version() const {
#ifdef EXT_VERSION_MOBILITYDUCK
	return EXT_VERSION_MOBILITYDUCK;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

// The entry point of the loadable extension alone, guarded as DuckDB guards its
// own (duckdb/extension/parquet/parquet_extension.cpp).
#if defined(DUCKDB_BUILD_LOADABLE_EXTENSION)
DUCKDB_CPP_EXTENSION_ENTRY(mobilityduck, loader) {
	duckdb::LoadInternal(loader);
	duckdb::LoadSpatial(loader.GetDatabaseInstance());
}
#endif

DUCKDB_EXTENSION_API const char *mobilityduck_version() {
	return duckdb::DuckDB::LibraryVersion();
}

} // extern "C"

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif
