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

/* The boundary between MEOS's raster and duckdb-raster (raster_boundary.hpp),
 * written over the MEOS functions alone: a raster is read through its
 * accessors and the bytes of its bands (raster_band_pixels), and built from
 * the bytes of each band and its georeferencing (raster_make), the bands
 * after the first added with raster_add_band_raster. The pixels move as bytes
 * and never as doubles, so every value crosses exactly. The datacube codec is
 * raster_datacube.cpp; the metadata JSON is the shape RT_Read writes
 * (raster_utils.cpp of duckdb-raster), parsed with the yyjson DuckDB
 * vendors. */

#include "raster/raster_boundary.hpp"
#include "raster/raster.hpp"
#include "raster/raster_datacube.hpp"
#include "mobilityduck/meos_exec_serial.hpp"

#include "duckdb/common/types/value.hpp"
#include "duckdb/common/vector_operations/unary_executor.hpp"
#include "duckdb/function/cast/default_casts.hpp"

#include "yyjson.hpp"

extern "C" {
#include <meos.h>
#include <meos_geo.h>
#include <meos_raster.h>
}

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace duckdb {

using namespace duckdb_yyjson; // NOLINT

LogicalType RasterBoundary::RtDatacube() {
	auto type = LogicalType(LogicalTypeId::BLOB);
	type.SetAlias("RT_DATACUBE");
	return type;
}

LogicalType RasterBoundary::RtStats() {
	auto type = LogicalType::STRUCT({{"minimum", LogicalType(LogicalTypeId::DOUBLE)},
	                                 {"maximum", LogicalType(LogicalTypeId::DOUBLE)},
	                                 {"sum", LogicalType(LogicalTypeId::DOUBLE)},
	                                 {"mean", LogicalType(LogicalTypeId::DOUBLE)},
	                                 {"stddev", LogicalType(LogicalTypeId::DOUBLE)},
	                                 {"valid_count", LogicalType(LogicalTypeId::BIGINT)},
	                                 {"nodata_count", LogicalType(LogicalTypeId::BIGINT)}});
	type.SetAlias("RT_STATS");
	return type;
}

LogicalType RasterBoundary::SummaryStats() {
	return LogicalType::STRUCT({{"count", LogicalType(LogicalTypeId::BIGINT)},
	                            {"sum", LogicalType(LogicalTypeId::DOUBLE)},
	                            {"mean", LogicalType(LogicalTypeId::DOUBLE)},
	                            {"stddev", LogicalType(LogicalTypeId::DOUBLE)},
	                            {"min", LogicalType(LogicalTypeId::DOUBLE)},
	                            {"max", LogicalType(LogicalTypeId::DOUBLE)}});
}

namespace {

struct MeosFree {
	void operator()(void *p) const {
		free(p);
	}
};
using RasterPtr = std::unique_ptr<Raster, MeosFree>;
using BytesPtr = std::unique_ptr<uint8_t, MeosFree>;
using CharsPtr = std::unique_ptr<char, MeosFree>;

RasterPtr ReadRaster(string_t blob) {
	Raster *rast = raster_from_wkb(const_data_ptr_cast(blob.GetData()), blob.GetSize());
	if (!rast) {
		throw InvalidInputException("raster: invalid raster serialisation");
	}
	return RasterPtr(rast);
}

string_t WriteRaster(Vector &result, const Raster *rast) {
	size_t size = 0;
	BytesPtr wkb(raster_as_wkb(rast, 0, &size));
	if (!wkb) {
		throw InvalidInputException("raster: cannot serialise the raster");
	}
	return StringVector::AddStringOrBlob(result, const_char_ptr_cast(wkb.get()), size);
}

/* The pixel types of a MEOS raster band and of a datacube, as PostGIS maps its
 * pixel types to GDAL's (rt_util_pixtype_to_gdal_datatype): the name is the
 * one raster_band_pixel_type answers, which is uint8 for 1BB, 2BUI and 4BUI as
 * their bytes are; the datacube data type, the PostGIS name a datacube band
 * builds, and GDAL's name and code, which the metadata states. */
struct PixelType {
	const char *meos;
	DatacubeType cube;
	const char *postgis;
	const char *gdal_name;
	int gdal_code;
};

const PixelType PIXEL_TYPES[] = {
    {"uint8", DatacubeType::UINT8, "8BUI", "Byte", 1},
    {"int8", DatacubeType::INT8, "8BSI", "Int8", 14},
    {"uint16", DatacubeType::UINT16, "16BUI", "UInt16", 2},
    {"int16", DatacubeType::INT16, "16BSI", "Int16", 3},
    {"uint32", DatacubeType::UINT32, "32BUI", "UInt32", 4},
    {"int32", DatacubeType::INT32, "32BSI", "Int32", 5},
    {"float32", DatacubeType::FLOAT, "32BF", "Float32", 6},
    {"float64", DatacubeType::DOUBLE, "64BF", "Float64", 7},
};

const PixelType *PixelTypeOfCube(DatacubeType type) {
	for (const auto &row : PIXEL_TYPES) {
		if (row.cube == type) {
			return &row;
		}
	}
	return nullptr;
}

//! The pixel type and nodata value of a band of a MEOS raster
struct Band {
	const PixelType *type;
	bool has_nodata;
	double nodata;
};

Band ReadBand(const Raster *rast, int band) {
	CharsPtr name(raster_band_pixel_type(rast, band));
	if (!name) {
		throw InvalidInputException("raster: cannot read the pixel type of band %d", band);
	}
	Band result {nullptr, false, 0.0};
	for (const auto &row : PIXEL_TYPES) {
		if (std::strcmp(row.meos, name.get()) == 0) {
			result.type = &row;
		}
	}
	if (!result.type) {
		throw InvalidInputException("raster: band %d of pixel type %s has no datacube data type", band, name.get());
	}
	result.has_nodata = raster_band_nodata_value(rast, band, &result.nodata);
	if (result.has_nodata && !std::isfinite(result.nodata)) {
		throw InvalidInputException("raster: band %d states the nodata value %s, which a datacube and its metadata "
		                            "cannot state",
		                            band, std::isnan(result.nodata) ? "NaN" : (result.nodata > 0 ? "+inf" : "-inf"));
	}
	return result;
}

//! Whether a nodata value of a datacube is a value of its data type
bool NoDataFitsType(DatacubeType type, double v) {
	if (type == DatacubeType::DOUBLE) {
		return true;
	}
	if (type == DatacubeType::FLOAT) {
		return std::fabs(v) <= static_cast<double>(std::numeric_limits<float>::max()) &&
		       static_cast<double>(static_cast<float>(v)) == v;
	}
	if (std::trunc(v) != v) {
		return false;
	}
	switch (type) {
	case DatacubeType::UINT8:
		return v >= 0 && v <= 255;
	case DatacubeType::INT8:
		return v >= -128 && v <= 127;
	case DatacubeType::UINT16:
		return v >= 0 && v <= 65535;
	case DatacubeType::INT16:
		return v >= -32768 && v <= 32767;
	case DatacubeType::UINT32:
		return v >= 0 && v <= 4294967295.0;
	case DatacubeType::INT32:
		return v >= -2147483648.0 && v <= 2147483647.0;
	default:
		return false;
	}
}

/* =====================================================================
 * The metadata of a tile
 * ===================================================================== */

//! What the metadata of a tile states that a raster is built from
struct TileGrid {
	double transform[6];
	int64_t blocksize_x;
	int64_t blocksize_y;
	int32_t srid;
};

[[noreturn]] void ThrowMetadata(const std::string &what) {
	throw InvalidInputException("raster: the tile metadata %s", what);
}

int64_t ReadBlocksize(yyjson_val *root, const char *key) {
	yyjson_val *val = yyjson_obj_get(root, key);
	if (!val || !yyjson_is_int(val)) {
		ThrowMetadata(std::string("states no integer '") + key + "'");
	}
	int64_t v;
	if (yyjson_is_uint(val)) {
		const uint64_t u = yyjson_get_uint(val);
		v = u > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()) ? -1 : static_cast<int64_t>(u);
	} else {
		v = yyjson_get_sint(val);
	}
	if (v <= 0 || v > std::numeric_limits<int32_t>::max()) {
		ThrowMetadata(std::string("states a '") + key + "' that is not a positive 32-bit integer");
	}
	return v;
}

/* The reference system of a tile, which RT_Read writes as EPSG:<code> where
 * GDAL identifies the code, as the WKT of the system where it does not, and
 * as the empty string for a raster without one. A raster reads EPSG:<code> as
 * its SRID and the empty string as the SRID 0 of no system; the WKT names no
 * SRID, so it raises an error rather than be guessed at. */
int32_t ReadCrs(yyjson_val *root) {
	yyjson_val *val = yyjson_obj_get(root, "crs");
	if (!val || !yyjson_is_str(val)) {
		ThrowMetadata("states no string 'crs'");
	}
	const char *crs = yyjson_get_str(val);
	const size_t len = yyjson_get_len(val);
	if (len == 0) {
		return 0;
	}
	constexpr size_t PREFIX = 5; /* EPSG: */
	if (len <= PREFIX || std::strncmp(crs, "EPSG:", PREFIX) != 0) {
		ThrowMetadata(std::string("states the reference system '") + std::string(crs, len) +
		              "', where a raster reads EPSG:<code> or none");
	}
	int64_t code = 0;
	for (size_t i = PREFIX; i < len; i++) {
		if (crs[i] < '0' || crs[i] > '9' || code > std::numeric_limits<int32_t>::max() / 10) {
			ThrowMetadata(std::string("states the reference system '") + std::string(crs, len) +
			              "', whose code is no positive 32-bit integer");
		}
		code = code * 10 + (crs[i] - '0');
	}
	if (code <= 0 || code > std::numeric_limits<int32_t>::max()) {
		ThrowMetadata(std::string("states the reference system '") + std::string(crs, len) +
		              "', whose code is no positive 32-bit integer");
	}
	return static_cast<int32_t>(code);
}

TileGrid ReadTileGrid(string_t metadata) {
	/* RT_Read writes the nodata value of a band without one as -inf, so its
	 * metadata is read allowing the non-finite numbers */
	std::unique_ptr<yyjson_doc, void (*)(yyjson_doc *)> doc(
	    yyjson_read(metadata.GetData(), metadata.GetSize(), YYJSON_READ_ALLOW_INF_AND_NAN), yyjson_doc_free);
	if (!doc) {
		ThrowMetadata("is not JSON");
	}
	yyjson_val *root = yyjson_doc_get_root(doc.get());
	if (!yyjson_is_obj(root)) {
		ThrowMetadata("is not a JSON object");
	}
	TileGrid grid;
	yyjson_val *transform = yyjson_obj_get(root, "transform");
	if (!transform || !yyjson_is_arr(transform) || yyjson_arr_size(transform) != 6) {
		ThrowMetadata("states no 'transform' of 6 numbers");
	}
	for (size_t i = 0; i < 6; i++) {
		yyjson_val *v = yyjson_arr_get(transform, i);
		if (!yyjson_is_num(v) || !std::isfinite(yyjson_get_num(v))) {
			ThrowMetadata("states a 'transform' whose coefficient " + std::to_string(i + 1) +
			              " is not a finite number");
		}
		grid.transform[i] = yyjson_get_num(v);
	}
	grid.blocksize_x = ReadBlocksize(root, "blocksize_x");
	grid.blocksize_y = ReadBlocksize(root, "blocksize_y");
	grid.srid = ReadCrs(root);
	return grid;
}

/* =====================================================================
 * raster(datacube, tile_x, tile_y, metadata)
 * ===================================================================== */

/* The world coordinates of a pixel corner under the six GDAL coefficients,
 * as duckdb-raster computes them (RasterUtils::RasterCoordToWorldCoord). A
 * zero offset adds no term, so the corner of tile (0, 0) is the coefficient
 * itself, its sign of zero included. */
void PixelToWorld(const double gt[6], int64_t col, int64_t row, double &x, double &y) {
	x = gt[0];
	y = gt[3];
	if (col != 0) {
		x += gt[1] * static_cast<double>(col);
		y += gt[4] * static_cast<double>(col);
	}
	if (row != 0) {
		x += gt[2] * static_cast<double>(row);
		y += gt[5] * static_cast<double>(row);
	}
}

string_t RasterFromTile(Vector &result, string_t cube, int32_t tile_x, int32_t tile_y, string_t metadata) {
	const uint8_t *data = const_data_ptr_cast(cube.GetData());
	const size_t size = cube.GetSize();
	const DatacubeHeader header = ReadDatacubeHeader(data, size);
	const PixelType *type = PixelTypeOfCube(header.type);
	if (!type) {
		throw InvalidInputException("raster: a datacube of %s has no MEOS pixel type",
		                            DatacubeTypeName(header.type));
	}
	/* rt_core states the width, the height and the number of bands of a
	 * raster as 16-bit unsigned integers */
	constexpr int32_t RT_MAX = std::numeric_limits<uint16_t>::max();
	if (header.cols > RT_MAX || header.rows > RT_MAX || header.bands > RT_MAX) {
		throw InvalidInputException("raster: a datacube of %d bands of %d x %d pixels exceeds the %d bands, columns "
		                            "and rows of a raster",
		                            header.bands, header.cols, header.rows, RT_MAX);
	}
	/* A DuckDB value holds at most 2^32 - 1 bytes, so pixels beyond that make
	 * no raster value: refused before a buffer of their size is allocated */
	if (header.data_size > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
		throw InvalidInputException("raster: a datacube of %d bands of %d x %d pixels of %s exceeds the size of a "
		                            "raster value",
		                            header.bands, header.cols, header.rows, DatacubeTypeName(header.type));
	}
	const bool has_nodata = !DatacubeNoDataIsNone(header.no_data);
	if (has_nodata && !NoDataFitsType(header.type, header.no_data)) {
		throw InvalidInputException("raster: the datacube nodata value %s is no value of its data type %s",
		                            Value::DOUBLE(header.no_data).ToString(), DatacubeTypeName(header.type));
	}
	if (tile_x < 0 || tile_y < 0) {
		throw InvalidInputException("raster: the tile (%d, %d) has a negative coordinate", tile_x, tile_y);
	}
	const TileGrid grid = ReadTileGrid(metadata);
	const int64_t offset_x = static_cast<int64_t>(tile_x) * grid.blocksize_x;
	const int64_t offset_y = static_cast<int64_t>(tile_y) * grid.blocksize_y;
	if (offset_x > std::numeric_limits<int32_t>::max() || offset_y > std::numeric_limits<int32_t>::max()) {
		throw InvalidInputException("raster: the tile (%d, %d) of %lld x %lld pixels lies beyond the pixel offsets of "
		                            "a raster",
		                            tile_x, tile_y, static_cast<long long>(grid.blocksize_x),
		                            static_cast<long long>(grid.blocksize_y));
	}
	double ulx, uly;
	PixelToWorld(grid.transform, offset_x, offset_y, ulx, uly);

	std::vector<uint8_t> buffer;
	const uint8_t *pixels = ReadDatacubePixels(data, size, header, buffer);
	const size_t band_size = static_cast<size_t>(header.data_size) / static_cast<size_t>(header.bands);
	const double nodata = has_nodata ? header.no_data : 0.0;
	RasterPtr rast;
	for (int32_t b = 0; b < header.bands; b++) {
		RasterPtr band(raster_make(header.cols, header.rows, ulx, uly, grid.transform[1], grid.transform[5],
		                           grid.transform[2], grid.transform[4], grid.srid, type->postgis, nodata,
		                           has_nodata, pixels + static_cast<size_t>(b) * band_size, band_size));
		if (!band) {
			throw InvalidInputException("raster: cannot build band %d of the datacube", b + 1);
		}
		if (!rast) {
			rast = std::move(band);
			continue;
		}
		RasterPtr added(raster_add_band_raster(rast.get(), band.get(), 1, 0));
		if (!added) {
			throw InvalidInputException("raster: cannot add band %d of the datacube", b + 1);
		}
		rast = std::move(added);
	}
	return WriteRaster(result, rast.get());
}

void RasterFromDatacubeFunction(DataChunk &args, ExpressionState &, Vector &result) {
	const idx_t count = args.size();
	UnifiedVectorFormat cube_f, x_f, y_f, meta_f;
	args.data[0].ToUnifiedFormat(count, cube_f);
	args.data[1].ToUnifiedFormat(count, x_f);
	args.data[2].ToUnifiedFormat(count, y_f);
	args.data[3].ToUnifiedFormat(count, meta_f);
	auto cubes = UnifiedVectorFormat::GetData<string_t>(cube_f);
	auto xs = UnifiedVectorFormat::GetData<int32_t>(x_f);
	auto ys = UnifiedVectorFormat::GetData<int32_t>(y_f);
	auto metas = UnifiedVectorFormat::GetData<string_t>(meta_f);
	result.SetVectorType(VectorType::FLAT_VECTOR);
	auto out = FlatVector::GetData<string_t>(result);
	auto &validity = FlatVector::Validity(result);
	for (idx_t i = 0; i < count; i++) {
		const idx_t ic = cube_f.sel->get_index(i), ix = x_f.sel->get_index(i), iy = y_f.sel->get_index(i),
		            im = meta_f.sel->get_index(i);
		if (!cube_f.validity.RowIsValid(ic) || !x_f.validity.RowIsValid(ix) || !y_f.validity.RowIsValid(iy) ||
		    !meta_f.validity.RowIsValid(im)) {
			validity.SetInvalid(i);
			continue;
		}
		out[i] = RasterFromTile(result, cubes[ic], xs[ix], ys[iy], metas[im]);
	}
	if (args.AllConstant()) {
		result.SetVectorType(VectorType::CONSTANT_VECTOR);
	}
}

/* =====================================================================
 * CAST(raster AS RT_DATACUBE)
 * ===================================================================== */

string_t DatacubeOfRaster(Vector &result, string_t blob) {
	RasterPtr rast = ReadRaster(blob);
	const int nbands = raster_num_bands(rast.get());
	const int width = raster_width(rast.get());
	const int height = raster_height(rast.get());
	if (nbands <= 0 || width <= 0 || height <= 0) {
		throw InvalidInputException("raster: a raster of %d bands of %d x %d pixels has no datacube, which holds at "
		                            "least one pixel",
		                            nbands, width, height);
	}
	const Band first = ReadBand(rast.get(), 1);
	std::string pixels;
	for (int b = 1; b <= nbands; b++) {
		const Band band = b == 1 ? first : ReadBand(rast.get(), b);
		if (band.type != first.type) {
			throw InvalidInputException("raster: band %d is of pixel type %s and band 1 of %s, where a datacube "
			                            "holds one data type",
			                            b, band.type->meos, first.type->meos);
		}
		if (band.has_nodata != first.has_nodata || (band.has_nodata && band.nodata != first.nodata)) {
			throw InvalidInputException("raster: band %d states a nodata value other than band 1's, where a datacube "
			                            "holds one",
			                            b);
		}
		size_t size = 0;
		BytesPtr bytes(raster_band_pixels(rast.get(), b, &size));
		if (!bytes) {
			throw InvalidInputException("raster: cannot read the pixels of band %d", b);
		}
		pixels.append(const_char_ptr_cast(bytes.get()), size);
	}
	DatacubeHeader header;
	header.format = DatacubeFormat::RAW;
	header.type = first.type->cube;
	header.bands = nbands;
	header.cols = width;
	header.rows = height;
	header.no_data = first.has_nodata ? first.nodata : -std::numeric_limits<double>::infinity();
	header.data_size = pixels.size();
	const std::string cube = WriteDatacube(header, const_data_ptr_cast(pixels.data()), pixels.size());
	return StringVector::AddStringOrBlob(result, cube);
}

bool RasterToDatacubeCast(Vector &source, Vector &result, idx_t count, CastParameters &) {
	UnaryExecutor::Execute<string_t, string_t>(source, result, count,
	                                           [&](string_t blob) { return DatacubeOfRaster(result, blob); });
	return true;
}

/* =====================================================================
 * rasterMetadata(raster)
 * ===================================================================== */

std::string JsonNumber(double v) {
	return Value::DOUBLE(v).ToString();
}

/* The metadata RT_Read writes of a tile (raster_utils.cpp of duckdb-raster),
 * for the raster as the one tile of the datacube its cast writes: its
 * reference system, its six GDAL coefficients, its bounds and footprint, its
 * size as its block size, and per band its type and nodata value. MEOS states
 * no band description, color interpretation, scale, offset or unit, which are
 * written empty, undefined (0) and null. */
std::string MetadataOfRaster(const Raster *rast) {
	const int width = raster_width(rast);
	const int height = raster_height(rast);
	const int nbands = raster_num_bands(rast);
	const int32_t srid = raster_srid(rast);
	if (srid < 0) {
		throw InvalidInputException("raster: the SRID %d names no reference system", srid);
	}
	const double gt[6] = {raster_upper_left_x(rast), raster_scale_x(rast), raster_skew_x(rast),
	                      raster_upper_left_y(rast), raster_skew_y(rast),  raster_scale_y(rast)};
	for (double v : gt) {
		if (!std::isfinite(v)) {
			throw InvalidInputException("raster: a georeferencing coefficient is not finite");
		}
	}
	double xs[4], ys[4];
	PixelToWorld(gt, 0, 0, xs[0], ys[0]);
	PixelToWorld(gt, width, 0, xs[1], ys[1]);
	PixelToWorld(gt, width, height, xs[2], ys[2]);
	PixelToWorld(gt, 0, height, xs[3], ys[3]);
	double xmin = xs[0], xmax = xs[0], ymin = ys[0], ymax = ys[0];
	for (int i = 1; i < 4; i++) {
		xmin = MinValue(xmin, xs[i]);
		xmax = MaxValue(xmax, xs[i]);
		ymin = MinValue(ymin, ys[i]);
		ymax = MaxValue(ymax, ys[i]);
	}
	std::string s;
	s += "{\"file_format\": \"raster\", \"version\": \"0.1.0\", \"data_format\": \"RAW\", \"datacube\": true, ";
	s += "\"crs\": \"" + (srid == 0 ? std::string() : "EPSG:" + std::to_string(srid)) + "\", ";
	s += "\"transform\": [";
	for (int i = 0; i < 6; i++) {
		s += (i ? ", " : "") + JsonNumber(gt[i]);
	}
	s += "], \"bounds\": [" + JsonNumber(xmin) + ", " + JsonNumber(ymin) + ", " + JsonNumber(xmax) + ", " +
	     JsonNumber(ymax) + "], ";
	s += "\"geometry\": \"POLYGON((";
	for (int i = 0; i <= 4; i++) {
		s += (i ? ", " : "") + JsonNumber(xs[i % 4]) + " " + JsonNumber(ys[i % 4]);
	}
	s += "))\", ";
	s += "\"width\": " + std::to_string(width) + ", \"height\": " + std::to_string(height) + ", ";
	s += "\"blocksize_x\": " + std::to_string(width) + ", \"blocksize_y\": " + std::to_string(height) + ", ";
	s += "\"band_count\": " + std::to_string(nbands) + ", \"bands\": [";
	for (int b = 1; b <= nbands; b++) {
		const Band band = ReadBand(rast, b);
		s += (b > 1 ? ", " : "");
		s += "{\"name\": \"databand_" + std::to_string(b) + "\", \"description\": \"\", ";
		s += "\"type_name\": \"" + std::string(band.type->gdal_name) + "\", ";
		s += "\"data_type\": " + std::to_string(band.type->gdal_code) + ", ";
		s += "\"data_size\": " + std::to_string(DatacubeTypeSize(band.type->cube)) + ", ";
		s += "\"width\": " + std::to_string(width) + ", \"height\": " + std::to_string(height) + ", ";
		s += "\"colorinterp\": 0, ";
		s += "\"nodata\": " + (band.has_nodata ? JsonNumber(band.nodata) : std::string("null")) + ", ";
		s += "\"scale\": null, \"offset\": null, \"unit\": \"\"}";
	}
	s += "]}";
	return s;
}

void RasterMetadataFunction(DataChunk &args, ExpressionState &, Vector &result) {
	UnaryExecutor::Execute<string_t, string_t>(args.data[0], result, args.size(), [&](string_t blob) {
		RasterPtr rast = ReadRaster(blob);
		return StringVector::AddString(result, MetadataOfRaster(rast.get()));
	});
}

/* =====================================================================
 * summarystats <-> RT_STATS
 *
 * Field to field, each child vector referenced as DuckDB's struct cast
 * references a child of the same type (StructToStructCast): count is
 * valid_count, min and max are minimum and maximum, and nodata_count, which
 * summarystats does not state, is NULL one way and left out the other.
 * ===================================================================== */

void StructFieldCast(Vector &source, Vector &result, idx_t count, const idx_t (&from)[7], idx_t nfields) {
	auto &src = StructVector::GetEntries(source);
	auto &dst = StructVector::GetEntries(result);
	for (idx_t i = 0; i < dst.size(); i++) {
		if (i < nfields) {
			dst[i]->Reference(*src[from[i]]);
		} else {
			dst[i]->SetVectorType(VectorType::CONSTANT_VECTOR);
			ConstantVector::SetNull(*dst[i], true);
		}
	}
	if (source.GetVectorType() == VectorType::CONSTANT_VECTOR) {
		result.SetVectorType(VectorType::CONSTANT_VECTOR);
		ConstantVector::SetNull(result, ConstantVector::IsNull(source));
		return;
	}
	source.Flatten(count);
	FlatVector::Validity(result) = FlatVector::Validity(source);
}

bool SummaryStatsToRtStatsCast(Vector &source, Vector &result, idx_t count, CastParameters &) {
	/* minimum, maximum, sum, mean, stddev, valid_count from min, max, sum,
	 * mean, stddev, count; nodata_count NULL */
	static const idx_t FROM[7] = {4, 5, 1, 2, 3, 0, 0};
	StructFieldCast(source, result, count, FROM, 6);
	return true;
}

bool RtStatsToSummaryStatsCast(Vector &source, Vector &result, idx_t count, CastParameters &) {
	/* count, sum, mean, stddev, min, max from valid_count, sum, mean, stddev,
	 * minimum, maximum */
	static const idx_t FROM[7] = {5, 2, 3, 4, 0, 1, 0};
	StructFieldCast(source, result, count, FROM, 6);
	return true;
}

} // namespace

void RasterBoundary::Register(ExtensionLoader &loader) {
	/* The name summarystats, under which a query casts to the row summaryStats
	 * answers; the type is that row itself, unaliased, so that the cast's
	 * result reaches the summarystats accessors as summaryStats' does */
	loader.RegisterType("summarystats", SummaryStats());

	RegisterSerializedScalarFunction(
	    loader, ScalarFunction("raster",
	                           {LogicalType::BLOB, LogicalType::INTEGER, LogicalType::INTEGER, LogicalType::JSON()},
	                           RasterTypes::raster(), RasterFromDatacubeFunction));
	RegisterSerializedScalarFunction(
	    loader, ScalarFunction("rasterMetadata", {RasterTypes::raster()}, LogicalType::JSON(), RasterMetadataFunction));

	RegisterMeosCastFunction(loader, RasterTypes::raster(), RtDatacube(), RasterToDatacubeCast);
	loader.RegisterCastFunction(SummaryStats(), RtStats(), SummaryStatsToRtStatsCast, -1);
	loader.RegisterCastFunction(RtStats(), SummaryStats(), RtStatsToSummaryStatsCast, -1);
}

} // namespace duckdb
