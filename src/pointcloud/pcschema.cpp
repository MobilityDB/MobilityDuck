#include "pointcloud/pcschema.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <string>
#include <vector>
#include "mobilityduck/meos_exec_serial.hpp"

extern "C" {
    #include <meos.h>
    #include <meos_internal.h>
    #include <meos_pointcloud.h>
}

namespace duckdb {

//! Register the schema a pcid names from the rows that state it
//! @details Reads one schema — its SRID, its compression and the list of
//! dimensions — and hands it to the engine's schema cache, after which a value
//! of that pcid answers about its coordinates. Re-registering a pcid replaces
//! what it named, so a corrected row takes effect on the next call.
static void Pcschema_register(DataChunk &args, ExpressionState &state, Vector &result) {
    D_ASSERT(args.ColumnCount() == 4);
    const idx_t count = args.size();

    UnifiedVectorFormat pcid_data, srid_data, compression_data, dims_data;
    args.data[0].ToUnifiedFormat(count, pcid_data);
    args.data[1].ToUnifiedFormat(count, srid_data);
    args.data[2].ToUnifiedFormat(count, compression_data);
    args.data[3].ToUnifiedFormat(count, dims_data);

    // One flat child vector holds every dimension of every row, so each field
    // is read once for the whole chunk and a row's dimensions are the slice
    // its list entry names.
    auto &dim_vector = ListVector::GetEntry(args.data[3]);
    const idx_t dim_count = ListVector::GetListSize(args.data[3]);
    auto &dim_fields = StructVector::GetEntries(dim_vector);
    D_ASSERT(dim_fields.size() == 7);

    UnifiedVectorFormat position_data, name_data, interpretation_data, scale_data,
        offset_data, active_data, description_data;
    dim_fields[0]->ToUnifiedFormat(dim_count, position_data);
    dim_fields[1]->ToUnifiedFormat(dim_count, name_data);
    dim_fields[2]->ToUnifiedFormat(dim_count, interpretation_data);
    dim_fields[3]->ToUnifiedFormat(dim_count, scale_data);
    dim_fields[4]->ToUnifiedFormat(dim_count, offset_data);
    dim_fields[5]->ToUnifiedFormat(dim_count, active_data);
    dim_fields[6]->ToUnifiedFormat(dim_count, description_data);

    auto result_data = FlatVector::GetData<bool>(result);
    auto &result_validity = FlatVector::Validity(result);
    result.SetVectorType(VectorType::FLAT_VECTOR);

    for (idx_t row = 0; row < count; row++) {
        const auto pcid_idx = pcid_data.sel->get_index(row);
        const auto srid_idx = srid_data.sel->get_index(row);
        const auto compression_idx = compression_data.sel->get_index(row);
        const auto dims_idx = dims_data.sel->get_index(row);
        if (!pcid_data.validity.RowIsValid(pcid_idx) ||
            !srid_data.validity.RowIsValid(srid_idx) ||
            !compression_data.validity.RowIsValid(compression_idx) ||
            !dims_data.validity.RowIsValid(dims_idx)) {
            result_validity.SetInvalid(row);
            continue;
        }

        const auto pcid = UnifiedVectorFormat::GetData<int32_t>(pcid_data)[pcid_idx];
        const auto srid = UnifiedVectorFormat::GetData<int32_t>(srid_data)[srid_idx];
        const auto compression =
            UnifiedVectorFormat::GetData<string_t>(compression_data)[compression_idx].GetString();
        const auto entry = UnifiedVectorFormat::GetData<list_entry_t>(dims_data)[dims_idx];

        // The strings a spec points at outlive the call the engine parses them
        // in, so they are held here rather than in the temporaries of the loop.
        std::vector<std::string> names(entry.length);
        std::vector<std::string> interpretations(entry.length);
        std::vector<std::string> descriptions(entry.length);
        std::vector<PCDimensionSpec> dims(entry.length);
        bool row_valid = true;

        for (idx_t i = 0; i < entry.length && row_valid; i++) {
            const auto child = entry.offset + i;
            const auto position_idx = position_data.sel->get_index(child);
            const auto name_idx = name_data.sel->get_index(child);
            const auto interpretation_idx = interpretation_data.sel->get_index(child);
            const auto scale_idx = scale_data.sel->get_index(child);
            const auto offset_idx = offset_data.sel->get_index(child);
            const auto active_idx = active_data.sel->get_index(child);
            const auto description_idx = description_data.sel->get_index(child);

            // Every field but the description states something the layout
            // depends on, so a missing one leaves the schema unstated.
            if (!position_data.validity.RowIsValid(position_idx) ||
                !name_data.validity.RowIsValid(name_idx) ||
                !interpretation_data.validity.RowIsValid(interpretation_idx) ||
                !scale_data.validity.RowIsValid(scale_idx) ||
                !offset_data.validity.RowIsValid(offset_idx) ||
                !active_data.validity.RowIsValid(active_idx)) {
                row_valid = false;
                break;
            }

            names[i] = UnifiedVectorFormat::GetData<string_t>(name_data)[name_idx].GetString();
            interpretations[i] =
                UnifiedVectorFormat::GetData<string_t>(interpretation_data)[interpretation_idx].GetString();
            if (description_data.validity.RowIsValid(description_idx)) {
                descriptions[i] =
                    UnifiedVectorFormat::GetData<string_t>(description_data)[description_idx].GetString();
            }

            dims[i].name = names[i].c_str();
            dims[i].description = descriptions[i].empty() ? nullptr : descriptions[i].c_str();
            dims[i].position = UnifiedVectorFormat::GetData<int32_t>(position_data)[position_idx];
            dims[i].interpretation = interpretations[i].c_str();
            dims[i].scale = UnifiedVectorFormat::GetData<double>(scale_data)[scale_idx];
            dims[i].offset = UnifiedVectorFormat::GetData<double>(offset_data)[offset_idx];
            dims[i].active = UnifiedVectorFormat::GetData<bool>(active_data)[active_idx];
        }

        if (!row_valid) {
            result_validity.SetInvalid(row);
            continue;
        }

        result_data[row] = meos_pc_schema_register_dims((uint32_t) pcid, srid,
            compression.c_str(), dims.data(), (int) dims.size());
    }

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void PcschemaFunctions::RegisterScalarFunctions(ExtensionLoader &loader) {
    const auto I = LogicalType::INTEGER;
    const auto V = LogicalType::VARCHAR;
    const auto D = LogicalType::DOUBLE;
    const auto BL = LogicalType::BOOLEAN;

    // One struct field per column of the `pointcloud_dimensions` table. The
    // size of a dimension and its offset within a point are absent because the
    // engine computes them from the interpretation and from the dimensions
    // before each one, and the X, Y, Z and M dimensions are absent because they
    // resolve from the names — a row stating any of them could only contradict
    // the engine.
    const auto DIM = LogicalType::STRUCT({
        {"position", I},
        {"name", V},
        {"interpretation", V},
        {"scale", D},
        {"offset", D},
        {"active", BL},
        {"description", V},
    });

    auto pcschema_register = ScalarFunction(
        "pointCloudSchemaRegister",
        {I, I, V, LogicalType::LIST(DIM)},
        BL,
        Pcschema_register
    );
    // A schema is state the engine holds for the rest of the session, so two
    // calls with the same rows are not one call: the function states an effect
    // rather than a value.
    pcschema_register.stability = FunctionStability::VOLATILE;
    duckdb::RegisterSerializedScalarFunction(loader, pcschema_register);
}

} // namespace duckdb
