// Precompiled header: the heavy, stable third-party headers shared by every
// MobilityDuck translation unit. Compiling these once per build instead of in
// each translation unit cuts incremental compile time. Only third-party
// headers belong here — adding a MobilityDuck header would rebuild the whole
// precompiled header whenever that header changes.
#pragma once

extern "C" {
#include <meos.h>
#include <meos_internal.h>
}

#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
