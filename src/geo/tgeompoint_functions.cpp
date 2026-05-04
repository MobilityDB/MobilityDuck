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
#include "spatial/geometry/wkb_writer.hpp"
#include "spatial/modules/geos/geos_geometry.hpp"
#include "spatial/modules/geos/geos_serde.hpp"

#include "duckdb/common/types.hpp"
#include "duckdb/common/types/vector.hpp"

namespace duckdb {

namespace {

inline int ea_disjoint_geo_tgeo_dispatch(const GSERIALIZED *gs, const Temporal *temp, bool ever) {
	return ever ? edisjoint_tgeo_geo(temp, gs) : adisjoint_tgeo_geo(temp, gs);
}

inline int ea_intersects_geo_tgeo_dispatch(const GSERIALIZED *gs, const Temporal *temp, bool ever) {
    return ever ? eintersects_tgeo_geo(temp, gs) : aintersects_tgeo_geo(temp, gs);
}

/* MobilityDB EA_spatialrel_geo_tspatial style: (gs, temp, …); MEOS uses (temp, gs, …). */
inline int ea_dwithin_geo_tgeo_dispatch(const GSERIALIZED *gs, const Temporal *temp, double dist, bool ever) {
	return ever ? edwithin_tgeo_geo(temp, gs, dist) : adwithin_tgeo_geo(temp, gs, dist);
}

inline int ea_touches_tpoint_geo_dispatch(const GSERIALIZED *gs, const Temporal *temp, bool ever) {
	return ever ? etouches_tpoint_geo(temp, gs) : atouches_tpoint_geo(temp, gs);
}

enum class SpatialarrElemKind { TGEOM_POINT, GEOMETRY_BLOB };

static SpatialarrElemKind spatialarr_elem_kind_from_list(const LogicalType &list_type) {
	if (list_type.id() != LogicalTypeId::LIST) {
		throw InvalidInputException("Spatialarr_as_text/asEWKT: expected a LIST input");
	}
	const auto &child = ListType::GetChildType(list_type);
	const auto &alias = child.GetAlias();
	if (alias == "TGEOMPOINT" || alias == "TGEOGPOINT") {
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
			if (maxdd < 0) {
				throw InvalidInputException("Spatialarr_as_text/asEWKT: maxdecimaldigits must be non-negative");
			}
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
                throw InvalidInputException("Invalid TGEOMPOINT input: " + input_str);
            }

            size_t data_size = temporal_mem_size(temp);
            uint8_t *data_buffer = (uint8_t*)malloc(data_size);
            if (!data_buffer) {
                free(temp);
                throw InvalidInputException("Failed to allocate memory for TGEOMPOINT");
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
                throw InvalidInputException("Invalid TGEOMPOINT data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT input: " + input_blob.GetString());
            }

            char* ret = tspatial_as_text(temp, 15);
            if (!ret) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TGEOMPOINT to text: " + input_blob.GetString());
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
                throw InvalidInputException("Invalid TGEOMPOINT data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT input: " + input_blob.GetString());
            }

            char *ewkt = tspatial_as_ewkt(temp, OUT_DEFAULT_DECIMAL_DIGITS);
            if (!ewkt) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TGEOMPOINT to EWKT: " + input_blob.GetString());
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

/* ***************************************************
* Constructor functions
****************************************************/

void TgeompointFunctions::Tpointinst_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    auto row_count = args.size();
    auto arg_count = args.ColumnCount();
    int32_t srid = 0;
    if (arg_count > 2) {
        auto &srid_child = args.data[2];
        srid_child.Flatten(row_count);
        srid = srid_child.GetValue(0).GetValue<int32_t>();
    }

    BinaryExecutor::Execute<string_t, timestamp_tz_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t geometry_blob, timestamp_tz_t ts_duckdb) -> string_t {
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
            Temporal *ret = (Temporal *) tpointinst_make(gs, static_cast<TimestampTz>(ts_meos.value));
            if (ret == NULL) {
                free(gs);
                throw InvalidInputException("Failed to create TGEOMPOINT from geometry and timestamp");
            }

            size_t ret_size = temporal_mem_size(ret);
            char *ret_data = (char *)malloc(ret_size);
            if (!ret_data) {
                free(ret);
                free(gs);
                throw InvalidInputException("Failed to allocate memory for TGEOMPOINT");
            }
            memcpy(ret_data, ret, ret_size);

            string_t temp_str(ret_data, ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, temp_str);

            free(ret);
            free(gs);
            free(ret_data);
            return stored_data;
        });
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
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
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
                return string_t();
            }
            STBox *stbox = tspatial_to_stbox(temp);
            if (!stbox) {
                free(temp);
                throw InvalidInputException("Failed to convert TGEOMPOINT to STBOX");
                return string_t();
            }

            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
                free(temp);
                throw InvalidInputException("Failed to allocate memory for STBOX");
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

void TgeompointFunctions::Tgeompoint_start_value(DataChunk &args, ExpressionState &state, Vector &result) {
    // Adapted from Temporal_start_value
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
                return string_t();
            }

            Datum start_datum = temporal_start_value(temp);
            GSERIALIZED *start_geom = DatumGetGserializedP(start_datum);
            if (!start_geom) {
                free(temp);
                throw InvalidInputException("Failed to get start value from TGEOMPOINT");
            }

            string_t geometry_blob = GSerializedToGeometry(start_geom, state, result);
            string_t stored_result = StringVector::AddStringOrBlob(result, geometry_blob);
            free(start_geom);
            free(temp);
            return stored_result;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tgeompoint_end_value(DataChunk &args, ExpressionState &state, Vector &result) {
    // Adapted from Temporal_end_value
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
                return string_t();
            }

            Datum end_datum = temporal_end_value(temp);
            GSERIALIZED *end_geom = DatumGetGserializedP(end_datum);
            if (!end_geom) {
                free(temp);
                throw InvalidInputException("Failed to get end value from TGEOMPOINT");
            }

            string_t geometry_blob = GSerializedToGeometry(end_geom, state, result);
            string_t stored_result = StringVector::AddStringOrBlob(result, geometry_blob);
            free(end_geom);
            free(temp);
            return stored_result;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tgeompoint_sequence_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &child_vec = ListVector::GetEntry(args.data[0]);
    auto child_count = ListVector::GetListSize(args.data[0]);

    UnifiedVectorFormat input_vdata;
    child_vec.ToUnifiedFormat(child_count, input_vdata);
    
    auto arg_count = args.ColumnCount();
    auto row_count = args.size();
    meosType temptype = TemporalHelpers::GetTemptypeFromAlias(result.GetType().GetAlias().c_str());
    interpType interp = temptype_continuous(temptype) ? LINEAR : STEP;
    bool lower_inc = true;
    bool upper_inc = true;

    if (arg_count > 1) {
        auto &interp_child = args.data[1];
        interp_child.Flatten(row_count);
        auto interp_str = interp_child.GetValue(0).ToString();
        interp = interptype_from_string(interp_str.c_str());
    }
    if (arg_count > 2) {
        auto &lower_inc_child = args.data[2];
        lower_inc = lower_inc_child.GetValue(0).GetValue<bool>();
    }
    if (arg_count > 3) {
        auto &upper_inc_child = args.data[3];
        upper_inc = upper_inc_child.GetValue(0).GetValue<bool>();
    }

    UnaryExecutor::Execute<list_entry_t, string_t>(
        args.data[0], result, args.size(),
        [&](const list_entry_t &entry) {
            const auto offset = entry.offset;
            const auto length = entry.length;

            int32_t valid_count = 0;
            for (idx_t out_idx = offset; out_idx < offset + length; out_idx++) {
                const auto row_idx = input_vdata.sel->get_index(out_idx);
                if (!input_vdata.validity.RowIsValid(row_idx)) {
                    continue;
                }

                auto &blob = UnifiedVectorFormat::GetData<string_t>(input_vdata)[row_idx];
                size_t data_size = blob.GetSize();
                if (data_size < sizeof(void*)) {
                    continue;
                }
                valid_count++;
            }

            TInstant **instants = (TInstant **)malloc(valid_count * sizeof(TInstant *));
            if (!instants) {
                throw InternalException("Memory allocation failed in Tgeompoint_sequence_constructor");
            }

            idx_t valid_idx = 0;
            for (idx_t out_idx = offset; out_idx < offset + length; out_idx++) {
                const auto row_idx = input_vdata.sel->get_index(out_idx);
                if (!input_vdata.validity.RowIsValid(row_idx)) {
                    continue;
                }

                auto &blob = UnifiedVectorFormat::GetData<string_t>(input_vdata)[row_idx];
                const uint8_t *data = reinterpret_cast<const uint8_t*>(blob.GetData());
                size_t data_size = blob.GetSize();
                if (data_size < sizeof(void*)) {
                    continue;
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, data, data_size);
                Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

                if (!temp) {
                    free(data_copy);
                    throw InvalidInputException("Failure in Tgeompoint_sequence_constructor: unable to convert WKB to temporal");
                }
                instants[valid_idx] = (TInstant*)temp;
                valid_idx++;
            }

            TSequence *seq = tsequence_make((TInstant **)instants, valid_count,
                lower_inc, upper_inc, interp, true);
            if (!seq) {
                for (idx_t j = 0; j < valid_count; j++) {
                    free(instants[j]);
                }
                free(instants);
                throw InternalException("Failure in Tgeompoint_sequence_constructor: unable to create sequence");
            }

            size_t temp_size = temporal_mem_size((Temporal*)seq);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, (Temporal*)seq, temp_size);
            string_t result_str(reinterpret_cast<char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, result_str);
            free(seq);
            for (idx_t j = 0; j < valid_count; j++) {
                free(instants[j]);
            }
            free(instants);
            free(temp_data);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
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

void TgeompointFunctions::Temporal_to_tstzspan(DataChunk &args, ExpressionState &state, Vector &result) {
    Temporal_to_tstzspan_common(args.data[0], result, args.size());
}

bool TgeompointFunctions::Temporal_to_tstzspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Temporal_to_tstzspan_common(source, result, count);
    return true;
}

/* ***************************************************
 * Accessor functions
 ****************************************************/

void TgeompointFunctions::Tgeompoint_value(DataChunk &args, ExpressionState &state, Vector &result) {
    // Adapted from Tinstant_value
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t tgeom_blob) -> string_t {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t*)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(tgeom_data_copy);
            if (!temp) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            if (temp->subtype != TINSTANT) {
                free(temp);
                throw InvalidInputException("The temporal value must be of subtype Instant");
            }
            Datum ret = tinstant_value((TInstant*)temp);
            GSERIALIZED *gs = DatumGetGserializedP(ret);
            if (!gs) {
                free(temp);
                throw InvalidInputException("Failed to get geometry from datum");
            }

            string_t geometry_blob = GSerializedToGeometry(gs, state, result);
            string_t stored_data = StringVector::AddStringOrBlob(result, geometry_blob);

            free(gs);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
 * Restriction functions
 ****************************************************/

void TgeompointFunctions::Tgeompoint_at_value(DataChunk &args, ExpressionState &state, Vector &result) {
    // Adapted from Temporal_at_value
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom_blob, string_t geometry_blob, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t*)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(tgeom_data_copy);
            if (!temp) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            int32 srid = tspatial_srid(temp);
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                free(temp);
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            Temporal *ret = temporal_restrict_value(temp, (Datum)gs, true);
            if (!ret) {
                free(temp);
                free(gs);
                mask.SetInvalid(idx);
                return string_t();
            }

            size_t ret_size = temporal_mem_size(ret);
            uint8_t *ret_data = (uint8_t*)malloc(ret_size);
            memcpy(ret_data, ret, ret_size);

            string_t ret_string(reinterpret_cast<const char*>(ret_data), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_string);
            
            free(ret_data);
            free(ret);
            free(temp);
            free(gs);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tgeompoint_value_at_timestamptz(DataChunk &args, ExpressionState &state, Vector &result) {
    // Adapted from Temporal_value_at_timestamptz
    BinaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_blob, timestamp_tz_t ts_duckdb, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
            Datum ret;
            bool found = temporal_value_at_timestamptz(temp, (TimestampTz)ts_meos.value, true, &ret);
            free(temp);
            if (!found) {
                mask.SetInvalid(idx);
                return string_t();
            }

            GSERIALIZED *gs = DatumGetGserializedP(ret);
            if (!gs) {
                throw InvalidInputException("Failed to get geometry from datum");
            }

            string_t geometry_blob = GSerializedToGeometry(gs, state, result);
            string_t stored_data = StringVector::AddStringOrBlob(result, geometry_blob);

            free(gs);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
 * Stops function
 ****************************************************/

void TgeompointFunctions::Tgeompoint_stops(DataChunk &args, ExpressionState &state, Vector &result) {
    TernaryExecutor::ExecuteWithNulls<string_t, double, interval_t, string_t>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t input_blob, double maxdist, interval_t minduration_duckdb, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            MeosInterval minduration = IntervaltToInterval(minduration_duckdb);
            TSequenceSet *ret = temporal_stops(temp, maxdist, &minduration);
            free(temp);
            if (!ret) {
                mask.SetInvalid(idx);
                return string_t();
            }

            size_t ret_size = temporal_mem_size((Temporal*)ret);
            uint8_t *ret_data = (uint8_t*)malloc(ret_size);
            memcpy(ret_data, ret, ret_size);
            string_t ret_string(reinterpret_cast<const char*>(ret_data), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_string);
            free(ret_data);
            free(ret);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
 * Spatial functions
 ****************************************************/
void TgeompointFunctions::Tpoint_get_x(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            Temporal *ret = tpoint_get_x(temp);
            free(temp);
            if (!ret) {
                throw InvalidInputException("Failed to get X coordinate from TGEOMPOINT");
            }
            size_t ret_size = temporal_mem_size(ret);
            uint8_t *ret_data = (uint8_t*)malloc(ret_size);
            memcpy(ret_data, ret, ret_size);
            string_t ret_string(reinterpret_cast<const char*>(ret_data), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_string);
            free(ret_data);
            free(ret);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tpoint_get_y(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            Temporal *ret = tpoint_get_y(temp);
            free(temp);
            if (!ret) {
                throw InvalidInputException("Failed to get Y coordinate from TGEOMPOINT");
            }
            size_t ret_size = temporal_mem_size(ret);
            uint8_t *ret_data = (uint8_t*)malloc(ret_size);
            memcpy(ret_data, ret, ret_size);
            string_t ret_string(reinterpret_cast<const char*>(ret_data), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_string);
            free(ret_data);
            free(ret);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tpoint_get_z(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            Temporal *ret = tpoint_get_z(temp);
            free(temp);
            if (!ret) {
                throw InvalidInputException("Failed to get Z coordinate from TGEOMPOINT");
            }
            size_t ret_size = temporal_mem_size(ret);
            uint8_t *ret_data = (uint8_t*)malloc(ret_size);
            memcpy(ret_data, ret, ret_size);
            string_t ret_string(reinterpret_cast<const char*>(ret_data), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_string);
            free(ret_data);
            free(ret);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tpoint_length(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, double>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) -> double {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            double ret = tpoint_length(temp);
            free(temp);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tpoint_cumulative_length(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            Temporal *ret = tpoint_cumulative_length(temp);
            free(temp);
            if (!ret) {
                throw InvalidInputException("Failed to compute cumulative length of TGEOMPOINT");
            }
            size_t ret_size = temporal_mem_size(ret);
            uint8_t *ret_data = (uint8_t*)malloc(ret_size);
            memcpy(ret_data, ret, ret_size);
            string_t ret_string(reinterpret_cast<const char*>(ret_data), ret_size);
            string_t stored_result = StringVector::AddStringOrBlob(result, ret_string);
            free(ret_data);
            free(ret);
            return stored_result;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tpoint_twcentroid(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            GSERIALIZED *ret = tpoint_twcentroid(temp);
            free(temp);
            if (!ret) {
                throw InvalidInputException("Failed to compute time-weighted centroid of TGEOMPOINT");
            }
            string_t geometry_blob = GSerializedToGeometry(ret, state, result);
            string_t stored_result = StringVector::AddStringOrBlob(result, geometry_blob);
            free(ret);
            return stored_result;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tpoint_direction(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, double>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) -> double {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            double ret;
            bool has_direction = tpoint_direction(temp, &ret);
            if (!has_direction) {                   
                free(temp);
                throw InvalidInputException("Failed to compute direction of TGEOMPOINT");
            }
            free(temp);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tpoint_azimuth(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            Temporal *ret = tpoint_azimuth(temp);
            free(temp);
            if (!ret) {
                throw InvalidInputException("Failed to compute azimuth of TGEOMPOINT"); 
            }
            size_t ret_size = temporal_mem_size(ret);
            uint8_t *ret_data = (uint8_t*)malloc(ret_size);
            memcpy(ret_data, ret, ret_size);
            string_t ret_string(reinterpret_cast<const char*>(ret_data), ret_size);
            string_t stored_result = StringVector::AddStringOrBlob(result, ret_string);
            free(ret_data);
            free(ret);
            return stored_result;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tpoint_angular_difference(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            Temporal *ret = tpoint_angular_difference(temp);
            free(temp);
            if (!ret) {
                throw InvalidInputException("Failed to compute angular difference of TGEOMPOINT");
            }
            size_t ret_size = temporal_mem_size(ret);
            uint8_t *ret_data = (uint8_t*)malloc(ret_size);
            memcpy(ret_data, ret, ret_size);
            string_t ret_string(reinterpret_cast<const char*>(ret_data), ret_size);
            string_t stored_result = StringVector::AddStringOrBlob(result, ret_string);
            free(ret_data);
            free(ret);
            return stored_result;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tpoint_is_simple(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, bool>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) -> bool {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            bool ret = tpoint_is_simple(temp);
            free(temp);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tpoint_make_simple(DataChunk &args, ExpressionState &state, Vector &result) {
	idx_t total_count = 0;
	UnaryExecutor::Execute<string_t, list_entry_t>(
	    args.data[0], result, args.size(),
	    [&](string_t input_blob) -> list_entry_t {
		    const uint8_t *data = reinterpret_cast<const uint8_t *>(input_blob.GetData());
		    size_t data_size = input_blob.GetSize();
		    if (data_size < sizeof(void *)) {
			    throw InvalidInputException("Invalid TGEOMPOINT data: insufficient size");
		    }
		    uint8_t *data_copy = (uint8_t *)malloc(data_size);
		    memcpy(data_copy, data, data_size);
		    Temporal *temp = reinterpret_cast<Temporal *>(data_copy);
		    if (!temp) {
			    free(data_copy);
			    throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
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

void TgeompointFunctions::Tpoint_trajectory(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            GSERIALIZED *gs = tpoint_trajectory(temp, false);
            if (!gs) {
                free(temp);
                throw InvalidInputException("Failed to get trajectory from TGEOMPOINT");
            }

            string_t geometry_blob = GSerializedToGeometry(gs, state, result);
            string_t stored_result = StringVector::AddStringOrBlob(result, geometry_blob);

            free(gs);
            free(temp);
            return stored_result;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tpoint_trajectory_gs(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            GSERIALIZED *gs = tpoint_trajectory(temp, false);
            if (!gs) {
                free(temp);
                throw InvalidInputException("Failed to get trajectory from TGEOMPOINT");
            }

            size_t gs_size = VARSIZE(gs);
            uint8_t *gs_data = (uint8_t*)malloc(gs_size);
            memcpy(gs_data, gs, gs_size);
            string_t gs_string(reinterpret_cast<const char*>(gs_data), gs_size);
            string_t stored_result = StringVector::AddStringOrBlob(result, gs_string);
            free(gs_data);
            free(gs);
            free(temp);
            return stored_result;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tgeo_at_geom(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom_blob, string_t geometry_blob, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t*)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal*>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            int32 srid = tspatial_srid(tgeom);
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                free(tgeom);
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            Temporal *ret = tgeo_at_geom(tgeom, gs);
            if (!ret) {
                free(tgeom);
                free(gs);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            uint8_t *ret_data = (uint8_t*)malloc(ret_size);
            memcpy(ret_data, ret, ret_size);
            string_t ret_string(reinterpret_cast<const char*>(ret_data), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_string);
            free(ret_data);
            free(ret);
            free(gs);
            free(tgeom);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tgeo_minus_geom(DataChunk &args, ExpressionState &state, Vector &result) {
	const idx_t count = args.size();

	auto minus_geom_common = [&](string_t tgeom_blob, string_t geometry_blob, const Span *zspan,
	                             ValidityMask &mask, idx_t idx) -> string_t {
		const uint8_t *tgeom_data = reinterpret_cast<const uint8_t *>(tgeom_blob.GetData());
		size_t tgeom_data_size = tgeom_blob.GetSize();
		if (tgeom_data_size < sizeof(void *)) {
			throw InvalidInputException("Invalid TGEOMPOINT data: insufficient size");
		}
		uint8_t *tgeom_data_copy = (uint8_t *)malloc(tgeom_data_size);
		memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
		Temporal *tgeom = reinterpret_cast<Temporal *>(tgeom_data_copy);
		if (!tgeom) {
			free(tgeom_data_copy);
			throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
		}

		int32 srid = tspatial_srid(tgeom);
		GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
		if (!gs) {
			free(tgeom);
			throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
		}

		Temporal *ret = zspan ? tpoint_minus_geom(tgeom, gs, zspan) : tgeo_minus_geom(tgeom, gs);
		free(tgeom);
		free(gs);
		if (!ret) {
			mask.SetInvalid(idx);
			return string_t();
		}
		size_t ret_size = temporal_mem_size(ret);
		uint8_t *ret_data = (uint8_t *)malloc(ret_size);
		memcpy(ret_data, ret, ret_size);
		string_t ret_string(reinterpret_cast<const char *>(ret_data), ret_size);
		string_t stored_data = StringVector::AddStringOrBlob(result, ret_string);
		free(ret_data);
		free(ret);
		return stored_data;
	};

	if (args.ColumnCount() == 2) {
		BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
		    args.data[0], args.data[1], result, count,
		    [&](string_t tgeom_blob, string_t geometry_blob, ValidityMask &mask, idx_t idx) -> string_t {
			    return minus_geom_common(tgeom_blob, geometry_blob, nullptr, mask, idx);
		    });
	} else if (args.ColumnCount() == 3) {
		TernaryExecutor::ExecuteWithNulls<string_t, string_t, string_t, string_t>(
		    args.data[0], args.data[1], args.data[2], result, count,
		    [&](string_t tgeom_blob, string_t geometry_blob, string_t span_blob, ValidityMask &mask,
		        idx_t idx) -> string_t {
			    size_t span_size = span_blob.GetSize();
			    if (span_size < sizeof(void *)) {
				    throw InvalidInputException("Invalid floatspan data: insufficient size");
			    }
			    uint8_t *span_copy = (uint8_t *)malloc(span_size);
			    memcpy(span_copy, span_blob.GetData(), span_size);
			    Span *zspan = reinterpret_cast<Span *>(span_copy);
			    try {
				    string_t out = minus_geom_common(tgeom_blob, geometry_blob, zspan, mask, idx);
				    free(span_copy);
				    return out;
			    } catch (...) {
				    free(span_copy);
				    throw;
			    }
		    });
	} else {
		throw InternalException("Tgeo_minus_geom: expected 2 or 3 arguments");
	}

	if (count == 1) {
		result.SetVectorType(VectorType::CONSTANT_VECTOR);
	}
}

void TgeompointFunctions::Tgeo_minus_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    bool border_inc = true;
    if (args.ColumnCount() > 2) {
        border_inc = args.data[2].GetValue(0).GetValue<bool>();
    }
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom_blob, string_t stbox_blob, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t*)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal*>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            const uint8_t *stbox_raw = reinterpret_cast<const uint8_t *>(stbox_blob.GetData());
            size_t stbox_size = stbox_blob.GetSize();
            uint8_t *stbox_data_copy = (uint8_t *)malloc(stbox_size);
            memcpy(stbox_data_copy, stbox_raw, stbox_size);
            STBox *stbox = reinterpret_cast<STBox *>(stbox_data_copy);
            if (!stbox) {
                free(tgeom_data_copy);
                free(stbox_data_copy);
                throw InvalidInputException("Invalid STBOX data: null pointer");
            }

            Temporal *ret = tgeo_minus_stbox(tgeom, stbox, border_inc);
            if (!ret) {
                free(tgeom_data_copy);
                free(stbox_data_copy);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            uint8_t *ret_data = (uint8_t*)malloc(ret_size);
            memcpy(ret_data, ret, ret_size);
            string_t ret_string(reinterpret_cast<const char*>(ret_data), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_string);
            free(ret_data);
            free(ret);
            free(stbox_data_copy);
            free(tgeom_data_copy);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tgeo_at_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    D_ASSERT(args.ColumnCount() == 2 || args.ColumnCount() == 3);

    auto run = [&](string_t tgeom_blob, string_t stbox_blob, bool border_inc, ValidityMask &mask, idx_t idx) -> string_t {
        const uint8_t *tgeom_data = reinterpret_cast<const uint8_t *>(tgeom_blob.GetData());
        size_t tgeom_data_size = tgeom_blob.GetSize();
        uint8_t *tgeom_data_copy = (uint8_t *)malloc(tgeom_data_size);
        memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
        Temporal *tgeom = reinterpret_cast<Temporal *>(tgeom_data_copy);
        if (!tgeom) {
            free(tgeom_data_copy);
            throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
        }

        const uint8_t *stbox_raw = reinterpret_cast<const uint8_t *>(stbox_blob.GetData());
        size_t stbox_size = stbox_blob.GetSize();
        uint8_t *stbox_data_copy = (uint8_t *)malloc(stbox_size);
        memcpy(stbox_data_copy, stbox_raw, stbox_size);
        STBox *stbox = reinterpret_cast<STBox *>(stbox_data_copy);
        if (!stbox) {
            free(tgeom_data_copy);
            free(stbox_data_copy);
            throw InvalidInputException("Invalid STBOX data: null pointer");
        }

        Temporal *ret = tgeo_at_stbox(tgeom, stbox, border_inc);
        if (!ret) {
            free(tgeom_data_copy);
            free(stbox_data_copy);
            mask.SetInvalid(idx);
            return string_t();
        }
        size_t ret_size = temporal_mem_size(ret);
        uint8_t *ret_data = (uint8_t *)malloc(ret_size);
        memcpy(ret_data, ret, ret_size);
        string_t ret_string(reinterpret_cast<const char *>(ret_data), ret_size);
        string_t stored_data = StringVector::AddStringOrBlob(result, ret_string);
        free(ret_data);
        free(ret);
        free(stbox_data_copy);
        free(tgeom_data_copy);
        return stored_data;
    };

    if (args.ColumnCount() == 2) {
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
            args.data[0], args.data[1], result, args.size(),
            [&](string_t tgeom_blob, string_t stbox_blob, ValidityMask &mask, idx_t idx) -> string_t {
                return run(tgeom_blob, stbox_blob, true, mask, idx);
            });
    } else {
        TernaryExecutor::ExecuteWithNulls<string_t, string_t, bool, string_t>(
            args.data[0], args.data[1], args.data[2], result, args.size(),
            [&](string_t tgeom_blob, string_t stbox_blob, bool border_inc, ValidityMask &mask, idx_t idx) -> string_t {
                return run(tgeom_blob, stbox_blob, border_inc, mask, idx);
            });
    }
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tspatial_transform(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, int32_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom_blob, int32_t srid) {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t*)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal*>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            Temporal *ret = tspatial_transform(tgeom, srid);
            if (ret == NULL) {
                free(tgeom);
                throw InvalidInputException("Failed to transform TGEOMPOINT");
            }

            size_t ret_size = temporal_mem_size(ret);
            char *ret_data = (char *)malloc(ret_size);
            if (!ret_data) {
                free(ret);
                free(tgeom);
                throw InvalidInputException("Failed to allocate memory for TGEOMPOINT");
            }
            memcpy(ret_data, ret, ret_size);

            string_t ret_str(reinterpret_cast<const char*>(ret_data), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);

            free(ret_data);
            free(ret);
            free(tgeom);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
 * Spatial relationships
 ****************************************************/
void TgeompointFunctions::Econtains_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t geometry_blob, string_t tgeom_blob, ValidityMask &mask, idx_t idx) -> bool {
            int32 srid = 0;
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t*)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal*>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                free(gs);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            int ret = econtains_geo_tgeo(gs, tgeom);
            free(tgeom);
            free(gs);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Acontains_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t geometry_blob, string_t tgeom_blob, ValidityMask &mask, idx_t idx) -> bool {
            int32 srid = 0;
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t*)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal*>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                free(gs);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            int ret = acontains_geo_tgeo(gs, tgeom);
            free(tgeom);
            free(gs);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Edisjoint_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t geometry_blob, string_t tgeom_blob, ValidityMask &mask, idx_t idx) -> bool {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t *>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            if (tgeom_data_size < sizeof(void *)) {
                throw InvalidInputException("Invalid TGEOMPOINT data: insufficient size");
            }
            uint8_t *tgeom_data_copy = (uint8_t *)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal *>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            int32 srid = tspatial_srid(tgeom);
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                free(tgeom);
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            int ret = ea_disjoint_geo_tgeo_dispatch(gs, tgeom, true);
            free(tgeom);
            free(gs);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Edisjoint_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom_blob, string_t geometry_blob, ValidityMask &mask, idx_t idx) -> bool {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t *>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            if (tgeom_data_size < sizeof(void *)) {
                throw InvalidInputException("Invalid TGEOMPOINT data: insufficient size");
            }
            uint8_t *tgeom_data_copy = (uint8_t *)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal *>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            int32 srid = tspatial_srid(tgeom);
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                free(tgeom);
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            int ret = edisjoint_tgeo_geo(tgeom, gs);
            free(tgeom);
            free(gs);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Edisjoint_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom1_blob, string_t tgeom2_blob, ValidityMask &mask, idx_t idx) -> bool {
            const uint8_t *tgeom1_data = reinterpret_cast<const uint8_t*>(tgeom1_blob.GetData());
            size_t tgeom1_data_size = tgeom1_blob.GetSize();
            uint8_t *tgeom1_data_copy = (uint8_t*)malloc(tgeom1_data_size);
            memcpy(tgeom1_data_copy, tgeom1_data, tgeom1_data_size);
            Temporal *tgeom1 = reinterpret_cast<Temporal*>(tgeom1_data_copy);
            if (!tgeom1) {
                free(tgeom1_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            
            const uint8_t *tgeom2_data = reinterpret_cast<const uint8_t*>(tgeom2_blob.GetData());
            size_t tgeom2_data_size = tgeom2_blob.GetSize();
            uint8_t *tgeom2_data_copy = (uint8_t*)malloc(tgeom2_data_size);
            memcpy(tgeom2_data_copy, tgeom2_data, tgeom2_data_size);
            Temporal *tgeom2 = reinterpret_cast<Temporal*>(tgeom2_data_copy);
            if (!tgeom2) {
                free(tgeom1);
                free(tgeom2_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            int ret = edisjoint_tgeo_tgeo(tgeom1, tgeom2);
            free(tgeom1);
            free(tgeom2);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Adisjoint_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t geometry_blob, string_t tgeom_blob, ValidityMask &mask, idx_t idx) -> bool {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t *>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            if (tgeom_data_size < sizeof(void *)) {
                throw InvalidInputException("Invalid TGEOMPOINT data: insufficient size");
            }
            uint8_t *tgeom_data_copy = (uint8_t *)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal *>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            int32 srid = tspatial_srid(tgeom);
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                free(tgeom);
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            // Same as MobilityDB EA_spatialrel_geo_tspatial(..., &ea_disjoint_geo_tgeo, ALWAYS)
            int ret = ea_disjoint_geo_tgeo_dispatch(gs, tgeom, false);
            free(tgeom);
            free(gs);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Adisjoint_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom_blob, string_t geometry_blob, ValidityMask &mask, idx_t idx) -> bool {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t *>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            if (tgeom_data_size < sizeof(void *)) {
                throw InvalidInputException("Invalid TGEOMPOINT data: insufficient size");
            }
            uint8_t *tgeom_data_copy = (uint8_t *)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal *>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            int32 srid = tspatial_srid(tgeom);
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                free(tgeom);
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            int ret = adisjoint_tgeo_geo(tgeom, gs);
            free(tgeom);
            free(gs);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Adisjoint_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom1_blob, string_t tgeom2_blob, ValidityMask &mask, idx_t idx) -> bool {
            const uint8_t *tgeom1_data = reinterpret_cast<const uint8_t*>(tgeom1_blob.GetData());
            size_t tgeom1_data_size = tgeom1_blob.GetSize();
            uint8_t *tgeom1_data_copy = (uint8_t*)malloc(tgeom1_data_size);
            memcpy(tgeom1_data_copy, tgeom1_data, tgeom1_data_size);
            Temporal *tgeom1 = reinterpret_cast<Temporal*>(tgeom1_data_copy);
            if (!tgeom1) {
                free(tgeom1_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            
            const uint8_t *tgeom2_data = reinterpret_cast<const uint8_t*>(tgeom2_blob.GetData());
            size_t tgeom2_data_size = tgeom2_blob.GetSize();
            uint8_t *tgeom2_data_copy = (uint8_t*)malloc(tgeom2_data_size);
            memcpy(tgeom2_data_copy, tgeom2_data, tgeom2_data_size);
            Temporal *tgeom2 = reinterpret_cast<Temporal*>(tgeom2_data_copy);
            if (!tgeom2) {
                free(tgeom1);
                free(tgeom2_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            // extern int adisjoint_tgeo_tgeo(const Temporal *temp1, const Temporal *temp2);
            int ret = adisjoint_tgeo_tgeo(tgeom1, tgeom2);
            free(tgeom1);
            free(tgeom2);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Eintersects_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom1_blob, string_t tgeom2_blob, ValidityMask &mask, idx_t idx) -> bool {
            const uint8_t *tgeom1_data = reinterpret_cast<const uint8_t*>(tgeom1_blob.GetData());
            size_t tgeom1_data_size = tgeom1_blob.GetSize();
            uint8_t *tgeom1_data_copy = (uint8_t*)malloc(tgeom1_data_size);
            memcpy(tgeom1_data_copy, tgeom1_data, tgeom1_data_size);
            Temporal *tgeom1 = reinterpret_cast<Temporal*>(tgeom1_data_copy);
            if (!tgeom1) {
                free(tgeom1_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            const uint8_t *tgeom2_data = reinterpret_cast<const uint8_t*>(tgeom2_blob.GetData());
            size_t tgeom2_data_size = tgeom2_blob.GetSize();
            uint8_t *tgeom2_data_copy = (uint8_t*)malloc(tgeom2_data_size);
            memcpy(tgeom2_data_copy, tgeom2_data, tgeom2_data_size);
            Temporal *tgeom2 = reinterpret_cast<Temporal*>(tgeom2_data_copy);
            if (!tgeom2) {
                free(tgeom1);
                free(tgeom2_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            int ret = eintersects_tgeo_tgeo(tgeom1, tgeom2);
            free(tgeom1);
            free(tgeom2);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Eintersects_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t geometry_blob, string_t tgeom_blob, ValidityMask &mask, idx_t idx) -> bool {
            int32 srid = 0;
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t*)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal*>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                free(gs);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            int ret = ea_intersects_geo_tgeo_dispatch(gs, tgeom, true);
            free(tgeom);
            free(gs);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Eintersects_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom_blob, string_t geometry_blob, ValidityMask &mask, idx_t idx) -> bool {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t*)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal*>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            int32 srid = tspatial_srid(tgeom);
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                free(tgeom);
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            int ret = eintersects_tgeo_geo(tgeom, gs);
            free(tgeom);
            free(gs);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Aintersects_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t geometry_blob, string_t tgeom_blob, ValidityMask &mask, idx_t idx) -> bool {
            int32 srid = 0;
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t *>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t *)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal *>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                free(gs);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            int ret = ea_intersects_geo_tgeo_dispatch(gs, tgeom, false);
            free(tgeom);
            free(gs);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Aintersects_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom_blob, string_t geometry_blob, ValidityMask &mask, idx_t idx) -> bool {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t *>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t *)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal *>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            int32 srid = tspatial_srid(tgeom);
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                free(tgeom);
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            int ret = aintersects_tgeo_geo(tgeom, gs);
            free(tgeom);
            free(gs);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Aintersects_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom1_blob, string_t tgeom2_blob, ValidityMask &mask, idx_t idx) -> bool {
            const uint8_t *tgeom1_data = reinterpret_cast<const uint8_t*>(tgeom1_blob.GetData());
            size_t tgeom1_data_size = tgeom1_blob.GetSize();
            uint8_t *tgeom1_data_copy = (uint8_t*)malloc(tgeom1_data_size);
            memcpy(tgeom1_data_copy, tgeom1_data, tgeom1_data_size);
            Temporal *tgeom1 = reinterpret_cast<Temporal*>(tgeom1_data_copy);
            if (!tgeom1) {
                free(tgeom1_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            const uint8_t *tgeom2_data = reinterpret_cast<const uint8_t*>(tgeom2_blob.GetData());
            size_t tgeom2_data_size = tgeom2_blob.GetSize();
            uint8_t *tgeom2_data_copy = (uint8_t*)malloc(tgeom2_data_size);
            memcpy(tgeom2_data_copy, tgeom2_data, tgeom2_data_size);
            Temporal *tgeom2 = reinterpret_cast<Temporal*>(tgeom2_data_copy);
            if (!tgeom2) {
                free(tgeom1);
                free(tgeom2_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            int ret = aintersects_tgeo_tgeo(tgeom1, tgeom2);
            free(tgeom1);
            free(tgeom2);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Etouches_geo_tpoint(DataChunk &args, ExpressionState &state, Vector &result) {
	BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
	    args.data[0], args.data[1], result, args.size(),
	    [&](string_t geometry_blob, string_t tgeom_blob, ValidityMask &mask, idx_t idx) -> bool {
		    const uint8_t *tgeom_data = reinterpret_cast<const uint8_t *>(tgeom_blob.GetData());
		    size_t tgeom_data_size = tgeom_blob.GetSize();
		    if (tgeom_data_size < sizeof(void *)) {
			    throw InvalidInputException("Invalid TGEOMPOINT data: insufficient size");
		    }
		    uint8_t *tgeom_data_copy = (uint8_t *)malloc(tgeom_data_size);
		    memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
		    Temporal *tgeom = reinterpret_cast<Temporal *>(tgeom_data_copy);
		    if (!tgeom) {
			    free(tgeom_data_copy);
			    throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
		    }

		    int32 srid = tspatial_srid(tgeom);
		    GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
		    if (!gs) {
			    free(tgeom);
			    throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
		    }

		    int ret = ea_touches_tpoint_geo_dispatch(gs, tgeom, true);
		    free(tgeom);
		    free(gs);
		    if (ret < 0) {
			    mask.SetInvalid(idx);
			    return false;
		    }
		    return ret;
	    });
	if (args.size() == 1) {
		result.SetVectorType(VectorType::CONSTANT_VECTOR);
	}
}

void TgeompointFunctions::Atouches_geo_tpoint(DataChunk &args, ExpressionState &state, Vector &result) {
	BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
	    args.data[0], args.data[1], result, args.size(),
	    [&](string_t geometry_blob, string_t tgeom_blob, ValidityMask &mask, idx_t idx) -> bool {
		    const uint8_t *tgeom_data = reinterpret_cast<const uint8_t *>(tgeom_blob.GetData());
		    size_t tgeom_data_size = tgeom_blob.GetSize();
		    if (tgeom_data_size < sizeof(void *)) {
			    throw InvalidInputException("Invalid TGEOMPOINT data: insufficient size");
		    }
		    uint8_t *tgeom_data_copy = (uint8_t *)malloc(tgeom_data_size);
		    memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
		    Temporal *tgeom = reinterpret_cast<Temporal *>(tgeom_data_copy);
		    if (!tgeom) {
			    free(tgeom_data_copy);
			    throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
		    }

		    int32 srid = tspatial_srid(tgeom);
		    GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
		    if (!gs) {
			    free(tgeom);
			    throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
		    }

		    int ret = ea_touches_tpoint_geo_dispatch(gs, tgeom, false);
		    free(tgeom);
		    free(gs);
		    if (ret < 0) {
			    mask.SetInvalid(idx);
			    return false;
		    }
		    return ret;
	    });
	if (args.size() == 1) {
		result.SetVectorType(VectorType::CONSTANT_VECTOR);
	}
}

void TgeompointFunctions::Etouches_tpoint_geo(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom_blob, string_t geometry_blob, ValidityMask &mask, idx_t idx) -> bool {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t *>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t *)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal *>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            int32 srid = tspatial_srid(tgeom);
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                free(tgeom);
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            int ret = etouches_tpoint_geo(tgeom, gs);
            free(tgeom);
            free(gs);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Atouches_tpoint_geo(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom_blob, string_t geometry_blob, ValidityMask &mask, idx_t idx) -> bool {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t *>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t *)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal *>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            int32 srid = tspatial_srid(tgeom);
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                free(tgeom);
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            int ret = atouches_tpoint_geo(tgeom, gs);
            free(tgeom);
            free(gs);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Edwithin_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    TernaryExecutor::ExecuteWithNulls<string_t, string_t, double, bool>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t tgeom1_blob, string_t tgeom2_blob, double dist, ValidityMask &mask, idx_t idx) -> bool {
            const uint8_t *tgeom1_data = reinterpret_cast<const uint8_t*>(tgeom1_blob.GetData());
            size_t tgeom1_data_size = tgeom1_blob.GetSize();
            uint8_t *tgeom1_data_copy = (uint8_t*)malloc(tgeom1_data_size);
            memcpy(tgeom1_data_copy, tgeom1_data, tgeom1_data_size);
            Temporal *tgeom1 = reinterpret_cast<Temporal*>(tgeom1_data_copy);
            if (!tgeom1) {
                free(tgeom1_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            const uint8_t *tgeom2_data = reinterpret_cast<const uint8_t*>(tgeom2_blob.GetData());
            size_t tgeom2_data_size = tgeom2_blob.GetSize();
            uint8_t *tgeom2_data_copy = (uint8_t*)malloc(tgeom2_data_size);
            memcpy(tgeom2_data_copy, tgeom2_data, tgeom2_data_size);
            Temporal *tgeom2 = reinterpret_cast<Temporal*>(tgeom2_data_copy);
            if (!tgeom2) {
                free(tgeom1);
                free(tgeom2_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            int ret = edwithin_tgeo_tgeo(tgeom1, tgeom2, dist);
            free(tgeom1);
            free(tgeom2);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Edwithin_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result) {
    TernaryExecutor::ExecuteWithNulls<string_t, string_t, double, bool>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t tgeom_blob, string_t geometry_blob, double dist, ValidityMask &mask, idx_t idx) -> bool {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t*)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal*>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            int32 srid = tspatial_srid(tgeom);
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                free(tgeom);
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            int ret = edwithin_tgeo_geo(tgeom, gs, dist);
            free(tgeom);
            free(gs);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Edwithin_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    TernaryExecutor::ExecuteWithNulls<string_t, string_t, double, bool>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t geometry_blob, string_t tgeom_blob, double dist, ValidityMask &mask, idx_t idx) -> bool {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t *>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            if (tgeom_data_size < sizeof(void *)) {
                throw InvalidInputException("Invalid TGEOMPOINT data: insufficient size");
            }
            uint8_t *tgeom_data_copy = (uint8_t *)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal *>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            int32 srid = tspatial_srid(tgeom);
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                free(tgeom);
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            int ret = ea_dwithin_geo_tgeo_dispatch(gs, tgeom, dist, true);
            free(tgeom);
            free(gs);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Adwithin_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result) {
    TernaryExecutor::ExecuteWithNulls<string_t, string_t, double, bool>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t tgeom_blob, string_t geometry_blob, double dist, ValidityMask &mask, idx_t idx) -> bool {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t *>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            if (tgeom_data_size < sizeof(void *)) {
                throw InvalidInputException("Invalid TGEOMPOINT data: insufficient size");
            }
            uint8_t *tgeom_data_copy = (uint8_t *)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal *>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            int32 srid = tspatial_srid(tgeom);
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                free(tgeom);
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            int ret = adwithin_tgeo_geo(tgeom, gs, dist);
            free(tgeom);
            free(gs);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Adwithin_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    TernaryExecutor::ExecuteWithNulls<string_t, string_t, double, bool>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t geometry_blob, string_t tgeom_blob, double dist, ValidityMask &mask, idx_t idx) -> bool {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t *>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            if (tgeom_data_size < sizeof(void *)) {
                throw InvalidInputException("Invalid TGEOMPOINT data: insufficient size");
            }
            uint8_t *tgeom_data_copy = (uint8_t *)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal *>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            int32 srid = tspatial_srid(tgeom);
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                free(tgeom);
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            int ret = ea_dwithin_geo_tgeo_dispatch(gs, tgeom, dist, false);
            free(tgeom);
            free(gs);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Adwithin_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    TernaryExecutor::ExecuteWithNulls<string_t, string_t, double, bool>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t tgeom1_blob, string_t tgeom2_blob, double dist, ValidityMask &mask, idx_t idx) -> bool {
            const uint8_t *tgeom1_data = reinterpret_cast<const uint8_t*>(tgeom1_blob.GetData());
            size_t tgeom1_data_size = tgeom1_blob.GetSize();
            uint8_t *tgeom1_data_copy = (uint8_t*)malloc(tgeom1_data_size);
            memcpy(tgeom1_data_copy, tgeom1_data, tgeom1_data_size);
            Temporal *tgeom1 = reinterpret_cast<Temporal*>(tgeom1_data_copy);
            if (!tgeom1) {
                free(tgeom1_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            const uint8_t *tgeom2_data = reinterpret_cast<const uint8_t*>(tgeom2_blob.GetData());
            size_t tgeom2_data_size = tgeom2_blob.GetSize();
            uint8_t *tgeom2_data_copy = (uint8_t*)malloc(tgeom2_data_size);
            memcpy(tgeom2_data_copy, tgeom2_data, tgeom2_data_size);
            Temporal *tgeom2 = reinterpret_cast<Temporal*>(tgeom2_data_copy);
            if (!tgeom2) {
                free(tgeom1);
                free(tgeom2_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            int ret = adwithin_tgeo_tgeo(tgeom1, tgeom2, dist);
            free(tgeom1);
            free(tgeom2);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
 * Temporal-spatial relationships
 ****************************************************/
void TgeompointFunctions::Tcontains_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    const idx_t count = args.size();
    auto eval = [&](string_t geometry_blob, string_t tgeom_blob, bool restr, bool at_value, ValidityMask &mask,
                    idx_t idx) -> string_t {
        int32 srid = 0;
        GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
        if (!gs) {
            throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
        }

        const uint8_t *tgeom_data = reinterpret_cast<const uint8_t *>(tgeom_blob.GetData());
        size_t tgeom_data_size = tgeom_blob.GetSize();
        uint8_t *tgeom_data_copy = (uint8_t *)malloc(tgeom_data_size);
        memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
        Temporal *tgeom = reinterpret_cast<Temporal *>(tgeom_data_copy);
        if (!tgeom) {
            free(tgeom_data_copy);
            free(gs);
            throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
        }

        Temporal *ret = tcontains_geo_tgeo(gs, tgeom, restr, at_value);
        free(tgeom);
        free(gs);
        if (!ret) {
            mask.SetInvalid(idx);
            return string_t();
        }
        size_t ret_size = temporal_mem_size(ret);
        string_t stored_data =
            StringVector::AddStringOrBlob(result, reinterpret_cast<const char *>(ret), ret_size);
        free(ret);
        return stored_data;
    };

    if (args.ColumnCount() == 2) {
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
            args.data[0], args.data[1], result, count,
            [&](string_t geometry_blob, string_t tgeom_blob, ValidityMask &mask, idx_t idx) -> string_t {
                return eval(geometry_blob, tgeom_blob, false, false, mask, idx);
            });
    } else if (args.ColumnCount() == 3) {
        TernaryExecutor::ExecuteWithNulls<string_t, string_t, bool, string_t>(
            args.data[0], args.data[1], args.data[2], result, count,
            [&](string_t geometry_blob, string_t tgeom_blob, bool at_value, ValidityMask &mask,
                idx_t idx) -> string_t {
                return eval(geometry_blob, tgeom_blob, true, at_value, mask, idx);
            });
    } else {
        throw InternalException("Tcontains_geo_tgeo: expected 2 or 3 arguments");
    }
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tdisjoint_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    bool at_value = false;
    bool restr = false;
    if (args.ColumnCount() > 2){
        at_value = args.data[2].GetValue(0).GetValue<bool>();
        restr = true;
    }
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t geometry_blob, string_t tgeom_blob, ValidityMask &mask, idx_t idx) -> string_t {
            int32 srid = 0;
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t *>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t *)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal *>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                free(gs);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            Temporal *ret = tdisjoint_geo_tgeo(gs, tgeom, restr, at_value);
            free(tgeom);
            free(gs);
            if (!ret) {
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t stored_data = StringVector::AddStringOrBlob(result, reinterpret_cast<const char *>(ret), ret_size);
            free(ret);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tdisjoint_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result) {
    bool at_value = false;
    bool restr = false;
    if (args.ColumnCount() > 2){
        at_value = args.data[2].GetValue(0).GetValue<bool>();
        restr = true;
    }
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom_blob, string_t geometry_blob, ValidityMask &mask, idx_t idx) -> string_t {
            int32 srid = 0;
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t *>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t *)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal *>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                free(gs);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            Temporal *ret = tdisjoint_tgeo_geo(tgeom, gs, restr, at_value);
            free(tgeom);
            free(gs);
            if (!ret) {
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t stored_data = StringVector::AddStringOrBlob(result, reinterpret_cast<const char *>(ret), ret_size);
            free(ret);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tdisjoint_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    bool at_value = false;
    bool restr = false;
    if (args.ColumnCount() > 2){
        at_value = args.data[2].GetValue(0).GetValue<bool>();
        restr = true;
    }
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom1_blob, string_t tgeom2_blob, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *tgeom1_data = reinterpret_cast<const uint8_t*>(tgeom1_blob.GetData());
            size_t tgeom1_data_size = tgeom1_blob.GetSize();
            uint8_t *tgeom1_data_copy = (uint8_t*)malloc(tgeom1_data_size);
            memcpy(tgeom1_data_copy, tgeom1_data, tgeom1_data_size);
            Temporal *tgeom1 = reinterpret_cast<Temporal*>(tgeom1_data_copy);
            if (!tgeom1) {
                free(tgeom1_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            const uint8_t *tgeom2_data = reinterpret_cast<const uint8_t*>(tgeom2_blob.GetData());
            size_t tgeom2_data_size = tgeom2_blob.GetSize();
            uint8_t *tgeom2_data_copy = (uint8_t*)malloc(tgeom2_data_size);
            memcpy(tgeom2_data_copy, tgeom2_data, tgeom2_data_size);
            Temporal *tgeom2 = reinterpret_cast<Temporal*>(tgeom2_data_copy);
            if (!tgeom2) {
                free(tgeom1);
                free(tgeom2_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            Temporal *ret = tdisjoint_tgeo_tgeo(tgeom1, tgeom2, restr, at_value);
            free(tgeom1);
            free(tgeom2);
            if (!ret) {
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t stored_data = StringVector::AddStringOrBlob(result, reinterpret_cast<const char *>(ret), ret_size);
            free(ret);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tintersects_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    bool at_value = false;
    bool restr = false;
    if (args.ColumnCount() > 2){
        at_value = args.data[2].GetValue(0).GetValue<bool>();
        restr = true; 
    }
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t geometry_blob, string_t tgeom_blob, ValidityMask &mask, idx_t idx) -> string_t {
            int32 srid = 0;
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t *>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t *)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal *>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                free(gs);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            Temporal *ret = tintersects_geo_tgeo(gs, tgeom, restr, at_value);
            free(tgeom);
            free(gs);
            if (!ret) {
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t stored_data = StringVector::AddStringOrBlob(result, reinterpret_cast<const char *>(ret), ret_size);
            free(ret);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tintersects_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result) {
    bool at_value = false;
    bool restr = false;
    if (args.ColumnCount() > 2){
        at_value = args.data[2].GetValue(0).GetValue<bool>();
        restr = true; 
    }
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom_blob, string_t geometry_blob, ValidityMask &mask, idx_t idx) -> string_t {
            int32 srid = 0;
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t *>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t *)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal *>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                free(gs);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            Temporal *ret = tintersects_tgeo_geo(tgeom, gs, restr, at_value);
            free(tgeom);
            free(gs);
            if (!ret) {
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t stored_data = StringVector::AddStringOrBlob(result, reinterpret_cast<const char *>(ret), ret_size);
            free(ret);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tintersects_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    bool at_value = false;
    bool restr = false;
    if (args.ColumnCount() > 2){
        at_value = args.data[2].GetValue(0).GetValue<bool>();
        restr = true; 
    }
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom1_blob, string_t tgeom2_blob, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *tgeom1_data = reinterpret_cast<const uint8_t*>(tgeom1_blob.GetData());
            size_t tgeom1_data_size = tgeom1_blob.GetSize();
            uint8_t *tgeom1_data_copy = (uint8_t*)malloc(tgeom1_data_size);
            memcpy(tgeom1_data_copy, tgeom1_data, tgeom1_data_size);
            Temporal *tgeom1 = reinterpret_cast<Temporal*>(tgeom1_data_copy);
            if (!tgeom1) {
                free(tgeom1_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            const uint8_t *tgeom2_data = reinterpret_cast<const uint8_t*>(tgeom2_blob.GetData());
            size_t tgeom2_data_size = tgeom2_blob.GetSize();
            uint8_t *tgeom2_data_copy = (uint8_t*)malloc(tgeom2_data_size);
            memcpy(tgeom2_data_copy, tgeom2_data, tgeom2_data_size);
            Temporal *tgeom2 = reinterpret_cast<Temporal*>(tgeom2_data_copy);
            if (!tgeom2) {
                free(tgeom1);
                free(tgeom2_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            Temporal *ret = tintersects_tgeo_tgeo(tgeom1, tgeom2, restr, at_value);
            free(tgeom1);
            free(tgeom2);
            if (!ret) {
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t stored_data = StringVector::AddStringOrBlob(result, reinterpret_cast<const char *>(ret), ret_size);
            free(ret);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Ttouches_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    bool at_value = false;
    bool restr = false;
    if (args.ColumnCount() > 2){
        at_value = args.data[2].GetValue(0).GetValue<bool>();
        restr = true; 
    }
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t geometry_blob, string_t tgeom_blob, ValidityMask &mask, idx_t idx) -> string_t {
            int32 srid = 0;
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t *>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t *)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal *>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                free(gs);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            Temporal *ret = ttouches_geo_tgeo(gs, tgeom, restr, at_value);
            free(tgeom);
            free(gs);
            if (!ret) {
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t stored_data = StringVector::AddStringOrBlob(result, reinterpret_cast<const char *>(ret), ret_size);
            free(ret);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Ttouches_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result) {
    bool at_value = false;
    bool restr = false;
    if (args.ColumnCount() > 2){
        at_value = args.data[2].GetValue(0).GetValue<bool>();
        restr = true; 
    }
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom_blob, string_t geometry_blob, ValidityMask &mask, idx_t idx) -> string_t {
            int32 srid = 0;
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t *>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t *)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal *>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                free(gs);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            Temporal *ret = ttouches_tgeo_geo(tgeom, gs, restr, at_value);
            free(tgeom);
            free(gs);
            if (!ret) {
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t stored_data = StringVector::AddStringOrBlob(result, reinterpret_cast<const char *>(ret), ret_size);
            free(ret);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tdwithin_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    bool at_value = false;
    bool restr = false;
    if (args.ColumnCount() > 3) {
        at_value = args.data[3].GetValue(0).GetValue<bool>();
        restr = true;
    }
    TernaryExecutor::ExecuteWithNulls<string_t, string_t, double, string_t>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t tgeom1_blob, string_t tgeom2_blob, double dist, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *tgeom1_data = reinterpret_cast<const uint8_t*>(tgeom1_blob.GetData());
            size_t tgeom1_data_size = tgeom1_blob.GetSize();
            uint8_t *tgeom1_data_copy = (uint8_t*)malloc(tgeom1_data_size);
            memcpy(tgeom1_data_copy, tgeom1_data, tgeom1_data_size);
            Temporal *tgeom1 = reinterpret_cast<Temporal*>(tgeom1_data_copy);
            if (!tgeom1) {
                free(tgeom1_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            const uint8_t *tgeom2_data = reinterpret_cast<const uint8_t*>(tgeom2_blob.GetData());
            size_t tgeom2_data_size = tgeom2_blob.GetSize();
            uint8_t *tgeom2_data_copy = (uint8_t*)malloc(tgeom2_data_size);
            memcpy(tgeom2_data_copy, tgeom2_data, tgeom2_data_size);
            Temporal *tgeom2 = reinterpret_cast<Temporal*>(tgeom2_data_copy);
            if (!tgeom2) {
                free(tgeom1);
                free(tgeom2_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            Temporal *ret = tdwithin_tgeo_tgeo(tgeom1, tgeom2, dist, restr, at_value);
            if (!ret) {
                free(tgeom1);
                free(tgeom2);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t stored_data = StringVector::AddStringOrBlob(result, reinterpret_cast<const char *>(ret), ret_size);
            free(ret);
            free(tgeom1);
            free(tgeom2);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tdwithin_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result) {
    bool at_value = false;
    bool restr = false;
    if (args.ColumnCount() > 3) {
        at_value = args.data[3].GetValue(0).GetValue<bool>();
        restr = true;
    }
    TernaryExecutor::ExecuteWithNulls<string_t, string_t, double, string_t>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t tgeom_blob, string_t geometry_blob, double dist, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t *>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t *)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal *>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            int32 srid = tspatial_srid(tgeom);
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                free(tgeom);
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            Temporal *ret = tdwithin_tgeo_geo(tgeom, gs, dist, restr, at_value);
            free(tgeom);
            free(gs);
            if (!ret) {
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t stored_data = StringVector::AddStringOrBlob(result, reinterpret_cast<const char *>(ret), ret_size);
            free(ret);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tdwithin_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    bool at_value = false;
    bool restr = false;
    if (args.ColumnCount() > 3) {
        at_value = args.data[3].GetValue(0).GetValue<bool>();
        restr = true;
    }
    TernaryExecutor::ExecuteWithNulls<string_t, string_t, double, string_t>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t geometry_blob, string_t tgeom_blob, double dist, ValidityMask &mask, idx_t idx) -> string_t {
            int32 srid = 0;
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
            }

            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t *>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t *)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal *>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                free(gs);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            Temporal *ret = tdwithin_geo_tgeo(gs, tgeom, dist, restr, at_value);
            free(tgeom);
            free(gs);
            if (!ret) {
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t stored_data = StringVector::AddStringOrBlob(result, reinterpret_cast<const char *>(ret), ret_size);
            free(ret);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::ShortestLine_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom1_blob, string_t tgeom2_blob, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *tgeom1_data = reinterpret_cast<const uint8_t*>(tgeom1_blob.GetData());
            size_t tgeom1_data_size = tgeom1_blob.GetSize();
            uint8_t *tgeom1_data_copy = (uint8_t*)malloc(tgeom1_data_size);
            memcpy(tgeom1_data_copy, tgeom1_data, tgeom1_data_size);
            Temporal *tgeom1 = reinterpret_cast<Temporal*>(tgeom1_data_copy);
            if (!tgeom1) {
                free(tgeom1_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            const uint8_t *tgeom2_data = reinterpret_cast<const uint8_t*>(tgeom2_blob.GetData());
            size_t tgeom2_data_size = tgeom2_blob.GetSize();
            uint8_t *tgeom2_data_copy = (uint8_t*)malloc(tgeom2_data_size);
            memcpy(tgeom2_data_copy, tgeom2_data, tgeom2_data_size);
            Temporal *tgeom2 = reinterpret_cast<Temporal*>(tgeom2_data_copy);
            if (!tgeom2) {
                free(tgeom1);
                free(tgeom2_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            GSERIALIZED *ret = shortestline_tgeo_tgeo(tgeom1, tgeom2);
            if (!ret) {
                free(tgeom1);
                free(tgeom2);
                mask.SetInvalid(idx);
                return string_t();
            }
            string_t geometry_blob = GSerializedToGeometry(ret, state, result);
            string_t stored_data = StringVector::AddStringOrBlob(result, geometry_blob);
            free(ret);
            free(tgeom1);
            free(tgeom2);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
 * Operators (workaround as functions)
 ****************************************************/

void TgeompointFunctions::Temporal_overlaps_tgeompoint_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom_blob, string_t stbox_blob) -> bool {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t*)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal*>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            const uint8_t *stbox_data = reinterpret_cast<const uint8_t*>(stbox_blob.GetData());
            size_t stbox_data_size = stbox_blob.GetSize();
            uint8_t *stbox_data_copy = (uint8_t*)malloc(stbox_data_size);
            memcpy(stbox_data_copy, stbox_data, stbox_data_size);
            STBox *stbox = reinterpret_cast<STBox*>(stbox_data_copy);
            if (!stbox) {
                free(tgeom);
                free(stbox_data_copy);
                throw InvalidInputException("Invalid STBOX data: null pointer");
            }
            bool ret = overlaps_tspatial_stbox(tgeom, stbox);
            free(tgeom);
            free(stbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Temporal_overlaps_tgeompoint_tstzspan(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom_blob, string_t span_blob) -> bool {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t*)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal*>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            
            const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
            size_t span_data_size = span_blob.GetSize();
            uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
            memcpy(span_data_copy, span_data, span_data_size);
            Span *span = reinterpret_cast<Span*>(span_data_copy);
            if (!span) {
                free(tgeom);
                free(span_data_copy);
                throw InvalidInputException("Invalid TSTZSPAN data: null pointer");
            }
            bool ret = overlaps_tstzspan_temporal(span, tgeom);
            free(tgeom);
            free(span);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Temporal_contains_tgeompoint_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom_blob, string_t stbox_blob) -> bool {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t*)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal*>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            const uint8_t *stbox_data = reinterpret_cast<const uint8_t*>(stbox_blob.GetData());
            size_t stbox_data_size = stbox_blob.GetSize();
            uint8_t *stbox_data_copy = (uint8_t*)malloc(stbox_data_size);
            memcpy(stbox_data_copy, stbox_data, stbox_data_size);
            STBox *stbox = reinterpret_cast<STBox*>(stbox_data_copy);
            if (!stbox) {
                free(tgeom);
                free(stbox_data_copy);
                throw InvalidInputException("Invalid STBOX data: null pointer");
            }
            bool ret = contains_tspatial_stbox(tgeom, stbox);
            free(tgeom);
            free(stbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
 * Distance functions
 ****************************************************/

void TgeompointFunctions::Tdistance_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom1_blob, string_t tgeom2_blob, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *tgeom1_data = reinterpret_cast<const uint8_t*>(tgeom1_blob.GetData());
            size_t tgeom1_data_size = tgeom1_blob.GetSize();
            uint8_t *tgeom1_data_copy = (uint8_t*)malloc(tgeom1_data_size);
            memcpy(tgeom1_data_copy, tgeom1_data, tgeom1_data_size);
            Temporal *tgeom1 = reinterpret_cast<Temporal*>(tgeom1_data_copy);
            if (!tgeom1) {
                free(tgeom1_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            
            const uint8_t *tgeom2_data = reinterpret_cast<const uint8_t*>(tgeom2_blob.GetData());
            size_t tgeom2_data_size = tgeom2_blob.GetSize();
            uint8_t *tgeom2_data_copy = (uint8_t*)malloc(tgeom2_data_size);
            memcpy(tgeom2_data_copy, tgeom2_data, tgeom2_data_size);
            Temporal *tgeom2 = reinterpret_cast<Temporal*>(tgeom2_data_copy);
            if (!tgeom2) {
                free(tgeom1);
                free(tgeom2_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            Temporal *ret = tdistance_tgeo_tgeo(tgeom1, tgeom2);
            if (!ret) {
                free(tgeom1);
                free(tgeom2);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            uint8_t *ret_data = (uint8_t*)malloc(ret_size);
            memcpy(ret_data, ret, ret_size);
            string_t ret_string(reinterpret_cast<const char*>(ret_data), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_string);
            free(ret_data);
            free(ret);
            free(tgeom1);
            free(tgeom2);
            return stored_data;
        }
    );
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

void TgeompointFunctions::Tpoint_as_mvt_geom(DataChunk &args, ExpressionState &state, Vector &result) {
    const idx_t n = args.size();
    const idx_t ncols = args.ColumnCount();

    // GEOS context (RAII)
    struct GeosCtx {
        GEOSContextHandle_t ctx;
        GeosCtx() : ctx(GEOS_init_r()) {
            GEOSContext_setErrorMessageHandler_r(ctx,
                [](const char *m, void *) { throw InvalidInputException(m); }, nullptr);
        }
        ~GeosCtx() { GEOS_finish_r(ctx); }
    } geos_ctx;

    // Scratch vector for intermediate GEOMETRY blobs (GSERIALIZED → DuckDB format)
    Vector scratch(GeoTypes::GEOMETRY(), n);
    ArenaAllocator arena(BufferAllocator::Get(state.GetContext()));

    // Unified formats for tgeompoint arg and BOX_2D struct fields
    UnifiedVectorFormat tgeo_fmt, bbox_fmt;
    args.data[0].ToUnifiedFormat(n, tgeo_fmt);
    args.data[1].ToUnifiedFormat(n, bbox_fmt);
    const auto tgeo_data = UnifiedVectorFormat::GetData<string_t>(tgeo_fmt);

    const auto &bbox_parts = StructVector::GetEntries(args.data[1]);
    UnifiedVectorFormat minx_fmt, miny_fmt, maxx_fmt, maxy_fmt;
    bbox_parts[0]->ToUnifiedFormat(n, minx_fmt);
    bbox_parts[1]->ToUnifiedFormat(n, miny_fmt);
    bbox_parts[2]->ToUnifiedFormat(n, maxx_fmt);
    bbox_parts[3]->ToUnifiedFormat(n, maxy_fmt);
    const auto minx_d = UnifiedVectorFormat::GetData<double>(minx_fmt);
    const auto miny_d = UnifiedVectorFormat::GetData<double>(miny_fmt);
    const auto maxx_d = UnifiedVectorFormat::GetData<double>(maxx_fmt);
    const auto maxy_d = UnifiedVectorFormat::GetData<double>(maxy_fmt);

    // Optional extent / buffer / clip_geom args
    UnifiedVectorFormat extent_fmt, buffer_fmt, clip_fmt;
    const int64_t *extent_d = nullptr;
    const int64_t *buffer_d = nullptr;
    const bool *clip_d = nullptr;
    if (ncols >= 3) {
        args.data[2].ToUnifiedFormat(n, extent_fmt);
        extent_d = UnifiedVectorFormat::GetData<int64_t>(extent_fmt);
    }
    if (ncols >= 4) {
        args.data[3].ToUnifiedFormat(n, buffer_fmt);
        buffer_d = UnifiedVectorFormat::GetData<int64_t>(buffer_fmt);
    }
    if (ncols >= 5) {
        args.data[4].ToUnifiedFormat(n, clip_fmt);
        clip_d = UnifiedVectorFormat::GetData<bool>(clip_fmt);
    }

    const auto res_data = FlatVector::GetData<string_t>(result);

    for (idx_t i = 0; i < n; i++) {
        const auto geo_idx = tgeo_fmt.sel->get_index(i);
        const auto box_idx = bbox_fmt.sel->get_index(i);

        if (!tgeo_fmt.validity.RowIsValid(geo_idx) || !bbox_fmt.validity.RowIsValid(box_idx)) {
            FlatVector::SetNull(result, i, true);
            continue;
        }

        // Per-row parameters (with defaults matching ST_AsMVTGeom)
        int32_t extent = 4096, buffer = 256;
        bool clip = true, null_param = false;
        if (ncols >= 3) {
            const auto eidx = extent_fmt.sel->get_index(i);
            if (!extent_fmt.validity.RowIsValid(eidx)) null_param = true;
            else extent = static_cast<int32_t>(extent_d[eidx]);
        }
        if (!null_param && ncols >= 4) {
            const auto bidx = buffer_fmt.sel->get_index(i);
            if (!buffer_fmt.validity.RowIsValid(bidx)) null_param = true;
            else buffer = static_cast<int32_t>(buffer_d[bidx]);
        }
        if (!null_param && ncols >= 5) {
            const auto cidx = clip_fmt.sel->get_index(i);
            if (!clip_fmt.validity.RowIsValid(cidx)) null_param = true;
            else clip = clip_d[cidx];
        }
        if (null_param) {
            FlatVector::SetNull(result, i, true);
            continue;
        }

        // Extract trajectory from tgeompoint
        const auto &blob = tgeo_data[geo_idx];
        const size_t bsz = blob.GetSize();
        uint8_t *tc = static_cast<uint8_t*>(malloc(bsz));
        memcpy(tc, blob.GetData(), bsz);
        Temporal *temp = reinterpret_cast<Temporal*>(tc);
        GSERIALIZED *gs = tpoint_trajectory(temp, false);
        free(temp);
        if (!gs) {
            FlatVector::SetNull(result, i, true);
            continue;
        }

        // GSERIALIZED → DuckDB GEOMETRY blob → GEOSGeometry
        string_t geom_blob = GSerializedToGeometry(gs, arena, scratch);
        free(gs);
        GEOSGeometry *raw = GeosSerde::Deserialize(geos_ctx.ctx, geom_blob.GetData(), geom_blob.GetSize());
        if (!raw) {
            FlatVector::SetNull(result, i, true);
            continue;
        }
        GeosGeometry geom(geos_ctx.ctx, raw);

        // Tile bounds from BOX_2D
        const double minx = minx_d[minx_fmt.sel->get_index(box_idx)];
        const double miny = miny_d[miny_fmt.sel->get_index(box_idx)];
        const double maxx = maxx_d[maxx_fmt.sel->get_index(box_idx)];
        const double maxy = maxy_d[maxy_fmt.sel->get_index(box_idx)];
        const double tw = maxx - minx, th = maxy - miny;
        if (tw <= 0 || th <= 0)
            throw InvalidInputException("asMVTGeom: tile width and height must be positive");

        // MVT affine transform: scale to extent, Y-axis flip, translate to origin
        //   x' = scale_x * (x - minx)
        //   y' = scale_y * (y - maxy)   (scale_y is negative → flip)
        const double sx = extent / tw;
        const double sy = -(extent / th);
        const double aff[6] = { sx, 0.0, 0.0, sy, -minx * sx, -maxy * sy };
        auto xf = geom.get_transformed(aff);
        auto sn = xf.get_gridded(1.0);

        if (!clip) {
            sn.orient_polygons(false);
            const auto rsz = GeosSerde::GetRequiredSize(geos_ctx.ctx, sn.get_raw());
            auto out = StringVector::EmptyString(result, rsz);
            GeosSerde::Serialize(geos_ctx.ctx, sn.get_raw(), out.GetDataWriteable(), rsz);
            out.Finalize();
            res_data[i] = out;
            continue;
        }

        // Clip to tile+buffer in MVT coordinate space
        auto cl = sn.get_clipped(-buffer, -buffer, extent + buffer, extent + buffer);
        if (cl.is_empty()) {
            FlatVector::SetNull(result, i, true);
            continue;
        }
        auto cn = cl.get_gridded(1.0);
        cn.orient_polygons(false);
        const auto rsz = GeosSerde::GetRequiredSize(geos_ctx.ctx, cn.get_raw());
        auto out = StringVector::EmptyString(result, rsz);
        GeosSerde::Serialize(geos_ctx.ctx, cn.get_raw(), out.GetDataWriteable(), rsz);
        out.Finalize();
        res_data[i] = out;
    }
}

} // namespace duckdb