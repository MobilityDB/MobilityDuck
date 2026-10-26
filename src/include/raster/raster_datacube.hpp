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

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace duckdb {

/* The datacube of DuckDB's raster extension (duckdb-raster, the RT_DATACUBE
 * type): a header of 24 bytes followed by the pixels of its bands. The header
 * is, little-endian, the magic code 0x5253 (uint16), the data format (uint8),
 * the data type (uint8), the number of bands, of columns and of rows (int32
 * each) and the nodata value (double, -infinity where the cube states none).
 * The pixels are band after band, each band row by row, each pixel in the
 * little-endian bytes of the data type; the data format compresses them as a
 * whole. The codec reads every data format the extension writes and writes
 * RAW; it is MobilityDuck's, as the format is the extension's own. */

//! The data format codes of a datacube header
enum class DatacubeFormat : uint8_t { RAW = 0, SNAPPY = 1, GZIP = 2, ZSTD = 3, LZ4 = 4 };

//! The data type codes of a datacube header
enum class DatacubeType : uint8_t {
	UINT8 = 0,
	INT8 = 1,
	UINT16 = 2,
	INT16 = 3,
	UINT32 = 4,
	INT32 = 5,
	UINT64 = 6,
	INT64 = 7,
	FLOAT = 8,
	DOUBLE = 9
};

//! The number of bytes of a datacube header
constexpr size_t DATACUBE_HEADER_SIZE = 24;

//! The magic code opening a datacube header
constexpr uint16_t DATACUBE_MAGIC = 0x5253;

//! The header of a datacube, and the number of bytes its pixels take
struct DatacubeHeader {
	DatacubeFormat format;
	DatacubeType type;
	int32_t bands;
	int32_t cols;
	int32_t rows;
	double no_data;
	//! The number of bytes of the pixels of all the bands, uncompressed
	uint64_t data_size;
};

//! The name of a data type, as the extension names it
const char *DatacubeTypeName(DatacubeType type);

//! The number of bytes of a pixel of a data type
size_t DatacubeTypeSize(DatacubeType type);

//! Whether a datacube nodata value states no nodata value (-infinity)
bool DatacubeNoDataIsNone(double no_data);

/* Read the header of a datacube. The header is checked whole: the magic code,
 * a known data format and data type, a positive number of bands, columns and
 * rows, a nodata value finite or -infinity, and a number of bytes of the
 * pixels computed without overflow. Throws an InvalidInputException naming
 * what is wrong. */
DatacubeHeader ReadDatacubeHeader(const uint8_t *data, size_t size);

/* Return the pixels of a datacube whose header is `header`, decompressed into
 * `buffer` unless the cube is RAW, in which case they are read in place. The
 * payload after the header fills exactly the number of bytes the header
 * states: no byte short, no byte left over, and a compressed payload
 * decompresses into a buffer of exactly that size or not at all. Throws an
 * InvalidInputException naming what is wrong. */
const uint8_t *ReadDatacubePixels(const uint8_t *data, size_t size, const DatacubeHeader &header,
                                  std::vector<uint8_t> &buffer);

/* Return the RAW datacube of `header` and of the `size` bytes of `pixels`,
 * band after band, which the header states. */
std::string WriteDatacube(const DatacubeHeader &header, const uint8_t *pixels, size_t size);

} // namespace duckdb
