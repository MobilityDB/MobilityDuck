# DuckDB ↔ MEOS raster boundary

How MobilityDuck converts MEOS's `raster` to and from the raster values of DuckDB's raster extension, [`duckdb-raster`](https://github.com/ahuarte47/duckdb-raster), and why the conversion is a boundary layer of the binding rather than a second raster algebra.

## The two rasters

MobilityDuck carries **MEOS's `raster`**, the raster of the MobilityDB manual: bands of pixels with their georeferencing, the value PostgreSQL's `raster` type holds. Its whole algebra (constructors, accessors, `clip`, `mapAlgebra`, `summaryStats`, `rasterUnion`, ...) runs inside MEOS, as every family of MobilityDuck does. A `raster` column is a BLOB holding the MEOS WKB of the raster.

**`duckdb-raster`** is a community extension reading and writing raster files through GDAL. It has no raster value: `RT_Read` answers one row per tile of a file, the pixels of the tile in a `databand_N` (or `datacube`) column of type `RT_DATACUBE` and its georeferencing in a `metadata` column of type `JSON`. Its statistics are `RT_STATS` structs.

The two never share a name. Every name `duckdb-raster` adds carries the prefix `RT_` (`RT_DATACUBE`, `RT_STATS`, `RT_Read`, `RT_Cube2ArrayFloat`, ...); every name without it is MobilityDB's, the same in every engine.

```
   ┌──────────────────────────────────────────────────────────┐
   │   duckdb-raster (RT_ names)                              │
   │     RT_Read → tile_x, tile_y, metadata JSON,             │
   │               databand_N / datacube RT_DATACUBE          │
   │     RT_STATS                                             │
   └────────────────┬─────────────────────────────────────────┘
                    │ MobilityDuck boundary layer
                    │ raster(datacube, tile_x, tile_y, metadata)
                    │ CAST(rast AS RT_DATACUBE), rasterMetadata(rast)
                    │ CAST(stats AS RT_STATS / summarystats)
                    ▼
   ┌──────────────────────────────────────────────────────────┐
   │   MEOS raster (no prefix, closed algebra)                │
   │     raster, summarystats                                 │
   │     clip, mapAlgebra, summaryStats, rasterUnion, ...     │
   └──────────────────────────────────────────────────────────┘
```

The datacube and the metadata JSON are the extension's own formats, so MobilityDuck reads and writes them itself, from what MEOS states of a raster: its accessors, the bytes of the pixels of a band (`pixels(raster, band)`) and the raster built from such bytes and a georeferencing (`raster(pixels, width, height, ...)`). MEOS knows nothing of `duckdb-raster`.

## Loading the extension

`RT_DATACUBE` and `RT_STATS` are named only once `duckdb-raster` is loaded; a community extension is never loaded from a type name:

```sql
INSTALL raster FROM community;
LOAD raster;
```

Before `LOAD raster`, `CAST(r AS RT_DATACUBE)` raises `Type with name RT_DATACUBE does not exist!`. The conversions register whichever extension loads first: MobilityDuck builds `RT_DATACUBE` and `RT_STATS` by value, as `duckdb-raster` builds them, and DuckDB matches a cast by the type itself.

`raster(datacube, tile_x, tile_y, metadata)` takes the datacube as a `BLOB`, which an `RT_DATACUBE` column becomes through the extension's own cast, so a datacube stored as a plain BLOB (in a Parquet file, say) builds a raster without the extension.

## The conversions

| From | To | SQL |
|---|---|---|
| a tile of `RT_Read` | `raster` | `raster(datacube, tile_x, tile_y, metadata)` |
| `raster` | `RT_DATACUBE` | `CAST(rast AS RT_DATACUBE)` |
| `raster` | the `metadata` of its tile | `rasterMetadata(rast)` |
| `summarystats` | `RT_STATS` | `CAST(stats AS RT_STATS)` |
| `RT_STATS` | `summarystats` | `CAST(stats AS summarystats)` |

The casts are explicit: none keeps every property of its source (a datacube carries no georeferencing, `RT_STATS` a nodata count `summarystats` does not state), so a `raster` never stands for an `RT_DATACUBE`, nor a `summarystats` for an `RT_STATS`, without a `CAST`.

### A raster from a tile

`raster(datacube, tile_x, tile_y, metadata)` builds the raster of a tile of `RT_Read`: its bands from the datacube, its grid from the six GDAL coefficients of the metadata `transform`, at the pixel offset `(tile_x * blocksize_x, tile_y * blocksize_y)`, and its SRID from the metadata `crs`. An edge tile, narrower than the block size, keeps its own width and height.

```sql
SELECT tile_x, tile_y, width(r), height(r), upperLeftX(r), upperLeftY(r), SRID(r)
FROM (SELECT tile_x, tile_y, raster(databand_1, tile_x, tile_y, metadata) AS r
      FROM RT_Read('tiled.tif', blocksize_x := 2, blocksize_y := 2))
ORDER BY tile_y, tile_x;
```

| tile_x | tile_y | width | height | upperLeftX | upperLeftY | SRID |
|-------:|-------:|------:|-------:|-----:|------:|-----:|
| 0 | 0 | 2 | 2 | 100.0 | -17.0 | 3857 |
| 1 | 0 | 2 | 2 | 102.0 | -17.0 | 3857 |
| 2 | 0 | 1 | 2 | 104.0 | -17.0 | 3857 |
| 0 | 1 | 2 | 1 | 100.0 | -19.0 | 3857 |
| 1 | 1 | 2 | 1 | 102.0 | -19.0 | 3857 |
| 2 | 1 | 1 | 1 | 104.0 | -19.0 | 3857 |

Of the metadata, the conversion reads `transform`, `blocksize_x`, `blocksize_y` and `crs`, which is what the `RT_` functions read of it; the nodata value and the data type come from the datacube. A `datacube` column of `RT_Read(..., datacube := true)` builds a raster of all the bands of the file, a `databand_N` column a raster of band `N`.

### The datacube of a raster

`CAST(rast AS RT_DATACUBE)` writes the bands of a raster as one datacube, which the `RT_` functions read:

```sql
CREATE TABLE t AS SELECT addBand(
  raster(from_hex('0100020003000400'), 2, 2, 100.0, 50.0, 10.0, -10.0, 0.0, 0.0, 3857, '16BSI', -9999),
  raster(from_hex('0500060007000800'), 2, 2, 100.0, 50.0, 10.0, -10.0, 0.0, 0.0, 3857, '16BSI', -9999)) AS r;

SELECT RT_Cube2ArrayInt16(CAST(r AS RT_DATACUBE), false).values FROM t;
-- [1, 2, 3, 4, 5, 6, 7, 8]
```

### The metadata of a raster

`rasterMetadata(rast)` writes the `metadata` `RT_Read` writes of a tile, for the raster as the one tile of its datacube, as a `JSON` value:

```sql
SELECT rasterMetadata(r) FROM t;
```

```json
{"file_format": "raster", "version": "0.1.0", "data_format": "RAW", "datacube": true,
 "crs": "EPSG:3857", "transform": [100.0, 10.0, 0.0, 50.0, 0.0, -10.0],
 "bounds": [100.0, 30.0, 120.0, 50.0],
 "geometry": "POLYGON((100.0 50.0, 120.0 50.0, 120.0 30.0, 100.0 30.0, 100.0 50.0))",
 "width": 2, "height": 2, "blocksize_x": 2, "blocksize_y": 2, "band_count": 2,
 "bands": [{"name": "databand_1", "description": "", "type_name": "Int16", "data_type": 3, "data_size": 2,
            "width": 2, "height": 2, "colorinterp": 0, "nodata": -9999.0, "scale": null, "offset": null,
            "unit": ""}, ...]}
```

`transform` is the six GDAL coefficients `[upperleftx, scalex, skewx, upperlefty, skewy, scaley]`, written as the shortest decimal that reads back the same double. Per band, `type_name` and `data_type` are GDAL's name and code of the type and `nodata` the band's nodata value, or `null` for none. MEOS states no band description, color interpretation, scale, offset or unit, which are written empty, undefined (`0`) and `null`.

### The statistics

`summarystats` is the row `summaryStats` answers, `count, sum, mean, stddev, min, max`; `RT_STATS` is `minimum, maximum, sum, mean, stddev, valid_count, nodata_count`. The casts go field to field, `count` as `valid_count`, `min` and `max` as `minimum` and `maximum`. `nodata_count` is `NULL` in an `RT_STATS` cast from `summarystats` and left out of a `summarystats` cast from `RT_STATS`.

```sql
SELECT CAST(summaryStats(r) AS RT_STATS) FROM t;
-- {'minimum': 1.0, 'maximum': 4.0, 'sum': 10.0, 'mean': 2.5, 'stddev': 1.118033988749895,
--  'valid_count': 4, 'nodata_count': NULL}

SELECT CAST(RT_CubeStats(CAST(r AS RT_DATACUBE), 1) AS summarystats) FROM t;
-- {'count': 4, 'sum': 26.0, 'mean': 6.5, 'stddev': 1.118033988749895, 'min': 5.0, 'max': 8.0}
```

## Pixel types

A band's pixel type maps to the datacube's data type as PostGIS maps its pixel types to GDAL's (`rt_util_pixtype_to_gdal_datatype`):

| MEOS (PostGIS) pixel type | Datacube data type | GDAL type in the metadata | Back to MEOS |
|---|---|---|---|
| `1BB`, `2BUI`, `4BUI`, `8BUI` | `UINT8` | `Byte` (1) | `8BUI` |
| `8BSI` | `INT8` | `Int8` (14) | `8BSI` |
| `16BUI` | `UINT16` | `UInt16` (2) | `16BUI` |
| `16BSI` | `INT16` | `Int16` (3) | `16BSI` |
| `32BUI` | `UINT32` | `UInt32` (4) | `32BUI` |
| `32BSI` | `INT32` | `Int32` (5) | `32BSI` |
| `32BF` | `FLOAT` | `Float32` (6) | `32BF` |
| `64BF` | `DOUBLE` | `Float64` (7) | `64BF` |
| | `UINT64`, `INT64` | | error: no MEOS pixel type holds them |

The pixels cross as their bytes, never as doubles, so every value crosses exactly, NaN pixels and the sign of zero included.

## The round trip

A raster is the tile it gives:

```sql
SELECT raster(CAST(r AS RT_DATACUBE), 0, 0, rasterMetadata(r)) = r FROM t;
-- true
```

This holds for every pixel type of a byte or more. A band of `1BB`, `2BUI` or `4BUI` stores a byte a pixel, so its datacube is of `UINT8` and comes back as an `8BUI` band holding the same bytes, as a raster passed through PostGIS's GDAL path does.

## Formats

The datacube is a header of 24 bytes followed by the pixels: the magic code `0x5253`, the data format, the data type, the number of bands, columns and rows, and the nodata value (`-infinity` for none), all little-endian. The pixels are band after band, each band row by row, each pixel in the little-endian bytes of its type; the data format compresses them as a whole.

| Data format | Read | Written |
|---|---|---|
| `RAW` (0) | yes | yes |
| `SNAPPY` (1) | yes | |
| `GZIP` (2) | yes | |
| `ZSTD` (3) | yes | |
| `LZ4` (4) | yes, after its 4-byte uncompressed size | |

The codecs are the ones DuckDB vendors (snappy, miniz, zstd, lz4); `CAST(rast AS RT_DATACUBE)` writes `RAW`, which every `RT_` function reads and `RT_Read(..., data_format := ...)` compresses on its own side.

## The reference system

The metadata `crs` is read as `EPSG:<code>`, whose code is the raster's SRID, or as the empty string `RT_Read` writes for a file without one, which is the SRID 0. `RT_Read` writes the WKT of a reference system GDAL does not identify by an EPSG code; the WKT names no SRID, so it raises an error rather than be guessed at. `rasterMetadata` writes `EPSG:<srid>`, or the empty string for the SRID 0.

## What raises an error

A datacube and its metadata are untrusted input; every case below raises an error naming what is wrong:

| Input | Why |
|---|---|
| a datacube shorter than its header, a magic code other than `0x5253`, an unknown data format or data type code | the bytes are no datacube |
| a number of bands, columns or rows that is not positive, or whose size overflows 64 bits | the datacube holds no pixel, or states more than memory can hold |
| a width, height or number of bands beyond 65535 | rt_core states each in 16 bits |
| pixels beyond 2^32 - 1 bytes | a DuckDB value holds no more, so they make no raster value; refused before a buffer of their size is allocated |
| a payload shorter or longer than the header states; a compressed payload that decompresses into another size, fails its checksum or carries bytes after its stream | the bytes are no datacube of that header; a payload is decompressed into a buffer of exactly the header's size or not at all |
| a nodata value that is NaN or `+infinity`, or that is no value of the data type (`300` for `UINT8`, `1.5` for an integer type) | the cube states its nodata as a double; one the band cannot hold would change which pixels are nodata |
| a datacube of `UINT64` or `INT64` | no MEOS pixel type holds 64-bit integers |
| metadata that is no JSON object, a `transform` that is not 6 finite numbers, a `blocksize_x` or `blocksize_y` that is not a positive 32-bit integer, a `crs` other than `EPSG:<code>` or empty | the tile cannot be georeferenced |
| a negative `tile_x` or `tile_y`, or a pixel offset beyond 32 bits | the tile lies outside any raster |
| `CAST(rast AS RT_DATACUBE)` of a raster whose bands differ in pixel type or in nodata value (its presence or its value) | a datacube holds one data type and one nodata value |
| `CAST(rast AS RT_DATACUBE)` or `rasterMetadata(rast)` of a band whose nodata value is NaN or infinite | the datacube reads `-infinity` as no nodata and JSON has no such number |
| `CAST(rast AS RT_DATACUBE)` of a raster without a band or a pixel | a datacube holds at least one pixel |

## Testing

`test/sql/raster_boundary.test` builds rasters from datacubes the published `duckdb-raster` wrote in each data type and data format, pasted as hex, and runs without the extension. `test/sql/raster_boundary_rt.test` tests the casts against the extension itself and runs where `MOBILITYDUCK_RASTER_EXTENSION` names its file, the community binary of `duckdb-raster` for the DuckDB version of the build:

```sh
MOBILITYDUCK_RASTER_EXTENSION=~/.duckdb/extensions/v1.5.6/linux_amd64/raster.duckdb_extension \
  build/release/test/unittest "test/sql/raster_boundary_rt.test"
```

## See also

- [`doc/geography-boundary.md`](geography-boundary.md): the boundary of the geodetic geography, the same layer for another host type.
- The [function reference of `duckdb-raster`](https://github.com/ahuarte47/duckdb-raster/blob/main/docs/functions.md): `RT_Read`, the datacube and the `RT_` functions.
- MobilityDB MEOS C-library header `meos_raster.h`: the raster functions this boundary layer calls.
