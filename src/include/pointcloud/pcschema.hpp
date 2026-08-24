#pragma once

#include <tydef.hpp>
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

//! A pcpoint and a pcpatch carry a pcid and nothing else about their own
//! layout, so which dimensions they hold and what a stored number means as a
//! coordinate are stated by the schema that pcid resolves to. A PostgreSQL
//! backend resolves it through the `pointcloud_schemas` and
//! `pointcloud_dimensions` tables of the MobilityDB extension; a database here
//! states a schema as rows of two tables of the same shape and hands them to
//! the engine through the function this registers, after which every question
//! about a coordinate is answerable.
struct PcschemaFunctions {
    static void RegisterScalarFunctions(ExtensionLoader &loader);
};

} // namespace duckdb
