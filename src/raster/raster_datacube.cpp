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

/* The datacube codec of the raster boundary (raster_datacube.hpp). A datacube
 * reaching it is untrusted input: every field of the header is checked before
 * a byte is allocated, the number of bytes of the pixels is computed with
 * overflow-checked 64-bit arithmetic, and a compressed payload decompresses
 * into a buffer of exactly that size, through the codecs DuckDB vendors in
 * third_party: snappy, miniz for GZIP, zstd and lz4, which duckdb-raster's
 * data_format_utils.cpp calls the same way. */

#include "raster/raster_datacube.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/typedefs.hpp"

#include "lz4.hpp"
#include "miniz.hpp"
#include "snappy.h"
#include "zstd.h"

#include <climits>
#include <cmath>
#include <cstring>
#include <limits>

namespace duckdb {

namespace {

uint16_t ReadLE16(const uint8_t *p) {
	return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t ReadLE32(const uint8_t *p) {
	return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
	       (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t ReadLE64(const uint8_t *p) {
	return static_cast<uint64_t>(ReadLE32(p)) | (static_cast<uint64_t>(ReadLE32(p + 4)) << 32);
}

void WriteLE32(std::string &out, uint32_t v) {
	for (int i = 0; i < 4; i++) {
		out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
	}
}

void WriteLE64(std::string &out, uint64_t v) {
	WriteLE32(out, static_cast<uint32_t>(v & 0xFFFFFFFFu));
	WriteLE32(out, static_cast<uint32_t>(v >> 32));
}

int32_t ToInt32(uint32_t bits) {
	int32_t v;
	std::memcpy(&v, &bits, sizeof v);
	return v;
}

double ToDouble(uint64_t bits) {
	double v;
	std::memcpy(&v, &bits, sizeof v);
	return v;
}

uint64_t FromDouble(double v) {
	uint64_t bits;
	std::memcpy(&bits, &v, sizeof bits);
	return bits;
}

const char *FormatName(DatacubeFormat format) {
	switch (format) {
	case DatacubeFormat::RAW:
		return "RAW";
	case DatacubeFormat::SNAPPY:
		return "SNAPPY";
	case DatacubeFormat::GZIP:
		return "GZIP";
	case DatacubeFormat::ZSTD:
		return "ZSTD";
	case DatacubeFormat::LZ4:
		return "LZ4";
	}
	return "?";
}

[[noreturn]] void ThrowPayload(DatacubeFormat format, const std::string &what) {
	throw InvalidInputException("datacube: the %s payload %s", FormatName(format), what);
}

[[noreturn]] void ThrowPayloadSize(DatacubeFormat format, const char *what, uint64_t got, uint64_t expected) {
	throw InvalidInputException("datacube: the %s payload %s %llu bytes where the header states %llu",
	                            FormatName(format), what, static_cast<unsigned long long>(got),
	                            static_cast<unsigned long long>(expected));
}

/* The GZIP member duckdb-raster writes: a header of 10 bytes with no flag, a
 * raw deflate stream, and a footer of the CRC-32 and the size of the data,
 * little-endian. The member fills the payload exactly, and its data fills the
 * buffer exactly; the footer is checked against what the stream gave. */
void InflateGzip(const uint8_t *in, size_t n, uint8_t *out, size_t expected) {
	const DatacubeFormat f = DatacubeFormat::GZIP;
	constexpr size_t HEADER = 10, FOOTER = 8;
	if (n < HEADER + FOOTER) {
		ThrowPayload(f, "is shorter than a GZIP header and footer");
	}
	if (in[0] != 0x1F || in[1] != 0x8B || in[2] != 0x08) {
		ThrowPayload(f, "does not open with the GZIP header of a deflate stream");
	}
	if (in[3] != 0) {
		ThrowPayload(f, "states GZIP header flags, of which a datacube member carries none");
	}
	duckdb_miniz::mz_stream stream;
	std::memset(&stream, 0, sizeof stream);
	if (duckdb_miniz::mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS) != duckdb_miniz::MZ_OK) {
		ThrowPayload(f, "cannot be inflated: the inflater did not start");
	}
	const uint8_t *next_in = in + HEADER;
	size_t in_left = n - HEADER - FOOTER;
	uint8_t *next_out = out;
	size_t out_left = expected;
	int ret = duckdb_miniz::MZ_OK;
	while (ret == duckdb_miniz::MZ_OK) {
		const unsigned int in_chunk = static_cast<unsigned int>(in_left < UINT_MAX ? in_left : UINT_MAX);
		const unsigned int out_chunk = static_cast<unsigned int>(out_left < UINT_MAX ? out_left : UINT_MAX);
		stream.next_in = next_in;
		stream.avail_in = in_chunk;
		stream.next_out = next_out;
		stream.avail_out = out_chunk;
		ret = duckdb_miniz::mz_inflate(&stream, in_chunk == in_left ? duckdb_miniz::MZ_FINISH
		                                                            : duckdb_miniz::MZ_NO_FLUSH);
		const size_t used_in = in_chunk - stream.avail_in;
		const size_t used_out = out_chunk - stream.avail_out;
		next_in += used_in;
		in_left -= used_in;
		next_out += used_out;
		out_left -= used_out;
		if (ret == duckdb_miniz::MZ_OK && used_in == 0 && used_out == 0) {
			/* No progress: the buffer is full while the stream goes on */
			break;
		}
	}
	duckdb_miniz::mz_inflateEnd(&stream);
	if (ret != duckdb_miniz::MZ_STREAM_END) {
		ThrowPayload(f, "does not inflate into exactly the number of bytes the header states");
	}
	if (out_left != 0) {
		ThrowPayloadSize(f, "inflates into", expected - out_left, expected);
	}
	if (in_left != 0) {
		ThrowPayload(f, "carries bytes after its deflate stream and footer");
	}
	const uint8_t *footer = in + n - FOOTER;
	const uint32_t crc = static_cast<uint32_t>(duckdb_miniz::mz_crc32(MZ_CRC32_INIT, out, expected));
	if (ReadLE32(footer) != crc) {
		ThrowPayload(f, "fails its CRC-32");
	}
	if (ReadLE32(footer + 4) != static_cast<uint32_t>(expected & 0xFFFFFFFFu)) {
		ThrowPayload(f, "states in its footer a size other than the header's");
	}
}

} // namespace

const char *DatacubeTypeName(DatacubeType type) {
	switch (type) {
	case DatacubeType::UINT8:
		return "UINT8";
	case DatacubeType::INT8:
		return "INT8";
	case DatacubeType::UINT16:
		return "UINT16";
	case DatacubeType::INT16:
		return "INT16";
	case DatacubeType::UINT32:
		return "UINT32";
	case DatacubeType::INT32:
		return "INT32";
	case DatacubeType::UINT64:
		return "UINT64";
	case DatacubeType::INT64:
		return "INT64";
	case DatacubeType::FLOAT:
		return "FLOAT";
	case DatacubeType::DOUBLE:
		return "DOUBLE";
	}
	return "?";
}

size_t DatacubeTypeSize(DatacubeType type) {
	switch (type) {
	case DatacubeType::UINT8:
	case DatacubeType::INT8:
		return 1;
	case DatacubeType::UINT16:
	case DatacubeType::INT16:
		return 2;
	case DatacubeType::UINT32:
	case DatacubeType::INT32:
	case DatacubeType::FLOAT:
		return 4;
	case DatacubeType::UINT64:
	case DatacubeType::INT64:
	case DatacubeType::DOUBLE:
		return 8;
	}
	return 0;
}

bool DatacubeNoDataIsNone(double no_data) {
	return std::isinf(no_data) && no_data < 0;
}

DatacubeHeader ReadDatacubeHeader(const uint8_t *data, size_t size) {
	if (size < DATACUBE_HEADER_SIZE) {
		throw InvalidInputException("datacube: %llu bytes cannot hold its header of %llu bytes",
		                            static_cast<unsigned long long>(size),
		                            static_cast<unsigned long long>(DATACUBE_HEADER_SIZE));
	}
	const uint16_t magic = ReadLE16(data);
	if (magic != DATACUBE_MAGIC) {
		throw InvalidInputException("datacube: the magic code is 0x%04x, not 0x5253", static_cast<unsigned>(magic));
	}
	if (data[2] > static_cast<uint8_t>(DatacubeFormat::LZ4)) {
		throw InvalidInputException("datacube: unknown data format code %d", static_cast<int>(data[2]));
	}
	if (data[3] > static_cast<uint8_t>(DatacubeType::DOUBLE)) {
		throw InvalidInputException("datacube: unknown data type code %d", static_cast<int>(data[3]));
	}
	DatacubeHeader header;
	header.format = static_cast<DatacubeFormat>(data[2]);
	header.type = static_cast<DatacubeType>(data[3]);
	header.bands = ToInt32(ReadLE32(data + 4));
	header.cols = ToInt32(ReadLE32(data + 8));
	header.rows = ToInt32(ReadLE32(data + 12));
	header.no_data = ToDouble(ReadLE64(data + 16));
	if (header.bands <= 0 || header.cols <= 0 || header.rows <= 0) {
		throw InvalidInputException("datacube: %d bands of %d columns and %d rows, where each must be positive",
		                            header.bands, header.cols, header.rows);
	}
	if (!std::isfinite(header.no_data) && !DatacubeNoDataIsNone(header.no_data)) {
		throw InvalidInputException("datacube: the nodata value is %s, where it is finite or -infinity for none",
		                            std::isnan(header.no_data) ? "NaN" : "+infinity");
	}
	uint64_t total = 0;
	if (__builtin_mul_overflow(static_cast<uint64_t>(header.bands), static_cast<uint64_t>(header.cols), &total) ||
	    __builtin_mul_overflow(total, static_cast<uint64_t>(header.rows), &total) ||
	    __builtin_mul_overflow(total, static_cast<uint64_t>(DatacubeTypeSize(header.type)), &total) ||
	    total > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
		throw InvalidInputException("datacube: %d bands of %d x %d pixels of %s overflow the size of a buffer",
		                            header.bands, header.cols, header.rows, DatacubeTypeName(header.type));
	}
	header.data_size = total;
	return header;
}

const uint8_t *ReadDatacubePixels(const uint8_t *data, size_t size, const DatacubeHeader &header,
                                  std::vector<uint8_t> &buffer) {
	const uint8_t *payload = data + DATACUBE_HEADER_SIZE;
	const size_t n = size - DATACUBE_HEADER_SIZE;
	const size_t expected = static_cast<size_t>(header.data_size);
	const DatacubeFormat f = header.format;
	switch (f) {
	case DatacubeFormat::RAW:
		if (n != expected) {
			ThrowPayloadSize(f, "holds", n, expected);
		}
		return payload;
	case DatacubeFormat::SNAPPY: {
		const char *in = const_char_ptr_cast(payload);
		size_t length = 0;
		if (!duckdb_snappy::GetUncompressedLength(in, n, &length)) {
			ThrowPayload(f, "states no uncompressed length");
		}
		if (length != expected) {
			ThrowPayloadSize(f, "decompresses into", length, expected);
		}
		if (!duckdb_snappy::IsValidCompressedBuffer(in, n)) {
			ThrowPayload(f, "is not a Snappy stream of that length");
		}
		buffer.resize(expected);
		if (!duckdb_snappy::RawUncompress(in, n, char_ptr_cast(buffer.data()))) {
			ThrowPayload(f, "does not decompress");
		}
		return buffer.data();
	}
	case DatacubeFormat::GZIP:
		buffer.resize(expected);
		InflateGzip(payload, n, buffer.data(), expected);
		return buffer.data();
	case DatacubeFormat::ZSTD: {
		const unsigned long long content = duckdb_zstd::ZSTD_getFrameContentSize(payload, n);
		if (content == ZSTD_CONTENTSIZE_ERROR) {
			ThrowPayload(f, "is not a Zstandard frame");
		}
		if (content == ZSTD_CONTENTSIZE_UNKNOWN) {
			ThrowPayload(f, "states no content size in its frame");
		}
		if (content != static_cast<unsigned long long>(expected)) {
			ThrowPayloadSize(f, "decompresses into", content, expected);
		}
		const size_t frame = duckdb_zstd::ZSTD_findFrameCompressedSize(payload, n);
		if (duckdb_zstd::ZSTD_isError(frame)) {
			ThrowPayload(f, "is a truncated Zstandard frame");
		}
		if (frame != n) {
			ThrowPayload(f, "carries bytes after its Zstandard frame");
		}
		buffer.resize(expected);
		const size_t got = duckdb_zstd::ZSTD_decompress(buffer.data(), expected, payload, n);
		if (duckdb_zstd::ZSTD_isError(got) || got != expected) {
			ThrowPayload(f, "does not decompress into exactly the number of bytes the header states");
		}
		return buffer.data();
	}
	case DatacubeFormat::LZ4: {
		/* duckdb-raster writes the uncompressed size, uint32 little-endian,
		 * before the LZ4 block */
		if (n < 4) {
			ThrowPayload(f, "is shorter than its size prefix of 4 bytes");
		}
		const uint32_t length = ReadLE32(payload);
		if (static_cast<uint64_t>(length) != static_cast<uint64_t>(expected)) {
			ThrowPayloadSize(f, "states", length, expected);
		}
		if (n - 4 > static_cast<size_t>(INT_MAX) || expected > static_cast<size_t>(INT_MAX)) {
			ThrowPayload(f, "exceeds the size of an LZ4 block");
		}
		buffer.resize(expected);
		const int got = duckdb_lz4::LZ4_decompress_safe(const_char_ptr_cast(payload + 4), char_ptr_cast(buffer.data()),
		                                                static_cast<int>(n - 4), static_cast<int>(expected));
		if (got < 0 || static_cast<size_t>(got) != expected) {
			ThrowPayload(f, "does not decompress into exactly the number of bytes the header states");
		}
		return buffer.data();
	}
	}
	throw InvalidInputException("datacube: unknown data format code %d", static_cast<int>(f));
}

std::string WriteDatacube(const DatacubeHeader &header, const uint8_t *pixels, size_t size) {
	std::string out;
	out.reserve(DATACUBE_HEADER_SIZE + size);
	out.push_back(static_cast<char>(DATACUBE_MAGIC & 0xFF));
	out.push_back(static_cast<char>(DATACUBE_MAGIC >> 8));
	out.push_back(static_cast<char>(DatacubeFormat::RAW));
	out.push_back(static_cast<char>(header.type));
	WriteLE32(out, static_cast<uint32_t>(header.bands));
	WriteLE32(out, static_cast<uint32_t>(header.cols));
	WriteLE32(out, static_cast<uint32_t>(header.rows));
	WriteLE64(out, FromDouble(header.no_data));
	out.append(const_char_ptr_cast(pixels), size);
	return out;
}

} // namespace duckdb
