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
#include "duckdb.hpp"
#include "geo/tgeometry.hpp"
#include "temporal/span.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension_util.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "index/rtree_module.hpp"

#include <mutex>
#include <fstream>
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
    #include <geos_c.h>
    #include <meos.h>
}

// OpenSSL linked through vcpkg
#include <openssl/opensslv.h>

namespace duckdb {
#include "spatial_ref_sys_csv.inc"

// =====================================================================
// 2. Utility: OpenSSL version scalar function
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
// 4. Extension load logic
// =====================================================================

static void LoadInternal(DatabaseInstance &instance) {
	// Configure MEOS SRID CSV once (env / embedded)
	ConfigureMeosSridCsvOnce();

	// Initialize MEOS once
	static std::once_flag meos_init_flag;
    std::call_once(meos_init_flag, []() {
        meos_initialize();
    });

	Connection con(instance);

	// Ensure DuckDB spatial extension is available
	con.Query("INSTALL spatial;");
	con.Query("LOAD spatial;");

	// Register scalar function: mobilityduck_openssl_version
	auto mobilityduck_openssl_version_scalar_function =
	    ScalarFunction(
	        "mobilityduck_openssl_version",
	        {LogicalType::VARCHAR},
	        LogicalType::VARCHAR,
	        MobilityduckOpenSSLVersionScalarFun
	    );
	ExtensionUtil::RegisterFunction(instance, mobilityduck_openssl_version_scalar_function);

	// Temporal and related types/functions
	TemporalTypes::RegisterTypes(instance);
	TemporalTypes::RegisterCastFunctions(instance);
	TemporalTypes::RegisterScalarFunctions(instance);
	TemporalTypes::RegisterTemporalUnnestFunction(instance);

	TboxType::RegisterType(instance);
	TboxType::RegisterCastFunctions(instance);
	TboxType::RegisterScalarFunctions(instance);

	StboxType::RegisterType(instance);
	StboxType::RegisterCastFunctions(instance);
	StboxType::RegisterScalarFunctions(instance);

	SpanTypes::RegisterScalarFunctions(instance);
	SpanTypes::RegisterTypes(instance);
	SpanTypes::RegisterCastFunctions(instance);

	TgeompointType::RegisterType(instance);
	TgeompointType::RegisterCastFunctions(instance);
	TgeompointType::RegisterScalarFunctions(instance);

	TGeometryTypes::RegisterScalarFunctions(instance);
	TGeometryTypes::RegisterTypes(instance);
	TGeometryTypes::RegisterCastFunctions(instance);
	TGeometryTypes::RegisterScalarInOutFunctions(instance);

	SetTypes::RegisterTypes(instance);
	SetTypes::RegisterCastFunctions(instance);
	SetTypes::RegisterScalarFunctions(instance);
	SetTypes::RegisterSetUnnest(instance);

	SpatialSetType::RegisterTypes(instance);
	SpatialSetType::RegisterCastFunctions(instance);
	SpatialSetType::RegisterScalarFunctions(instance);

	SpansetTypes::RegisterTypes(instance);
	SpansetTypes::RegisterCastFunctions(instance);
	SpansetTypes::RegisterScalarFunctions(instance);

	RTreeModule::RegisterRTreeIndex(instance);
	RTreeModule::RegisterIndexScan(instance);
	RTreeModule::RegisterScanOptimizer(instance);
}

void MobilityduckExtension::Load(DuckDB &db) {
	LoadInternal(*db.instance);
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

DUCKDB_EXTENSION_API void mobilityduck_init(duckdb::DatabaseInstance &db) {
	duckdb::DuckDB db_wrapper(db);
	db_wrapper.LoadExtension<duckdb::MobilityduckExtension>();
}

DUCKDB_EXTENSION_API const char *mobilityduck_version() {
	return duckdb::DuckDB::LibraryVersion();
}

} // extern "C"

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif
