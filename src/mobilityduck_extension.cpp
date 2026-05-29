#define DUCKDB_EXTENSION_MAIN

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
#include "geo/tgeogpoint.hpp"
#include "geo/tgeogpoint_ops.hpp"
#include "temporal/span.hpp"
#include "temporal/span_aggregates.hpp"
#include "temporal/temporal_aggregates.hpp"
#include "geo/spatial_aggregates.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "index/rtree_module.hpp"
#include "single_tile_getters.hpp"

#include <mutex>
#include <fstream>
#include <cstdlib>
#include <cstdio>
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
    #include <geos_c.h>
    #include <meos.h>
    #include <proj.h>
}

// OpenSSL linked through vcpkg
#include <openssl/opensslv.h>

#include "mobilityduck/meos_exec_serial.hpp"

namespace duckdb {
#include "spatial_ref_sys_csv.inc"

// =====================================================================
// 2. Utility: version scalar functions
// =====================================================================

static void MobilityduckOpenSSLVersionScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &name_vector = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(name_vector, result, args.size(), [&](string_t name) {
		return StringVector::AddString(
		    result,
		    "Mobilityduck " + name.GetString() +
		        ", my linked OpenSSL version is " + OPENSSL_VERSION_TEXT
		);
	});
}

// Keep this short pin in sync with vcpkg_ports/meos/portfile.cmake's REF.
// MEOS does not expose a runtime version symbol, so the build-time pin
// is the most precise version stamp the extension can report.
#ifndef MOBILITYDUCK_MEOS_PIN
#define MOBILITYDUCK_MEOS_PIN "f11b7443e"
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
	out += ", GEOS ";
	out += GEOSversion();
	out += ", PROJ ";
	out += std::to_string(PROJ_VERSION_MAJOR) + "." +
	       std::to_string(PROJ_VERSION_MINOR) + "." +
	       std::to_string(PROJ_VERSION_PATCH);
	// OPENSSL_VERSION_TEXT already starts with "OpenSSL", so just append it.
	out += ", ";
	out += OPENSSL_VERSION_TEXT;
	return out;
}

static void MobilityduckVersionScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
	const std::string s = MobilityduckShortVersion();
	result.Reference(Value(s));
}

static void MobilityduckFullVersionScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
	const std::string s = MobilityduckFullVersion();
	result.Reference(Value(s));
}

// =====================================================================
// 3. SRID CSV configuration for MEOS (embedded bytes)
// =====================================================================

#ifndef MDUCK_SRID_ENV_NAME
  #define MDUCK_SRID_ENV_NAME "SPATIAL_REF_SYS"
#endif

static bool file_exists(const char *p) {
    struct stat st {};
    return p && *p && (stat(p, &st) == 0);
}

static bool file_exists(const std::string &p) {
    return file_exists(p.c_str());
}

static std::string GetTempDir() {
	const char *tmp = std::getenv("TMPDIR");
	if (!tmp || !*tmp) {
		tmp = "/tmp";
	}
	return std::string(tmp);
}

static std::string EnsureEmbeddedSridCsvOnDisk() {
    std::string dir  = GetTempDir();
    std::string path = dir + "/mobilityduck_spatial_ref_sys.csv";

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

// Configure MEOS exactly once with a CSV path
static void ConfigureMeosSridCsvOnce() {
    static std::once_flag once;
    std::call_once(once, [] {
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
    });
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

static void LoadInternal(ExtensionLoader &loader) {
	// Configure MEOS SRID CSV once (env / embedded)
	ConfigureMeosSridCsvOnce();

	// Initialize MEOS once and install our error handler so MEOS errors
	// become DuckDB exceptions instead of exit()ing the process.
	static std::once_flag meos_init_flag;
    std::call_once(meos_init_flag, []() {
        meos_initialize();
        /* Set the MEOS timezone to Europe/Brussels so that all temporal-type
         * text I/O uses a consistent, named timezone on every platform.
         * Brussels is a non-UTC zone that surfaces bugs hidden by UTC (e.g.
         * off-by-one-hour errors in timestamp handling). */
        meos_initialize_timezone("Europe/Brussels");
        meos_initialize_error_handler(&MobilityduckMeosErrorHandler);
    });

    // Single-timezone model: ensure DuckDB's session timezone matches the
    // MEOS timezone so bare TIMESTAMPTZ display agrees with MEOS composite
    // type strings.  This needs ICU for the named "Europe/Brussels" zone.
    //
    // If ICU cannot be auto-loaded (no on-disk copy AND no network egress:
    // CI docker images, edge/musl deployments, offline installs), degrade
    // gracefully to the session default instead of failing the whole
    // extension load.  Tests that assert Brussels display stage ICU locally
    // via the Makefile's stage_icu.
    auto &db = loader.GetDatabaseInstance();
    try {
        ExtensionHelper::AutoLoadExtension(db, "icu");
        auto &config = DBConfig::GetConfig(db);
        config.SetOptionByName("TimeZone", Value("Europe/Brussels"));
    } catch (const std::exception &e) {
        // ICU unavailable: leave the session timezone at its default.
        // Temporal-type text I/O is unaffected; only bare TIMESTAMPTZ display
        // falls back to UTC.
        fprintf(stderr,
                "mobilityduck: ICU not available (%s); session timezone left "
                "at default instead of Europe/Brussels.\n", e.what());
    }


	// Register scalar function: mobilityduck_openssl_version
	auto mobilityduck_openssl_version_scalar_function =
	    ScalarFunction(
	        "mobilityduck_openssl_version",
	        {LogicalType::VARCHAR},
	        LogicalType::VARCHAR,
	        MobilityduckOpenSSLVersionScalarFun
	    );
	duckdb::RegisterSerializedScalarFunction(loader,  mobilityduck_openssl_version_scalar_function);

	duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
		"mobilityduck_version", {}, LogicalType::VARCHAR,
		MobilityduckVersionScalarFun));
	duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
		"mobilityduck_full_version", {}, LogicalType::VARCHAR,
		MobilityduckFullVersionScalarFun));
	// Portable-SQL aliases — match MobilityDB's `mobilitydb_*` names so
	// queries that probe for engine version work unchanged across both.
	duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
		"mobilitydb_version", {}, LogicalType::VARCHAR,
		MobilityduckVersionScalarFun));
	duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
		"mobilitydb_full_version", {}, LogicalType::VARCHAR,
		MobilityduckFullVersionScalarFun));

	// Temporal and related types/functions
	TemporalTypes::RegisterTypes(loader);
	TemporalTypes::RegisterCastFunctions(loader);
	TemporalTypes::RegisterScalarFunctions(loader);
	TemporalTypes::RegisterTemporalUnnestFunction(loader);
	TemporalTypes::RegisterWkbFunctions(loader);
	TemporalTypes::RegisterTemporalTileSplit(loader);
	TemporalTypes::RegisterTnumberValueSplit(loader);
	TemporalTypes::RegisterSimilarityPath(loader);

	TboxType::RegisterType(loader);
	TboxType::RegisterCastFunctions(loader);
	TboxType::RegisterScalarFunctions(loader);

	StboxType::RegisterType(loader);
	StboxType::RegisterCastFunctions(loader);
	StboxType::RegisterScalarFunctions(loader);

	SpanTypes::RegisterScalarFunctions(loader);
	SpanTypes::RegisterTypes(loader);
	SpanTypes::RegisterCastFunctions(loader);
	SpanAggregates::RegisterAggregateFunctions(loader);
	TemporalAggregates::RegisterAggregateFunctions(loader);

	// Tile getters return SpanTypes blobs and consume TBOX, so all those
	// types must already be registered.
	TemporalTypes::RegisterTileGetters(loader);

	TgeompointType::RegisterType(loader);
	TgeompointType::RegisterCastFunctions(loader);
	TgeompointType::RegisterScalarFunctions(loader);
	TgeompointType::RegisterRoundtripIO(loader);
	TgeompointType::RegisterTpointSplit(loader);
	TgeompointType::RegisterAnalyticsViz(loader);

	TgeogpointType::RegisterType(loader);
	TgeogpointType::RegisterCastFunctions(loader);
	TgeogpointType::RegisterScalarFunctions(loader);
	TgeogpointType::RegisterRoundtripIO(loader);

	TGeometryTypes::RegisterScalarFunctions(loader);
	TGeometryTypes::RegisterTypes(loader);
	TGeometryTypes::RegisterCastFunctions(loader);
	TGeometryTypes::RegisterScalarInOutFunctions(loader);
	TGeometryOps::RegisterScalarFunctions(loader);

	TGeographyTypes::RegisterTypes(loader);
	TGeographyTypes::RegisterScalarFunctions(loader);
	TGeographyTypes::RegisterCastFunctions(loader);
	TGeographyTypes::RegisterScalarInOutFunctions(loader);
	TGeographyOps::RegisterScalarFunctions(loader);

	TGeogpointType::RegisterScalarFunctions(loader);
	TGeogpointType::RegisterCastFunctions(loader);
	TGeogpointType::RegisterScalarInOutFunctions(loader);
	TGeogpointOps::RegisterScalarFunctions(loader);

	SetTypes::RegisterTypes(loader);
	SetTypes::RegisterCastFunctions(loader);
	SetTypes::RegisterScalarFunctions(loader);
	SetTypes::RegisterSetUnnest(loader);

	SpatialSetType::RegisterTypes(loader);
	SpatialSetType::RegisterCastFunctions(loader);
	SpatialSetType::RegisterScalarFunctions(loader);

	SpansetTypes::RegisterTypes(loader);
	SpansetTypes::RegisterCastFunctions(loader);
	SpansetTypes::RegisterScalarFunctions(loader);

	TRTreeModule::RegisterRTreeIndex(loader);
	TRTreeModule::RegisterIndexScan(loader);
	TRTreeModule::RegisterScanOptimizer(loader);

	// Single-tile getters depend on TBOX, STBOX, and the spatial GEOMETRY
	// type being registered first.
	SingleTileGetters::RegisterScalarFunctions(loader);
}

void MobilityduckExtension::Load(ExtensionLoader &loader) {
	DuckDB db_wrapper(loader.GetDatabaseInstance());
	ExtensionHelper::LoadExtension(db_wrapper, "spatial");
	LoadInternal(loader);
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

DUCKDB_CPP_EXTENSION_ENTRY(mobilityduck, loader) {
	duckdb::DuckDB db_wrapper(loader.GetDatabaseInstance());
	duckdb::ExtensionHelper::LoadExtension(db_wrapper, "spatial");
	duckdb::LoadInternal(loader);
}

DUCKDB_EXTENSION_API const char *mobilityduck_version() {
	return duckdb::DuckDB::LibraryVersion();
}

} // extern "C"

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif
