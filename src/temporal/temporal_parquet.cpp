#include "temporal/temporal_parquet.hpp"
#include "temporal/temporal_covering.hpp"
#include "duckdb/common/vector_operations/unary_executor.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include <cstdio>
#include <string>

namespace duckdb {

/* The footer follows TemporalParquet 2.0.0, the MobilityLakehouse
 * specification spec/temporalparquet.md */
static constexpr const char *TEMPORAL_PARQUET_VERSION = "2.0.0";
static constexpr const char *MEOS_WKB_ENCODING_VERSION = "1.0";

/* Append the JSON string literal holding @p s */
static void AppendJsonString(std::string &out, const std::string &s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    out += '"';
}

/* Append the covering @p covering of column @p column: each bound maps to the
 * field of the struct column that carries it */
static void AppendCovering(std::string &out, const std::string &column,
                           const TemporalCovering &covering) {
    const std::string target = column + covering.column_suffix;
    AppendJsonString(out, covering.key);
    out += ":{";
    for (const char *const *bound = covering.bounds; *bound; bound++) {
        if (bound != covering.bounds) out += ",";
        AppendJsonString(out, *bound);
        out += ":[";
        AppendJsonString(out, target);
        out += ",";
        AppendJsonString(out, *bound);
        out += "]";
    }
    out += "}";
}

/* Append the metadata of the temporal column @p column of type @p base_type.
 * The map names no SRID, CRS or Z, so the footer states none of them; the
 * edges and the coverings the base type fixes come from the MEOS-API catalog */
static void AppendColumn(std::string &out, const std::string &column,
                         const std::string &base_type) {
    AppendJsonString(out, column);
    out += ":{\"encoding\":\"MEOS-WKB\",\"encoding_version\":\"";
    out += MEOS_WKB_ENCODING_VERSION;
    out += "\",\"base_type\":";
    AppendJsonString(out, base_type);
    const TemporalCoveringType *type = TemporalCoveringOf(base_type);
    if (type && type->edges) {
        out += ",\"edges\":\"";
        out += type->edges;
        out += "\",\"geodetic\":";
        out += std::string(type->edges) == "planar" ? "false" : "true";
    }
    if (type && type->coverings[0].key) {
        out += ",\"covering\":{";
        for (const TemporalCovering *covering = type->coverings; covering->key; covering++) {
            if (covering != type->coverings) out += ",";
            AppendCovering(out, column, *covering);
        }
        out += "}";
    }
    out += "}";
}

static void TemporalFooterFun(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &map_vec = args.data[0];

    auto &keys_child = MapVector::GetKeys(map_vec);
    auto &vals_child = MapVector::GetValues(map_vec);
    auto child_count = ListVector::GetListSize(map_vec);

    keys_child.Flatten(child_count);
    vals_child.Flatten(child_count);
    auto *keys_data = FlatVector::GetData<string_t>(keys_child);
    auto *vals_data = FlatVector::GetData<string_t>(vals_child);
    auto &keys_validity = FlatVector::Validity(keys_child);
    auto &vals_validity = FlatVector::Validity(vals_child);

    UnifiedVectorFormat map_data;
    map_vec.ToUnifiedFormat(count, map_data);
    auto *list_entries = UnifiedVectorFormat::GetData<list_entry_t>(map_data);
    auto &map_validity = map_data.validity;

    auto *result_data = FlatVector::GetData<string_t>(result);
    auto &result_validity = FlatVector::Validity(result);

    for (idx_t i = 0; i < count; i++) {
        idx_t idx = map_data.sel->get_index(i);
        if (!map_validity.RowIsValid(idx)) {
            result_validity.SetInvalid(i);
            continue;
        }
        const auto &entry = list_entries[idx];
        std::string json = "{\"version\":\"";
        json += TEMPORAL_PARQUET_VERSION;
        json += "\",\"columns\":{";
        bool first = true;
        for (idx_t j = entry.offset; j < entry.offset + entry.length; j++) {
            if (!keys_validity.RowIsValid(j) || !vals_validity.RowIsValid(j)) continue;
            if (!first) json += ",";
            first = false;
            AppendColumn(json, keys_data[j].GetString(), vals_data[j].GetString());
        }
        json += "}}";
        result_data[i] = StringVector::AddString(result, json);
    }
}

void TemporalParquetFunctions::Register(ExtensionLoader &loader) {
    auto map_type = LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR);
    loader.RegisterFunction(
        ScalarFunction("temporalFooter", {map_type}, LogicalType::VARCHAR, TemporalFooterFun));
}

} // namespace duckdb
