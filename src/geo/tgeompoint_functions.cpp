#include "meos_wrapper_simple.hpp"
#include "common.hpp"

#include "geo/tgeompoint_functions.hpp"
#include "temporal/temporal_functions.hpp"
#include "time_util.hpp"
#include "geo_util.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/vector.hpp"
#include "duckdb/common/vector_operations/ternary_executor.hpp"
#include "duckdb/common/typedefs.hpp"

#include "spatial/spatial_types.hpp"

#include "duckdb/common/types.hpp"

#include <cfloat>
#include <cmath>

#include "meos_internal_geo.h"

namespace duckdb {

namespace {

enum class SpatialarrElemKind { TGEOM_POINT, GEOMETRY_BLOB };

static SpatialarrElemKind spatialarr_elem_kind_from_list(const LogicalType &list_type) {
	if (list_type.id() != LogicalTypeId::LIST) {
		throw InvalidInputException("Spatialarr_as_text/asEWKT: expected a LIST input");
	}
	const auto &child = ListType::GetChildType(list_type);
	const auto &alias = child.GetAlias();
	if (alias == "tgeompoint" || alias == "tgeogpoint") {
		return SpatialarrElemKind::TGEOM_POINT;
	}
	if (alias == "GEOMETRY") {
		return SpatialarrElemKind::GEOMETRY_BLOB;
	}
	throw InvalidInputException(
	    "Spatialarr_as_text/asEWKT: expected LIST(tgeompoint), LIST(tgeogpoint), or LIST(geometry), got LIST(" +
	    alias + ")");
}

static void spatialarr_wkt_array(DataChunk &args, ExpressionState &state, Vector &result, bool extended,
                                 int default_maxdd) {
	auto &list_vec = args.data[0];
	const idx_t row_count = args.size();
	list_vec.Flatten(row_count);

	const SpatialarrElemKind kind = spatialarr_elem_kind_from_list(list_vec.GetType());

	const bool has_maxdd = args.ColumnCount() > 1;
	if (has_maxdd) {
		args.data[1].Flatten(row_count);
	}

	auto *in_list_entries = ListVector::GetData(list_vec);
	auto &in_child = ListVector::GetEntry(list_vec);
	const idx_t list_child_size = ListVector::GetListSize(list_vec);
	in_child.Flatten(list_child_size);
	auto in_child_data = FlatVector::GetData<string_t>(in_child);
	auto &in_child_validity = FlatVector::Validity(in_child);
	// a geometry of the list carries the SRID its type states (GeometryCrsSrid)
	const int32_t crs = GeometryCrsSrid(list_vec.GetType());

	auto &list_validity = FlatVector::Validity(list_vec);

	auto *out_list_entries = ListVector::GetData(result);
	auto &out_child = ListVector::GetEntry(result);
	out_child.SetVectorType(VectorType::FLAT_VECTOR);
	auto out_child_data = FlatVector::GetData<string_t>(out_child);
	auto &out_validity = FlatVector::Validity(result);

	idx_t total_offset = 0;

	for (idx_t row = 0; row < row_count; row++) {
		if (!list_validity.RowIsValid(row)) {
			out_validity.SetInvalid(row);
			continue;
		}

		int maxdd = default_maxdd;
		if (has_maxdd) {
			auto &dd_vec = args.data[1];
			if (!FlatVector::Validity(dd_vec).RowIsValid(row)) {
				out_validity.SetInvalid(row);
				continue;
			}
			maxdd = FlatVector::GetData<int32_t>(dd_vec)[row];
		}

		const list_entry_t le = in_list_entries[row];
		if (le.length == 0) {
			out_validity.SetInvalid(row);
			continue;
		}

		ListVector::Reserve(result, total_offset + le.length);
		ListVector::SetListSize(result, total_offset + le.length);
		out_list_entries[row] = list_entry_t{total_offset, le.length};

		for (idx_t j = 0; j < le.length; j++) {
			const idx_t child_idx = le.offset + j;
			if (!in_child_validity.RowIsValid(child_idx)) {
				throw InvalidInputException("Spatialarr_as_text/asEWKT: NULL element inside spatial array");
			}
			const string_t &blob = in_child_data[child_idx];
			char *wkt = nullptr;
			if (kind == SpatialarrElemKind::TGEOM_POINT) {
				size_t data_size = blob.GetSize();
				if (data_size < sizeof(void *)) {
					throw InvalidInputException("Spatialarr_as_text/asEWKT: invalid temporal blob in array");
				}
				uint8_t *data_copy = (uint8_t *)malloc(data_size);
				memcpy(data_copy, blob.GetData(), data_size);
				Temporal *temp = reinterpret_cast<Temporal *>(data_copy);
				wkt = extended ? tspatial_as_ewkt(temp, maxdd) : tspatial_as_text(temp, maxdd);
				free(data_copy);
			} else {
				GSERIALIZED *gs = GeometryToGSerialized(blob, crs);
				wkt = extended ? geo_as_ewkt(gs, maxdd) : geo_as_text(gs, maxdd);
				free(gs);
			}
			if (!wkt) {
				throw InvalidInputException("Spatialarr_as_text/asEWKT: failed to convert array element");
			}
			out_child_data[total_offset + j] = StringVector::AddString(out_child, wkt);
			free(wkt);
		}
		total_offset += le.length;
	}

	ListVector::SetListSize(result, total_offset);

	if (row_count == 1) {
		result.SetVectorType(VectorType::CONSTANT_VECTOR);
	}
}

} // namespace

/* ***************************************************
 * In/out functions
 ****************************************************/

bool TgeompointFunctions::Tpoint_in(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_geom_str) -> string_t {
            std::string input_str = input_geom_str.GetString();

            Temporal *temp = tgeompoint_in(input_str.c_str());
            if (!temp) {
                throw InvalidInputException("Invalid tgeompoint input: " + input_str);
            }

            size_t data_size = temporal_mem_size(temp);
            uint8_t *data_buffer = (uint8_t*)malloc(data_size);
            if (!data_buffer) {
                free(temp);
                throw InvalidInputException("Failed to allocate memory for tgeompoint");
            }

            memcpy(data_buffer, temp, data_size);

            string_t output(reinterpret_cast<const char *>(data_buffer), data_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, output);

            free(data_buffer);
            free(temp);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
    return true;
}

void TgeompointFunctions::Spatialarr_as_text(DataChunk &args, ExpressionState &state, Vector &result) {
	spatialarr_wkt_array(args, state, result, false, 15);
}

void TgeompointFunctions::Spatialarr_as_ewkt(DataChunk &args, ExpressionState &state, Vector &result) {
	spatialarr_wkt_array(args, state, result, true, OUT_DEFAULT_DECIMAL_DIGITS);
}

inline void Tspatial_to_stbox_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid tgeompoint data: null pointer");
                return string_t();
            }
            STBox *stbox = tspatial_to_stbox(temp);
            if (!stbox) {
                free(temp);
                throw InvalidInputException("Failed to convert tgeompoint to stbox");
                return string_t();
            }

            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
                free(temp);
                throw InvalidInputException("Failed to allocate memory for stbox");
                return string_t();
            }
            memcpy(stbox_data, stbox, stbox_size);
            string_t stbox_string(reinterpret_cast<const char*>(stbox_data), stbox_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, stbox_string);

            free(stbox_data);
            free(stbox);
            free(temp);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

bool TgeompointFunctions::Tspatial_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Tspatial_to_stbox_common(source, result, count);
    return true;
}

/* ***************************************************
 * Conversion functions
 ****************************************************/

inline void Temporal_to_tstzspan_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_to_tstzspan: unable to cast string to temporal");
            }

            Span *ret = (Span*)malloc(sizeof(Span));
            temporal_set_tstzspan(temp, ret);
            size_t span_size = sizeof(*ret);
            uint8_t *span_buffer = (uint8_t*) malloc(span_size);
            memcpy(span_buffer, ret, span_size);
            string_t span_string_t((char *) span_buffer, span_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
            free(span_buffer);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

bool TgeompointFunctions::Temporal_to_tstzspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Temporal_to_tstzspan_common(source, result, count);
    return true;
}

/* ***************************************************
 * Accessor functions
 ****************************************************/

/* ***************************************************
 * Stops function
 ****************************************************/

// void TgeompointFunctions::gs_as_text(DataChunk &args, ExpressionState &state, Vector &result) {
//     UnaryExecutor::Execute<string_t, string_t>(
//         args.data[0], result, args.size(),
//         [&](string_t blob) -> string_t {
//             const uint8_t *data = reinterpret_cast<const uint8_t*>(blob.GetData());
//             size_t data_size = blob.GetSize();
//             uint8_t *data_copy = (uint8_t*)malloc(data_size);
//             memcpy(data_copy, data, data_size);
//             GSERIALIZED *gs = reinterpret_cast<GSERIALIZED*>(data_copy);
//             if (!gs) {
//                 free(data_copy);
//                 throw InvalidInputException("Invalid GSERIALIZED data: null pointer");
//             }
//             char *gs_text = geo_as_ewkt(gs, 10);
//             if (!gs_text) {
//                 free(data_copy);
//                 throw InvalidInputException("Failed to convert GSERIALIZED to text");
//             }
//             string_t gs_text_string(gs_text);
//             string_t stored_result = StringVector::AddStringOrBlob(result, gs_text_string);
//             free(gs);
//             return stored_result;
//         }
//     );
//     if (args.size() == 1) {
//         result.SetVectorType(VectorType::CONSTANT_VECTOR);
//     }
// }

} // namespace duckdb
