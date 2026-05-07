#include "temporal/temporal_parquet.hpp"
#include "duckdb/common/vector_operations/unary_executor.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

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
        std::string json = "{\"version\":\"1.0.0\",\"columns\":{";
        bool first = true;
        for (idx_t j = entry.offset; j < entry.offset + entry.length; j++) {
            if (!keys_validity.RowIsValid(j) || !vals_validity.RowIsValid(j)) continue;
            if (!first) json += ",";
            first = false;
            std::string col_name = keys_data[j].GetString();
            std::string base_type = vals_data[j].GetString();
            json += "\"" + col_name + "\":{\"encoding\":\"MEOS-WKB\","
                    "\"encoding_version\":\"1.0\","
                    "\"base_type\":\"" + base_type + "\"}";
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
