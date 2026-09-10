#include "temporal/temporal_parquet.hpp"
#include "duckdb/common/vector_operations/unary_executor.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include <cstdio>
#include <initializer_list>
#include <string>

namespace duckdb {

/* The footer follows TemporalParquet 2.0.0, the MobilityLakehouse
 * specification spec/temporalparquet.md */
static constexpr const char *TEMPORAL_PARQUET_VERSION = "2.0.0";
static constexpr const char *MEOS_WKB_ENCODING_VERSION = "1.0";

/* The class of a temporal base type, which fixes the covering columns it
 * carries, as spec/covering-columns.md lists them */
enum class TemporalCoveringClass { SPATIAL, NUMBER, TIME_ONLY, NONE };

static TemporalCoveringClass CoveringClassOf(const std::string &base_type) {
    for (auto type : {"tgeompoint", "tgeogpoint", "tgeometry", "tgeography", "tcbuffer",
                      "tnpoint", "tpose", "trgeometry"}) {
        if (base_type == type) return TemporalCoveringClass::SPATIAL;
    }
    for (auto type : {"tint", "tfloat", "tbigint"}) {
        if (base_type == type) return TemporalCoveringClass::NUMBER;
    }
    for (auto type : {"tbool", "ttext"}) {
        if (base_type == type) return TemporalCoveringClass::TIME_ONLY;
    }
    return TemporalCoveringClass::NONE;
}

/* GeoParquet's `edges` for the base types that fix it: a geodetic value moves
 * between two instants along the shortest path on the sphere */
static const char *EdgesOf(const std::string &base_type) {
    if (base_type == "tgeompoint" || base_type == "tgeometry") return "planar";
    if (base_type == "tgeogpoint" || base_type == "tgeography") return "spherical";
    return nullptr;
}

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

/* Append the covering @p key of column @p column: each bound maps to the field
 * of the struct column `<column>_<key>` that carries it */
static void AppendCovering(std::string &out, const std::string &column, const char *key,
                           std::initializer_list<const char *> bounds) {
    const std::string target = column + "_" + key;
    AppendJsonString(out, key);
    out += ":{";
    bool first = true;
    for (auto bound : bounds) {
        if (!first) out += ",";
        first = false;
        AppendJsonString(out, bound);
        out += ":[";
        AppendJsonString(out, target);
        out += ",";
        AppendJsonString(out, bound);
        out += "]";
    }
    out += "}";
}

/* Append the metadata of the temporal column @p column of type @p base_type.
 * The map names no SRID, CRS or Z, so the footer states none of them */
static void AppendColumn(std::string &out, const std::string &column,
                         const std::string &base_type) {
    AppendJsonString(out, column);
    out += ":{\"encoding\":\"MEOS-WKB\",\"encoding_version\":\"";
    out += MEOS_WKB_ENCODING_VERSION;
    out += "\",\"base_type\":";
    AppendJsonString(out, base_type);
    const char *edges = EdgesOf(base_type);
    if (edges) {
        out += ",\"edges\":\"";
        out += edges;
        out += "\",\"geodetic\":";
        out += std::string(edges) == "planar" ? "false" : "true";
    }
    auto covering = CoveringClassOf(base_type);
    if (covering != TemporalCoveringClass::NONE) {
        out += ",\"covering\":{";
        if (covering == TemporalCoveringClass::SPATIAL) {
            AppendCovering(out, column, "bbox", {"xmin", "ymin", "xmax", "ymax"});
            out += ",";
        } else if (covering == TemporalCoveringClass::NUMBER) {
            AppendCovering(out, column, "vspan", {"vmin", "vmax"});
            out += ",";
        }
        AppendCovering(out, column, "tspan", {"tmin", "tmax"});
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
