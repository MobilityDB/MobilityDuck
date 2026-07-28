# DuckDB ↔ MEOS geography boundary

How MobilityDuck represents geodetic geography values across the MEOS↔DuckDB columnar boundary, and why a separate `GEOGRAPHY` LogicalType is required even though MEOS already handles geodetic semantics internally.

## The problem in one paragraph

MEOS has the **closed-algebra property** for geography: every geographic operation — `geog_in`, `geog_area`, `eIntersects(geog, geog)`, `tgeog_length`, `tgeog_speed`, and so on — takes geodetic inputs, performs WGS-84 spheroidal-metre computation, and returns a properly-typed geodetic result without leaving the MEOS C runtime. As long as the value stays inside MEOS, the geodetic flag is preserved in the `GSERIALIZED` type tag and the spheroidal interpretation is automatic.

The problem appears only at the boundary. When MobilityDuck projects a MEOS geography value into DuckDB's columnar layout, DuckDB's **bundled `spatial` extension exposes one logical type — `GEOMETRY` — that has no geodetic bit**. The flag is therefore at risk of being lost the moment a MEOS geography result becomes a DuckDB column value: the next operator in the query plan, the COPY-to-Parquet writer, or the join key extraction would see a plain WKB blob with no way to know whether it should be interpreted on the sphere or the plane.

## The solution

MobilityDuck **registers its own `GEOGRAPHY` LogicalType** — a `BLOB` alias whose payload is MEOS-WKB with the geodetic flag preserved in the type tag. The semantics live in MEOS; DuckDB only carries the BLOB through the columnar engine with a stable alias name. No change to DuckDB itself is needed; no dependence on a third-party `duckdb-geography` extension.

This is an instance of the standing ecosystem rule that every binding owns a *thin boundary layer* converting platform-native types to/from the MEOS canonical encoding, with the canonical encoding never leaking and the platform-native type never leaking into MEOS calls.

```
   ┌──────────────────────────────────────────────────────────┐
   │            DuckDB columnar engine                        │
   │   ─────────────────────────────────────────────          │
   │     GEOMETRY (BLOB alias, no geodetic bit)               │
   │     GEOGRAPHY (BLOB alias, MEOS-WKB with geodetic bit)   │
   │     TGEOGPOINT (BLOB alias, temporal geodetic point)     │
   │     TGEOMPOINT (BLOB alias, temporal planar point)       │
   └────────────────┬─────────────────────────────────────────┘
                    │ MobilityDuck boundary layer
                    │ (ST_GeogFromText, ST_AsText, casts, …)
                    ▼
   ┌──────────────────────────────────────────────────────────┐
   │            MEOS C runtime (closed algebra)               │
   │   ─────────────────────────────────────────────          │
   │     GSERIALIZED  (geodetic flag in type tag)             │
   │     geog_in, geog_area, eIntersects(geog,geog), …        │
   │     length(tgeog), speed(tgeog), tDwithin(tgeog,tgeog), …│
   │     stays inside MEOS — no scalar value ever crosses     │
   │     the boundary mid-computation                         │
   └──────────────────────────────────────────────────────────┘
```

## Registration

```cpp
// src/spatial/geography.cpp (sketch)
LogicalType GEOGRAPHY = LogicalType::BLOB;
GEOGRAPHY.SetAlias("GEOGRAPHY");
ExtensionLoader::RegisterType(loader, "GEOGRAPHY", GEOGRAPHY);
```

The alias makes `INSERT INTO … VALUES (geography 'POINT(4.35 50.85)')` parse, `SELECT ST_GeogFromText('POINT(4.35 50.85)')` type-check, and TemporalParquet round-trips preserve the type information (the alias is stored in the Parquet `temporal` footer JSON; readers reconstruct it).

## I/O surface

The functions MobilityDuck registers on top of the `GEOGRAPHY` LogicalType. Each call is a thin shim over the corresponding MEOS export — no semantic logic in the binding.

| DuckDB UDF | DuckDB signature | MEOS function called |
|---|---|---|
| `ST_GeogFromText(VARCHAR)` | → `GEOGRAPHY` | `geog_in` |
| `ST_AsText(GEOGRAPHY)` | → `VARCHAR` | `geo_as_ewkt` |
| `ST_AsBinary(GEOGRAPHY)` | → `BLOB` | `geo_as_ewkb` |
| `ST_GeogFromBinary(BLOB)` | → `GEOGRAPHY` | `geo_from_ewkb` (asserts geodetic flag) |
| `geography(BLOB)` (implicit cast) | `BLOB` → `GEOGRAPHY` | `geo_from_ewkb` |
| `geometry(GEOGRAPHY)` (explicit cast) | `GEOGRAPHY` → `GEOMETRY` | flips the geodetic bit, keeps the WKB |
| `geography(GEOMETRY)` (explicit cast) | `GEOMETRY` → `GEOGRAPHY` | asserts lon/lat range, sets the geodetic bit |

## Operations stay closed inside MEOS

Every operation on a `GEOGRAPHY` column delegates to a MEOS function that takes geodetic input and returns the correct type. The binding never has to know what "geodetic length on the sphere" means; it just calls the right C function.

| DuckDB UDF | Returns | MEOS function called |
|---|---|---|
| `length(GEOGRAPHY)` | `DOUBLE` (metres, spheroidal) | `geog_length` |
| `area(GEOGRAPHY, spheroid BOOLEAN)` | `DOUBLE` (m²) | `geog_area` |
| `eIntersects(GEOGRAPHY, GEOGRAPHY)` | `BOOLEAN` | `geog_intersects` |
| `eContains(GEOGRAPHY, GEOGRAPHY)` | `BOOLEAN` | `geog_contains` |
| `nearestApproachDistance(GEOGRAPHY, GEOGRAPHY)` | `DOUBLE` | `geog_distance` |
| `tgeogpoint(GEOGRAPHY, TIMESTAMPTZ)` | `TGEOGPOINT` | `tgeogpoint_make` |
| `valueAtTimestamp(TGEOGPOINT, TIMESTAMPTZ)` | `GEOGRAPHY` | already returns the geodetic-flagged GSERIALIZED |

DuckDB never sees a non-geodetic representation of a geodetic value during a computation: every intermediate stays inside MEOS until the final result hits the column boundary, at which point it is either a primitive type (DOUBLE, BOOLEAN, TIMESTAMPTZ) or a properly-typed `GEOGRAPHY` / `TGEOGPOINT` BLOB.

## Cast matrix

The complete set of inter-type conversions involving `GEOGRAPHY`. Implicit casts apply where the conversion is unambiguous and lossless; explicit casts where there is a semantic choice (most commonly: dropping or setting the geodetic flag, or asserting a coordinate range).

|                                  | `GEOMETRY` (DuckDB Spatial) | `GEOGRAPHY` (MobilityDuck) | `TGEOGPOINT` (MobilityDuck) | `TGEOMPOINT` (MobilityDuck) |
|---|:---:|:---:|:---:|:---:|
| **from `GEOMETRY`**              | (identity)         | explicit cast: assert lon/lat; set geodetic flag    | invalid                     | implicit                    |
| **from `GEOGRAPHY`**             | explicit cast: drop geodetic flag           | (identity)         | via `tgeogpoint(GEOGRAPHY, TIMESTAMPTZ)` | invalid              |
| **from `TGEOGPOINT`**            | via `geometry(tgeogpoint)`  | via `valueAtTimestamp` then implicit | (identity)         | `tgeogpoint_to_tgeompoint` |
| **from `TGEOMPOINT`**            | implicit            | invalid                     | `tgeompoint_to_tgeogpoint` | (identity)         |

This is the same shape MobilityDB-on-Postgres has between `geometry` / `geography` / `tgeompoint` / `tgeogpoint`. MobilityDuck mirrors the matrix; MEOS does the conversion work.

## TemporalParquet round-trip preservation

A column declared `GEOGRAPHY` in MobilityDuck is written to Parquet as `BYTE_ARRAY` carrying MEOS-WKB with the geodetic flag in the type tag. The TemporalParquet footer JSON records the type alias (`"base_type": "geography"`), so a downstream reader (MobilityDuck, MobilityDB, MobilitySpark, MobilityAPI) reconstructs both the alias and the geodetic interpretation without ambiguity:

```json
{
  "temporal": {
    "trajectory": {
      "base_type": "tgeogpoint",
      "geodetic":  true,
      "srid":      4326,
      "subtype":   "Sequence",
      "interpolation": "linear"
    },
    "footprint": {
      "base_type": "geography",
      "geodetic":  true,
      "srid":      4326
    }
  }
}
```

Closed-algebra producers (`spaceTimeSplit`, `valueSet`, `eIntersection`) preserve the type — `eIntersection(GEOGRAPHY, GEOGRAPHY)` returns `GEOGRAPHY`, and the round-trip through Parquet is a no-op as long as the writer and reader both honour the footer convention.

## Pitfalls a binding implementation must avoid

| Pitfall | Why it breaks the boundary |
|---|---|
| Storing a `GEOGRAPHY` value as `GEOMETRY` for join compatibility | DuckDB's `GEOMETRY` has no geodetic bit; the next operator interprets the WKB on the plane and returns Cartesian metres instead of spheroidal metres |
| Reusing the DuckDB Spatial `ST_*` functions on `GEOGRAPHY` BLOBs | DuckDB Spatial's `ST_Length`, `ST_Area`, `ST_Intersects` are Cartesian; they ignore the geodetic flag in the input and produce planar results |
| Using `ST_GeomFromText` to construct a `GEOGRAPHY` value | The DuckDB Spatial constructor sets the geodetic flag to false; MobilityDuck must use its own `ST_GeogFromText` shim |
| Stripping the geodetic flag in TemporalParquet output to "save space" | The flag is a single bit in the type tag; stripping it makes the round-trip lossy and breaks every downstream consumer |
| Treating `GEOGRAPHY` as a column with the DuckDB Spatial `GEOMETRY` extension's spatial index | Spatial indexes on planar metrics produce wrong candidate sets for geodetic queries; use `th3index` or `TRTREE` instead |

## State of the implementation

| Component | Where | Status |
|---|---|---|
| MEOS closed-algebra geodetic functions | MobilityDB MEOS C library, master | Available — `geog_in`, `geog_area`, `geog_intersects`, `geog_length`, `tgeogpoint_make`, etc. |
| `tgeogpoint` LogicalType + temporal-geographic UDFs | MobilityDuck `src/geo/tgeogpoint*.cpp` | Already registered |
| `GEOGRAPHY` LogicalType + ST_GeogFromText / ST_AsText / ST_AsBinary / ST_GeogFromBinary | (planned PR, see "Pending work" below) | Pending |
| Casts between `GEOMETRY` ⇄ `GEOGRAPHY` and `GEOGRAPHY` ⇄ `TGEOGPOINT` | (planned PR) | Pending |
| TemporalParquet footer support for `"base_type": "geography"` | `tools/temporal_parquet.py` | Already supports arbitrary `base_type` strings; the consumer reads the alias verbatim |
| Tests for round-trip, value-equality, cast-matrix, length/area numeric checks | `test/sql/geography.test` (planned) | Pending |

The retirement of the old `Spherical_lonlat_rect_area_m2` / `Geodetic_stbox_footprint_area` workaround (which previously approximated geodetic area in the binding because MEOS-1.3 `stbox_area` could SIGSEGV) is in [PR #165](https://github.com/MobilityDB/MobilityDuck/pull/165); the present design assumes that workaround is gone.

## Pending work

A single PR scope, ~430 LoC total:

| Item | LoC | Notes |
|---|---|---|
| Register `GEOGRAPHY` LogicalType in `mobilityduck_extension.cpp` | ~10 | One line of `SetAlias("GEOGRAPHY")` plus the `RegisterType` call |
| `ST_GeogFromText`, `ST_AsText`, `ST_AsBinary`, `ST_GeogFromBinary` UDFs | ~150 | Each ~40 LoC, all thin shims over MEOS exports |
| Casts `GEOMETRY` ⇄ `GEOGRAPHY` and `GEOGRAPHY` ⇄ `TGEOGPOINT` extras | ~80 | The `TGEOGPOINT(GEOGRAPHY, TIMESTAMPTZ)` constructor is already registered for the existing `tgeogpoint` flow; the new casts plug into the same registry |
| `test/sql/geography.test` | ~200 | Round-trip, cast-matrix, numeric checks against MEOS-on-Postgres ground truth |

The total cost is bounded because every line of geodetic semantics already exists in MEOS; the binding just labels and routes.

## See also

- [`doc/multi-duckdb-version.md`](multi-duckdb-version.md) — version-target story; the geography boundary registers identically on DuckDB v1.4.4 and v1.5.x.
- [Discussion #913 — Temporal Data Lake RFC](https://github.com/MobilityDB/MobilityDB/discussions/913) — places `tgeogpoint` (and by extension `GEOGRAPHY`) at the centre of the cross-platform query dialect.
- [`docs/DuckDB-Parity-Gaps.md`](../docs/DuckDB-Parity-Gaps.md) — catalogues the few MobilityDB SQL surfaces that have no DuckDB equivalent.
- MobilityDB MEOS C-library headers `meos_geo.h` — the closed-algebra function declarations that this boundary layer dispatches to.
