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
				GSERIALIZED *gs = GeometryToGSerialized(blob, 0);
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

void TgeompointFunctions::Tspatial_as_text(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid tgeompoint data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid tgeompoint input: " + input_blob.GetString());
            }

            char* ret = tspatial_as_text(temp, 15);
            if (!ret) {
                free(data_copy);
                throw InvalidInputException("Failed to convert tgeompoint to text: " + input_blob.GetString());
            }
            std::string ret_string(ret);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_string);

            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tspatial_as_ewkt(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid tgeompoint data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid tgeompoint input: " + input_blob.GetString());
            }

            char *ewkt = tspatial_as_ewkt(temp, OUT_DEFAULT_DECIMAL_DIGITS);
            if (!ewkt) {
                free(data_copy);
                throw InvalidInputException("Failed to convert tgeompoint to EWKT: " + input_blob.GetString());
            }
            std::string ret_string(ewkt);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_string);

            free(ewkt);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
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

void TgeompointFunctions::Tspatial_to_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    Tspatial_to_stbox_common(args.data[0], result, args.size());
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

void TgeompointFunctions::Tpoint_make_simple(DataChunk &args, ExpressionState &state, Vector &result) {
	idx_t total_count = 0;
	UnaryExecutor::Execute<string_t, list_entry_t>(
	    args.data[0], result, args.size(),
	    [&](string_t input_blob) -> list_entry_t {
		    const uint8_t *data = reinterpret_cast<const uint8_t *>(input_blob.GetData());
		    size_t data_size = input_blob.GetSize();
		    if (data_size < sizeof(void *)) {
			    throw InvalidInputException("Invalid tgeompoint data: insufficient size");
		    }
		    uint8_t *data_copy = (uint8_t *)malloc(data_size);
		    memcpy(data_copy, data, data_size);
		    Temporal *temp = reinterpret_cast<Temporal *>(data_copy);
		    if (!temp) {
			    free(data_copy);
			    throw InvalidInputException("Invalid tgeompoint data: null pointer");
		    }

		    int frag_count = 0;
		    Temporal **fragments = tpoint_make_simple(temp, &frag_count);
		    free(data_copy);

		    if (!fragments || frag_count <= 0) {
			    if (fragments) {
				    free(fragments);
			    }
			    throw InvalidInputException("makeSimple: failed to split temporal point");
		    }

		    const list_entry_t entry(total_count, static_cast<uint64_t>(frag_count));
		    total_count += frag_count;
		    ListVector::Reserve(result, total_count);

		    auto &child_vec = ListVector::GetEntry(result);
		    auto child_data = FlatVector::GetData<string_t>(child_vec);

		    for (int i = 0; i < frag_count; i++) {
			    Temporal *frag = fragments[i];
			    if (!frag) {
				    for (int k = i; k < frag_count; k++) {
					    if (fragments[k]) {
						    free(fragments[k]);
					    }
				    }
				    free(fragments);
				    throw InvalidInputException("makeSimple: null fragment in result array");
			    }
			    size_t frag_size = temporal_mem_size(frag);
			    uint8_t *frag_buf = (uint8_t *)malloc(frag_size);
			    memcpy(frag_buf, frag, frag_size);
			    string_t frag_blob(reinterpret_cast<const char *>(frag_buf), frag_size);
			    string_t stored = StringVector::AddStringOrBlob(child_vec, frag_blob);
			    free(frag_buf);
			    free(frag);
			    child_data[entry.offset + static_cast<idx_t>(i)] = stored;
		    }
		    free(fragments);
		    return entry;
	    });
	ListVector::SetListSize(result, total_count);
	if (args.size() == 1) {
		result.SetVectorType(VectorType::CONSTANT_VECTOR);
	}
}

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

void TgeompointFunctions::collect_gs(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &array_vec = args.data[0];
    array_vec.Flatten(args.size());
    auto *list_entries = ListVector::GetData(array_vec);
    auto &child_vec = ListVector::GetEntry(array_vec);
    child_vec.Flatten(ListVector::GetListSize(array_vec));
    auto child_data = FlatVector::GetData<string_t>(child_vec);

    UnaryExecutor::Execute<list_entry_t, string_t>(
        array_vec, result, args.size(),
        [&](const list_entry_t &list) {
            auto offset = list.offset;
            auto length = list.length;
            GSERIALIZED **gsarr = (GSERIALIZED **)malloc(length * sizeof(GSERIALIZED *));
            if (!gsarr) {
                throw InternalException("Failed to allocate memory for GSERIALIZED array");
            }
            for (idx_t i = 0; i < length; i++) {
                idx_t child_idx = offset + i;
                auto wkb_data = child_data[child_idx];
                size_t data_size = wkb_data.GetSize();
                if (data_size < sizeof(void*)) {
                    for (idx_t j = 0; j < i; j++) {
                        free(gsarr[j]);
                    }
                    free(gsarr);
                    throw InvalidInputException("Invalid BLOB data: insufficient size");
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, wkb_data.GetData(), data_size);
                GSERIALIZED *gs = reinterpret_cast<GSERIALIZED*>(data_copy);
                if (!gs) {
                    free(data_copy);
                    for (idx_t j = 0; j < i; j++) {
                        free(gsarr[j]);
                    }
                    free(gsarr);
                    throw InvalidInputException("Invalid GSERIALIZED data: null pointer");
                }
                gsarr[i] = gs;
                // char *gs_text = geo_as_ewkt(gs, 10);
                // printf("GS %ld: %s\n", i, gs_text);
            }

            GSERIALIZED *gs_out = geo_collect_garray(gsarr, (int)length);
            if (!gs_out) {
                for (idx_t j = 0; j < length; j++) {
                    free(gsarr[j]);
                }
                free(gsarr);
                throw InvalidInputException("Failed to collect GSERIALIZED array");
            }

            size_t gs_out_size = VARSIZE(gs_out);
            uint8_t *gs_out_data = (uint8_t*)malloc(gs_out_size);
            memcpy(gs_out_data, gs_out, gs_out_size);
            string_t gs_out_string(reinterpret_cast<const char*>(gs_out_data), gs_out_size);
            string_t stored_result = StringVector::AddStringOrBlob(result, gs_out_string);
            free(gs_out_data);
            free(gs_out);
            for (idx_t i = 0; i < length; i++) {
                free(gsarr[i]);
            }
            free(gsarr);
            return stored_result;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::distance_geo_geo(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, double>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t blob1, string_t blob2) -> double {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(blob1.GetData());
            size_t data1_size = blob1.GetSize();
            uint8_t *data1_copy = (uint8_t*)malloc(data1_size);
            memcpy(data1_copy, data1, data1_size);
            GSERIALIZED *gs1 = reinterpret_cast<GSERIALIZED*>(data1_copy);
            if (!gs1) {
                free(data1_copy);
                throw InvalidInputException("Invalid GSERIALIZED data 1: null pointer");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(blob2.GetData());
            size_t data2_size = blob2.GetSize();
            uint8_t *data2_copy = (uint8_t*)malloc(data2_size);
            memcpy(data2_copy, data2, data2_size);
            GSERIALIZED *gs2 = reinterpret_cast<GSERIALIZED*>(data2_copy);
            if (!gs2) {
                free(gs1);
                free(data2_copy);
                throw InvalidInputException("Invalid GSERIALIZED data 2: null pointer");
            }

            double distance = geom_distance2d(gs1, gs2);
            free(gs1);
            free(gs2);
            return distance;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
 * Affine-derived spatial-transform stragglers
 * (rotate-around-point-geometry, scale-by-doubles)
 ****************************************************/

namespace {

string_t ApplyAffineToTgeoStraggler(Vector &result, string_t input_blob, const AFFINE &m) {
    size_t data_size = input_blob.GetSize();
    if (data_size < sizeof(void *)) {
        throw InvalidInputException("Invalid tgeompoint data: insufficient size");
    }
    uint8_t *data_copy = (uint8_t *)malloc(data_size);
    memcpy(data_copy, input_blob.GetData(), data_size);
    Temporal *temp = reinterpret_cast<Temporal *>(data_copy);
    Temporal *ret = tgeo_affine(temp, m.afac, m.bfac, m.cfac, m.dfac, m.efac, m.ffac, m.gfac, m.hfac, m.ifac, m.xoff, m.yoff, m.zoff);
    free(temp);
    if (!ret) {
        throw InternalException("tgeo_affine returned null");
    }
    size_t ret_size = temporal_mem_size(ret);
    uint8_t *ret_data = (uint8_t *)malloc(ret_size);
    memcpy(ret_data, ret, ret_size);
    string_t ret_string(reinterpret_cast<const char *>(ret_data), ret_size);
    string_t stored = StringVector::AddStringOrBlob(result, ret_string);
    free(ret_data);
    free(ret);
    return stored;
}

inline AFFINE IdentityAffineStraggler() {
    AFFINE m{};
    m.afac = 1.0;
    m.efac = 1.0;
    m.ifac = 1.0;
    return m;
}

} // namespace

void TgeompointFunctions::Tgeo_rotate_geom(DataChunk &args, ExpressionState &state, Vector &result) {
    const idx_t row_count = args.size();
    args.data[0].Flatten(row_count);
    args.data[1].Flatten(row_count);
    args.data[2].Flatten(row_count);
    auto in_data = FlatVector::GetData<string_t>(args.data[0]);
    auto angle_data = FlatVector::GetData<double>(args.data[1]);
    auto geom_data = FlatVector::GetData<string_t>(args.data[2]);
    auto &v0 = FlatVector::Validity(args.data[0]);
    auto &v1 = FlatVector::Validity(args.data[1]);
    auto &v2 = FlatVector::Validity(args.data[2]);
    auto out_data = FlatVector::GetData<string_t>(result);
    auto &out_validity = FlatVector::Validity(result);

    for (idx_t row = 0; row < row_count; row++) {
        if (!v0.RowIsValid(row) || !v1.RowIsValid(row) || !v2.RowIsValid(row)) {
            out_validity.SetInvalid(row);
            continue;
        }
        const double theta = angle_data[row];
        GSERIALIZED *gs = GeometryToGSerialized(geom_data[row], 0);
        if (!gs) {
            throw InvalidInputException("Invalid centre-point geometry");
        }
        const POINT2D *pt = GSERIALIZED_POINT2D_P(gs);
        const double cx = pt->x;
        const double cy = pt->y;
        free(gs);

        const double c = std::cos(theta);
        const double s = std::sin(theta);
        AFFINE m = IdentityAffineStraggler();
        m.afac =  c; m.bfac = -s;
        m.dfac =  s; m.efac =  c;
        m.xoff = cx - c * cx + s * cy;
        m.yoff = cy - s * cx - c * cy;

        out_data[row] = ApplyAffineToTgeoStraggler(result, in_data[row], m);
    }
    if (row_count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
 * nearestApproachInstant / nearestApproachDistance / nad
 ****************************************************/

namespace {

} // anonymous namespace

/* ***************************************************
 * Affine/translate/rotate/rotateX/Y/Z/transscale/scale transforms
 * All build an AFFINE struct and delegate to tgeo_affine (or tgeo_scale_geo).
 ****************************************************/

namespace {

string_t ApplyAffineToTgeo(Vector &result, string_t blob, const AFFINE &m) {
    size_t sz = blob.GetSize();
    uint8_t *copy = (uint8_t *)malloc(sz);
    memcpy(copy, blob.GetData(), sz);
    Temporal *temp = reinterpret_cast<Temporal *>(copy);
    Temporal *ret = tgeo_affine(temp, m.afac, m.bfac, m.cfac, m.dfac, m.efac, m.ffac, m.gfac, m.hfac, m.ifac, m.xoff, m.yoff, m.zoff);
    free(temp);
    if (!ret) throw InternalException("tgeo_affine returned null");
    size_t rsz = temporal_mem_size(ret);
    uint8_t *rbuf = (uint8_t *)malloc(rsz);
    memcpy(rbuf, ret, rsz);
    string_t rs(reinterpret_cast<const char *>(rbuf), rsz);
    string_t stored = StringVector::AddStringOrBlob(result, rs);
    free(rbuf); free(ret);
    return stored;
}

inline AFFINE IdentityAffine() {
    AFFINE m{};
    m.afac = 1.0; m.efac = 1.0; m.ifac = 1.0;
    return m;
}

} // anonymous namespace

void TgeompointFunctions::Tgeo_rotate_angle(DataChunk &args, ExpressionState &state, Vector &result) {
    const idx_t n = args.size();
    args.data[0].Flatten(n); args.data[1].Flatten(n);
    auto in    = FlatVector::GetData<string_t>(args.data[0]);
    auto angle = FlatVector::GetData<double>(args.data[1]);
    auto &v0   = FlatVector::Validity(args.data[0]);
    auto out   = FlatVector::GetData<string_t>(result);
    auto &outv = FlatVector::Validity(result);

    for (idx_t i = 0; i < n; i++) {
        if (!v0.RowIsValid(i)) { outv.SetInvalid(i); continue; }
        const double c = std::cos(angle[i]), s = std::sin(angle[i]);
        AFFINE m = IdentityAffine();
        m.afac = c; m.bfac = -s;
        m.dfac = s; m.efac =  c;
        out[i] = ApplyAffineToTgeo(result, in[i], m);
    }
    if (n == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

void TgeompointFunctions::Tgeo_scale_geom_origin(DataChunk &args, ExpressionState &state, Vector &result) {
    const idx_t n = args.size();
    for (idx_t c = 0; c < 3; c++) args.data[c].Flatten(n);
    auto in_t  = FlatVector::GetData<string_t>(args.data[0]);
    auto in_g  = FlatVector::GetData<string_t>(args.data[1]);
    auto in_o  = FlatVector::GetData<string_t>(args.data[2]);
    auto &v0   = FlatVector::Validity(args.data[0]);
    auto &v1   = FlatVector::Validity(args.data[1]);
    auto &v2   = FlatVector::Validity(args.data[2]);
    auto out   = FlatVector::GetData<string_t>(result);
    auto &outv = FlatVector::Validity(result);

    for (idx_t i = 0; i < n; i++) {
        if (!v0.RowIsValid(i) || !v1.RowIsValid(i) || !v2.RowIsValid(i)) { outv.SetInvalid(i); continue; }
        size_t tsz = in_t[i].GetSize();
        uint8_t *tcopy = (uint8_t *)malloc(tsz);
        memcpy(tcopy, in_t[i].GetData(), tsz);
        Temporal *temp = reinterpret_cast<Temporal *>(tcopy);
        GSERIALIZED *scale  = GeometryToGSerialized(in_g[i], 0);
        GSERIALIZED *origin = GeometryToGSerialized(in_o[i], 0);
        if (!scale || !origin) { free(temp); if (scale) free(scale); if (origin) free(origin); outv.SetInvalid(i); continue; }
        Temporal *ret = tgeo_scale_geo(temp, scale, origin);
        free(temp); free(scale); free(origin);
        if (!ret) { outv.SetInvalid(i); continue; }
        size_t rsz = temporal_mem_size(ret);
        uint8_t *rbuf = (uint8_t *)malloc(rsz);
        memcpy(rbuf, ret, rsz);
        out[i] = StringVector::AddStringOrBlob(result, string_t(reinterpret_cast<const char *>(rbuf), rsz));
        free(rbuf); free(ret);
    }
    if (n == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

} // namespace duckdb
