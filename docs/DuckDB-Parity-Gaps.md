# DuckDB-specific parity gaps

This page lists MobilityDB / MEOS surface that cannot be exposed in MobilityDuck
in identical form because of properties of DuckDB's parser, type system, or
extension model. Everything here has already been audited against MobilityDB's
SQL surface and either reshaped (named-function equivalent) or, where no
equivalent is reachable, deliberately left out. None of these are open work
items in the parity task — they are structural and won't be closed by additional
MobilityDuck-side code.

If MobilityDB upstream or DuckDB upstream changes any of the constraints below,
the corresponding row should be revisited.

## Operator forms the DuckDB parser rejects

DuckDB's SQL parser rejects multi-character operator tokens that PostgreSQL
accepts. MobilityDB defines several such operators on its temporal / position /
distance surfaces. The named-function equivalent registered by MobilityDuck is
listed alongside.

| MobilityDB operator | Semantics                          | MobilityDuck equivalent          |
|---------------------|------------------------------------|----------------------------------|
| `\|>>`              | strictly above (Y or Z dimension)  | `above(a, b)`                    |
| `<<\|`              | strictly below                     | `below(a, b)`                    |
| `&<\|`              | overlaps-or-below                  | `overbelow(a, b)`                |
| `\|&>`              | overlaps-or-above                  | `overabove(a, b)`                |
| `/>>`               | strictly in front (Z)              | `front(a, b)`                    |
| `<</`               | strictly behind (Z)                | `back(a, b)`                     |
| `&</`               | overlaps-or-behind                 | `overback(a, b)`                 |
| `/&>`               | overlaps-or-in-front               | `overfront(a, b)`                |
| `~=`                | "same" (geometric equality)        | not exposed at SQL level         |
| `<#>`               | nearest-approach distance          | `nearestApproachDistance(a, b)`  |
| `\|=\|`             | distance between trajectories      | `distance(a, b)` (named form)    |

For the L/R variants `<<`, `>>`, `&<`, `&>` the DuckDB parser does accept the
tokens, and MobilityDuck registers them as operators (see
`src/temporal/temporal.cpp` "tspatial × {stbox, tspatial} position predicates").
Only the multi-character forms above are unreachable.

The above/below/front/back named functions cover the entire set of position
predicates between {`tgeompoint`, `tgeometry`, `stbox`} pairs. Any code that
ports MobilityDB SQL using `|>>` etc. should rewrite the operator to the named
function — semantics are identical.

## GEOMETRY-type-trip operators

A few operators exist on PostGIS's `geometry` that MobilityDB extends to its
spatiotemporal types. duckdb-spatial provides DuckDB's `GEOMETRY` type, but it
is not bit-compatible with PostGIS's `GSERIALIZED`, and some MobilityDB
operators are defined on the PostGIS-side OID rather than on a generic interface.
Where a clean named-function path exists in MEOS, MobilityDuck registers the
named form; where the operator depended on PostGIS implementation details
without a generic MEOS equivalent, the operator is omitted.

This is the reason a few "geometry-X temporal" / "temporal-X geometry"
operator overloads listed in the MobilityDB manual do not appear in
`duckdb_functions()` for MobilityDuck. The corresponding named functions
(`atGeometry`, `minusGeometry`, `econtains`, `etouches`, …) are registered.

## `setSRID` / `asMFJSON` SRS resolution

MobilityDB resolves the SRS string for a given SRID via the PostgreSQL
`spatial_ref_sys` catalog table from inside C-level functions
(`getSRSbySRID(fcinfo, srid, …)` in `mobilitydb/src/temporal/type_out.c`).
MEOS does not export an equivalent SRS-by-SRID lookup.

Consequence: `asMFJSON(tgeompoint, ...)` in MobilityDuck always passes `NULL`
for the SRS argument to `temporal_as_mfjson`, so the resulting JSON has no
`crs` member regardless of the input's SRID. The SRID itself is preserved in
the input value and round-trips through `asWKB` / `asHexWKB` correctly.

If an SRS lookup is later exposed by MEOS (or if MobilityDuck adds its own
PROJ-backed lookup), `Tspatial_as_mfjson` in
`src/geo/tgeompoint_functions.cpp` is the single place to wire it through;
the rest of the MFJSON formatting is already MEOS-side.

## Extension-load model: statically-linked shell

The MobilityDuck CMake build produces both a loadable
`mobilityduck.duckdb_extension` and a `duckdb` shell binary that statically
links the extension at build time. When testing local changes:

* `LOAD '/path/...mobilityduck.duckdb_extension'` against the project-built
  `build/release/duckdb` is a **no-op** — the shell already has mobilityduck
  initialized statically; the dynamic load is short-circuited.
* To pick up extension code changes, rebuild the `shell` cmake target (which
  produces `build/release/duckdb`), not just `mobilityduck_loadable_extension`.
* Alternatively, run against a stock DuckDB CLI (one not built from this
  repository) where mobilityduck is genuinely loadable.

This is a property of the local development build, not a limitation of the
extension itself. End users who install MobilityDuck via the standard
extension repository or via `INSTALL FROM ...; LOAD mobilityduck;` get the
expected dynamic-load semantics.

## Normalization

Per the project convention, every user-visible result is canonicalized
(adjacent single-value spans collapsed, redundant boundary equality removed,
etc.). The MobilityDB SQL regression suite occasionally encodes the
non-normalized internal form as the expected output — MobilityDuck deviates
from those specific cases by design. This is not a parity gap to close on the
MobilityDuck side; the regression files are the artifacts that should track
the user-facing convention.

## Out of scope (deferred upstream work)

These rows are not DuckDB-specific gaps but appear here for completeness so
they aren't filed as open parity tasks:

* **`merge(temporal)` aggregate** — pending MobilityDB PR #823 exporting
  `temporal_merge_transfn` / `temporal_merge_combinefn` from MEOS public API.
  MobilityDuck-side wiring is ~30 lines once the upstream lands.
* **`asGeoJSON(temporal)`** — MEOS exports `geo_as_geojson` for static
  geometries but no `temporal_as_geojson`. Either upstream MEOS adds a
  per-temporal helper, or MobilityDuck builds one out of per-instant
  `geo_as_geojson` calls. Punted until there's a concrete consumer.
* **th3index** — MEOS-side T3D-RTree variant, not yet stable upstream;
  MobilityDuck side has nothing to wire until then.
* **cbuffer / json / rgeo / pose temporal types** — under active development
  upstream; intentionally not in MobilityDuck's parity scope yet.
