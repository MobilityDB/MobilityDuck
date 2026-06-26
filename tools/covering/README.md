# Covering-column generator (TemporalParquet)

MobilityDuck's projection of the MEOS catalog `temporalCovering` descriptor: the
DuckDB macros that materialise the [TemporalParquet][tp] **covering columns** of a
temporal value, so a table prunes at the Parquet row-group and Iceberg manifest
level. The covering schema is generated from the single catalog source of truth,
not hand-written per type.

[tp]: https://github.com/MobilityDB/MobilityLakehouse

## Files

| File | Role |
| --- | --- |
| `temporal-covering.json` | the descriptor, vendored from MEOS-API (the source of truth) |
| `codegen_covering.py` | the generator: descriptor → `covering.sql` |
| `covering.sql` | generated macros — **do not edit by hand** |
| `test_covering.sql` | loads the macros and checks they materialise the columns |

## Use

```sql
.read tools/covering/covering.sql

-- materialise the covering columns next to the lossless value
COPY (
  SELECT mmsi, asBinary(traj) AS traj, c.*
  FROM (SELECT mmsi, traj, covering_spatial(traj) AS c FROM trajectories)
) TO 'shard.parquet' (FORMAT PARQUET);
```

`covering_spatial` covers the spatial temporal types (STBOX: `xmin..zmax`,
`tmin/tmax`, `srid`); `covering_number` covers the numeric ones (TBOX:
`vmin/vmax`, `tmin/tmax`); `covering_timeOnly` covers the time-only ones
(`tbool`, `ttext`: `tmin/tmax`, no spatial box). The type-to-macro mapping is
listed at the foot of `covering.sql`. `zmin/zmax` are `NULL` for 2D values.

## Regenerate

```bash
python3 tools/covering/codegen_covering.py
```

## Status

The generator is pin-independent (it reads the descriptor JSON), and the emitted
macros use box accessors already present in the extension, so `covering.sql` runs
on the current MobilityDuck build today. To land this as the covering surface:

1. vendor `temporal-covering.json` from the merged MEOS-API descriptor,
2. wire `covering.sql` into the extension's load-time SQL,
3. re-verify against the pinned `libmeos`.
