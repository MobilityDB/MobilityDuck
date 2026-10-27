#include "pointcloud/tpcpatch.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include <regex>
#include <string>
#include <temporal/span.hpp>
#include "temporal/temporal_functions.hpp"
#include "mobilityduck/meos_registration.hpp"

extern "C" {
    #include <meos.h>
    #include <meos_geo.h>
    #include <meos_internal.h>
    #include <meos_internal_geo.h>
}

namespace duckdb {

// The text form of a tpcpatch is read by tpcpatch_in and written by temporal_out through the
// VARCHAR <-> tpcpatch cast below; its I/O functions are generated from the catalog.

bool TpcpatchFunctions::StringToTpcpatch(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_string) -> string_t {
            std::string input_str = input_string.GetString();

            Temporal *temp = tpcpatch_in(input_str.c_str());
            if (!temp) {
                throw InvalidInputException("Invalid tpcpatch input: " + input_str);
            }

            size_t data_size = temporal_mem_size(temp);
            uint8_t *data_buffer = (uint8_t*)malloc(data_size);
            if (!data_buffer) {
                free(temp);
                throw InvalidInputException("Failed to allocate memory for tpcpatch data");
            }

            memcpy(data_buffer, temp, data_size);

            string_t data_string_t(reinterpret_cast<const char*>(data_buffer), data_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, data_string_t);

            free(data_buffer);
            free(temp);

            return stored_data;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
    return true;
}

bool TpcpatchFunctions::TpcpatchToString(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();

            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid tpcpatch data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for tpcpatch deserialization");
            }
            memcpy(data_copy, data, data_size);

            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid tpcpatch data: null pointer");
            }

            char *str = temporal_out(temp, 15);
            if (!str) {
                free(data_copy);
                throw InvalidInputException("Failed to convert tpcpatch to string");
            }

            std::string output(str);
            string_t stored_result = StringVector::AddString(result, output);

            free(str);
            free(data_copy);

            return stored_result;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
    return true;
}

void TPcpatchTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader, LogicalType::VARCHAR, TPcpatchTypes::tpcpatch(), TpcpatchFunctions::StringToTpcpatch);
    RegisterMeosCastFunction(loader, TPcpatchTypes::tpcpatch(), LogicalType::VARCHAR, TpcpatchFunctions::TpcpatchToString);
    // Base pcpatch value render cast: generated startValue/endValue return the pcpatch
    // base value, rendered as hex-WKB text via pcpatch_hex_out (cbuffer sibling pattern).
    RegisterMeosCastFunction(loader, TPcpatchTypes::pcpatch(), LogicalType::VARCHAR, TpcpatchFunctions::Pcpatch_out_cast);
    RegisterMeosCastFunction(loader, LogicalType::VARCHAR, TPcpatchTypes::pcpatch(), TpcpatchFunctions::Pcpatch_in_cast);
}

}
