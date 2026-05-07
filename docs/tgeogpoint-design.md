# TGEOGPOINT in MobilityDuck — Design Note

## Problem

`length(tgeompoint)` returns Euclidean distance in the coordinate unit of the spatial
reference system.  For lon/lat WGS-84 input (SRID 4326) that unit is degrees, not km.
This is surprising to users who expect geodetic (spheroidal) distances.

The canonical solution in MobilityDB and MEOS is `tgeogpoint`: a temporal type whose
geodetic flag is set, causing all spatial math to route through MEOS's spheroidal engine
(Haversine / PostGIS Vincenty), returning metres.

## DuckDB Geography Extension — Assessment (2026-05-07)

DuckDB has a community geography extension (`duckdb-geography`) based on **Google S2
Geometry**.  S2 uses a spherical Earth model.  MEOS's geodetic engine uses a
**spheroidal WGS-84** model (PostGIS Vincenty).  The two models produce different
distance values and are not interoperable.

Additionally, the extension is community-maintained, not in the DuckDB core, and is
still marked experimental.

**Decision**: MobilityDuck's `TGEOGPOINT` does NOT depend on the DuckDB geography
extension.  It accepts `GEOMETRY` (lon/lat) as input — the same DuckDB type used by
`TGEOMPOINT` — and sets the geodetic flag **inside MEOS** so that all geodetic math
lives exclusively in MEOS.

## Implementation Architecture

`TGEOGPOINT` shares almost all infrastructure with `TGEOMPOINT`.  Only two functions
diverge:

| Function | TGEOMPOINT | TGEOGPOINT |
|---|---|---|
| `Tpoint_in(str)` | calls `tgeompoint_in` | calls `tgeogpoint_in` |
| `Tpointinst_constructor(geom, ts)` | calls `tpointinst_make(gs, ts)` | adds `FLAGS_SET_GEODETIC(gs->gflags, 1)` before `tpointinst_make` |

Everything else — generic temporal ops, spatial relationships, sequence constructors,
round-trip IO — delegates to the same infrastructure used by `TGEOMPOINT`.
`GetTemptypeFromAlias("TGEOGPOINT")` already returns `T_TGEOGPOINT`, so sequence
constructors (`tgeogpointSeq`, `tgeogpointSeqSet`) work without any change to the
constructor dispatch.

## Cross-Platform Uniformity

This design is the uniform fix for the `length_deg` problem across all ecosystem
platforms:

| Platform | How tgeogpoint works |
|---|---|
| MobilityDB | PostGIS `geography` type; PostGIS handles geodetic math |
| MobilityDuck | `GEOMETRY` input; `FLAGS_SET_GEODETIC` set; MEOS owns geodetic math |
| PyMEOS | `TGeogPoint` class; geodetic flag set in MEOS |
| MEOS-WKB / Parquet | Geodetic flag in type tag (`T_TGEOGPOINT`); self-describing across all platforms |

The last point is important: a Parquet file written by `asBinary(tgeogpointSeq(...))` in
MobilityDuck can be read with `tgeogpointFromBinary` in MobilityDB and vice-versa —
the MEOS-WKB type tag carries the geodetic flag.

## Usage

```sql
-- Geodetic distance in metres (not degrees):
SELECT length(tgeogpointSeq(
    list(TGEOGPOINT(ST_Point(lon, lat), ts) ORDER BY ts)
))
FROM ais_raw GROUP BY mmsi;

-- Round-trip through Parquet:
COPY (SELECT mmsi, asBinary(traj) AS traj FROM trajectories)
TO 'ais.parquet' (FORMAT PARQUET);

SELECT mmsi, length(tgeogpointFromBinary(traj)) AS length_m
FROM read_parquet('ais.parquet');
```

## Future Path

When the DuckDB geography extension matures (joins core, aligns on spheroidal model,
stable API), add an **additive overload** `TGEOGPOINT(GEOGRAPHY, TIMESTAMPTZ)` that
converts the S2 representation to WGS-84 lon/lat and creates the MEOS geodetic instant.
The `GEOMETRY`-input overload stays; the new overload is additive, not breaking.
