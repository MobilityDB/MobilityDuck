/*****************************************************************************
 *
 * This MobilityDB code is provided under The PostgreSQL License.
 * Copyright (c) 2016-2026, Université libre de Bruxelles and MobilityDB
 * contributors
 *
 * MobilityDB includes portions of PostGIS version 3 source code released
 * under the GNU General Public License (GPLv2 or later).
 * Copyright (c) 2001-2026, PostGIS contributors
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without a written
 * agreement is hereby granted, provided that the above copyright notice and
 * this paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL UNIVERSITE LIBRE DE BRUXELLES BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
 * LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION,
 * EVEN IF UNIVERSITE LIBRE DE BRUXELLES HAS BEEN ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * UNIVERSITE LIBRE DE BRUXELLES SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON
 * AN "AS IS" BASIS, AND UNIVERSITE LIBRE DE BRUXELLES HAS NO OBLIGATIONS TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 *****************************************************************************/

#include "pointcloud/pcschema.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include "mobilityduck/meos_registration.hpp"

extern "C" {
    #include <meos.h>
    #include <meos_internal.h>
    #include <meos_pointcloud.h>
}

namespace duckdb {

//! A schema as pcschema_register states it, held by the extension for every thread
//! @details MEOS keeps its point cloud schemas per thread, as its time zone, and DuckDB
//! runs a query on worker threads other than the one that registered the schema. The
//! extension keeps every schema registered through SQL, in order, and each thread hands
//! the ones it has not seen to its own MEOS cache on its first MEOS call after them
//! (SyncPcSchemas, from EnsureMeosThreadInitialized).
struct PcSchemaEntry {
    uint32_t pcid;
    int32_t srid;
    std::string compression;
    std::vector<std::string> names, descriptions, interpretations;
    std::vector<PCDimensionSpec> dims;
};

static std::mutex &PcSchemaMutex() {
    static std::mutex m;
    return m;
}

static std::vector<PcSchemaEntry> &PcSchemaEntries() {
    static std::vector<PcSchemaEntry> entries;
    return entries;
}

static std::atomic<size_t> &PcSchemaCount() {
    static std::atomic<size_t> count {0};
    return count;
}

void SyncPcSchemas() {
    static thread_local size_t seen = 0;
    if (seen == PcSchemaCount().load(std::memory_order_acquire)) {
        return;
    }
    std::lock_guard<std::mutex> guard(PcSchemaMutex());
    auto &entries = PcSchemaEntries();
    for (; seen < entries.size(); seen++) {
        auto &e = entries[seen];
        meos_pc_schema_register_dims(e.pcid, e.srid, e.compression.c_str(), e.dims.data(),
            (int) e.dims.size());
    }
}

//! Keep a schema for every thread and register it in this one, answering what MEOS answers
static bool RegisterPcSchemaForEveryThread(uint32_t pcid, int32_t srid, const std::string &compression,
                                           const std::vector<PCDimensionSpec> &dims) {
    PcSchemaEntry e;
    e.pcid = pcid;
    e.srid = srid;
    e.compression = compression;
    for (auto &d : dims) {
        e.names.emplace_back(d.name);
        e.descriptions.emplace_back(d.description ? d.description : "");
        e.interpretations.emplace_back(d.interpretation);
    }
    for (size_t i = 0; i < dims.size(); i++) {
        PCDimensionSpec s = dims[i];
        s.name = e.names[i].c_str();
        s.description = dims[i].description ? e.descriptions[i].c_str() : nullptr;
        s.interpretation = e.interpretations[i].c_str();
        e.dims.push_back(s);
    }
    bool ok = meos_pc_schema_register_dims(pcid, srid, compression.c_str(), dims.data(),
        (int) dims.size());
    if (!ok) {
        return false;
    }
    std::lock_guard<std::mutex> guard(PcSchemaMutex());
    PcSchemaEntries().push_back(std::move(e));
    PcSchemaCount().store(PcSchemaEntries().size(), std::memory_order_release);
    return true;
}

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

        result_data[row] = RegisterPcSchemaForEveryThread((uint32_t) pcid, srid, compression, dims);
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
        {"dim_no", I},
        {"dim_name", V},
        {"interpretation", V},
        {"dim_scale", D},
        {"dim_offset", D},
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
    duckdb::RegisterMeosFunction(loader, pcschema_register);
}

} // namespace duckdb
