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

## Spatial Predicates with GEOMETRY Input

Because DuckDB has no `geography` type, spatial predicates that compare a `TGEOGPOINT`
against a region use the plain `GEOMETRY` type:

```sql
SELECT entity_id
FROM trajectories
WHERE eIntersects(
    ST_GeomFromText('POLYGON((11.5 55.0,13.5 55.0,13.5 56.5,11.5 56.5,11.5 55.0))'),
    traj
);
```

MobilityDuck transparently converts the `GEOMETRY` to a proper geodetic GSERIALIZED using
MEOS's `geom_to_geog()` when the opposing temporal type is a geodetic one (i.e. when
`MEOS_FLAGS_GET_GEODETIC(tgeom->flags)` is true).  This mirrors what PostgreSQL does when
an implicit `geometry → geography` cast is applied in MobilityDB.

**Root causes fixed (commit `3441566`, 2026-05-07):**

| Bug | Symptom | Fix |
|---|---|---|
| SRID hardcoded 0 in `(GEOMETRY, temporal)` direction | "Operation on mixed SRID" | Deserialize `tgeom` first; use `tspatial_srid(tgeom)` |
| Geodetic flag mismatch | "Operation on mixed planar and geodetic coordinates" | Call `geom_to_geog(gs)` to rebuild GSERIALIZED with valid 3D bbox + GEODETIC=1 |

Applies to all 12 `(GEOMETRY, temporal)` overloads: `eIntersects/eContains/eDisjoint/
eTouches` (ever/always variants) and `tIntersects/tContains/tDisjoint/tTouches/tDwithin`
families.

## Usage

```sql
-- Geodetic distance in metres (not degrees):
SELECT length(tgeogpointSeq(
    list(TGEOGPOINT(ST_Point(lon, lat), ts) ORDER BY ts)
))
FROM ais_raw GROUP BY mmsi;

-- Region intersection — GEOMETRY is auto-promoted to geodetic:
SELECT mmsi
FROM trajectories
WHERE eIntersects(
    ST_GeomFromText('POLYGON((xmin ymin,xmax ymin,xmax ymax,xmin ymax,xmin ymin))'),
    traj
);

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
