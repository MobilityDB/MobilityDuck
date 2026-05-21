# Iceberg + Apache Polaris readiness for MobilityDuck's TemporalParquet substrate

This guide documents how MobilityDuck's TemporalParquet shards (the edge-to-cloud substrate of `feat/edge-to-cloud-quickstart-rebased` / PR #158) interoperate with Apache Iceberg via Apache Polaris as the catalog server. It is a **readiness doc**: the substrate works end-to-end with vanilla Iceberg today; this doc captures the recommended catalog flavor, the per-engine integration points, and the deployment recipe.

## Why Polaris

Apache Polaris (Snowflake 2024, Apache 2.0 licensed, incubating) is the recommended OSS Iceberg-REST catalog for the MobilityDB ecosystem:

| Requirement | Polaris | Alternatives |
|---|---|---|
| OSS Iceberg-REST protocol | ✅ vanilla, no vendor extensions | Nessie (also Apache 2.0); JDBC catalog (no REST surface) |
| Production-grade RBAC + auth | ✅ OAuth2 client credentials, role hierarchy, principal-based grants | Hive-style — coarse; Nessie — branch-based, semantically different |
| Multi-tenant isolation | ✅ catalog/role/principal-scoped | Nessie — git-style branches not a tenancy model |
| Credential vending | ✅ short-lived S3/GCS/Azure credentials per request | Engines hold static cloud credentials |
| Quarkus + Postgres stack | ✅ familiar JVM operability | Nessie uses RocksDB |
| Protocol-vanilla (engine-portable) | ✅ DuckDB / Spark / Polaris-aware engines all work | Nessie's GitOps requires per-engine wrapper |

**Net**: Polaris keeps the TemporalParquet stack **engine-portable** while adding production controls. Storing TemporalParquet shards in Iceberg tables via Polaris is the recommended deployment pattern for multi-team / cloud-storage / RBAC-required scenarios.

## How MobilityDuck's TemporalParquet substrate composes with Iceberg

TemporalParquet shards are **standard Parquet files** with a single side-channel: a footer `KV_METADATA` key named `temporal` describing each column's MEOS type, encoding, and CRS. **Iceberg / Polaris see the bytes as opaque** — they manage the file as any Parquet, partitioning / pruning / snapshotting it without ever interpreting the temporal payload.

```
┌─────────────────────────────────────────────────────────────────┐
│  Polaris (catalog)                                              │
│    namespace → table → snapshot → manifest → data files         │
│                                                            │    │
│                                                            ▼    │
│                                          ┌─────────────────────┐│
│                                          │ TemporalParquet     ││
│                                          │  shard (opaque)     ││
│                                          │    column foo: BLOB ││
│                                          │    KV: temporal=…   ││
│                                          └─────────────────────┘│
└─────────────────────────────────────────────────────────────────┘
            ▲                                       ▲
            │ REST / Iceberg                        │ direct read
            │                                       │ (DuckDB / Spark
            │                                       │  / PyMEOS load)
   ┌────────────────┐                       ┌────────────────────┐
   │ DuckDB         │                       │ MobilityDuck        │
   │ iceberg ext    │                       │ temporal_parquet UDFs│
   │  + spatial     │                       │  (temporalFooter)    │
   │  + temporal_iceberg_scan (planned)     │                      │
   └────────────────┘                       └────────────────────┘
```

The bbox sidecars MobilityDuck writes per column (e.g. `foo__bbox`) flow into Iceberg as ordinary columns. Iceberg's column statistics on the sidecars give **free manifest-level pruning** at the catalog layer — no temporal-aware predicate pushdown required at the engine. Adopters declare the bbox sidecars as `partition_spec` columns (or rely on file-level stats) to get this benefit automatically.

## Per-engine integration

### MobilityDuck

DuckDB has the [`iceberg` extension](https://duckdb.org/docs/extensions/iceberg.html) that speaks Iceberg-REST. MobilityDuck's planned `temporal_iceberg_scan` UDF is a thin delegator:

```sql
-- Today: a temporal Parquet file from local / S3 / etc.
SELECT *
FROM temporal_parquet_scan('s3://bucket/trips.parquet');

-- After temporal_iceberg_scan (~1 PR): the same shard via Polaris.
SELECT *
FROM temporal_iceberg_scan('rest://polaris.example.com/catalog/trips@snapshot=12345');
```

The UDF delegates the catalog lookup to `iceberg_scan` and the per-row decoding to the existing temporal-column logic in `temporal_parquet_scan`. ~1 PR scope.

### MobilitySpark

Spark's native Iceberg runtime + Polaris-Spark connector covers the catalog side without any MobilitySpark-specific work. MobilitySpark's existing temporal UDFs read the Parquet rows directly — Polaris adds zero new boundary. **Net cost**: ~0 PRs; Polaris readiness is documentation + deployment recipe.

### PyMEOS

PyMEOS #84's `pymeos.io.{to_arrow, from_arrow}` returns Arrow record batches. For Iceberg / Polaris access, the recommended pattern is:

```python
import polars as pl
from pymeos.io import from_arrow

# Polars natively reads from Iceberg / Polaris
df = pl.scan_iceberg("rest://polaris.example.com/catalog/trips").collect()

# Convert the temporal column to PyMEOS objects
trajectories = from_arrow(df.to_arrow(), column="trajectory")
```

No PyMEOS-side wrapper is needed; `pl.scan_iceberg` already handles Polaris. PyMEOS-Examples will ship a notebook demonstrating this.

## Snapshot-time vs valid-time orthogonality

Iceberg's snapshot model and MobilityDB's valid-time model are **orthogonal**:

| Axis | Iceberg | MobilityDB |
|---|---|---|
| When the row was *written* | snapshot timestamp | `_ingested_at` column (if present) |
| When the row's data was *true in the world* | (not modelled) | `tgeogpointSeq` periods |

Adopters can use Iceberg snapshots for **as-of-write queries** (audit, lineage, time-travel data versioning) and the MobilityDB temporal columns for **as-of-event queries** (where was vehicle X at time T) in the same table. The two never interfere.

## OAuth2 + storage credential vending

Polaris's credential vending eliminates static cloud credentials from query engines:

```yaml
# polaris-config.yaml (excerpt)
catalogs:
  - name: mobilitydb_prod
    storage_config:
      storage_type: S3
      role_arn: arn:aws:iam::123456789012:role/mobilitydb-iceberg
      external_id: random-string
principals:
  - name: duckdb-edge-tenant-acme
    grants: ["READ", "WRITE"]
    catalogs: ["mobilitydb_prod"]
```

A DuckDB tenant authenticates to Polaris via OAuth2 client credentials, requests catalog access, and Polaris vends short-lived S3 credentials scoped to the allowed paths. Engines never see long-lived cloud secrets.

## Deployment recipe

### Dev / single-node

```yaml
# docker-compose.yml (Polaris + Postgres + MinIO)
services:
  polaris:
    image: snowflakedb/apache-polaris:latest
    depends_on: [postgres, minio]
    environment:
      QUARKUS_DATASOURCE_JDBC_URL: jdbc:postgresql://postgres/polaris
      ...
    ports: ["8181:8181"]
  postgres: { image: postgres:16, ... }
  minio:    { image: minio/minio, ports: ["9000:9000"], ... }
```

### Production

- **Polaris cluster**: 2+ Quarkus nodes behind a load balancer, Postgres backing store (RDS / Cloud SQL / managed equivalent), TLS-terminating ingress.
- **Engines**: DuckDB / Spark / PyMEOS clients each register their OAuth2 client credentials at deploy time; tokens are short-lived and rotated automatically.
- **Storage**: TemporalParquet shards land on the cloud object store (S3 / GCS / Azure Blob) via Polaris-vended credentials. The bbox sidecars give Iceberg's manifest-level pruning a free index.

## Status + future work

| Item | Status |
|---|---|
| TemporalParquet + Iceberg interop (opaque BLOB column) | Works today with vanilla Iceberg via DuckDB `iceberg_scan` / Spark Iceberg runtime |
| `temporal_iceberg_scan` DuckDB UDF | Future MobilityDuck PR (~1 PR delegating to `iceberg_scan` + existing temporal-column logic) |
| MobilitySpark Polaris config | ~0 PRs; documentation only |
| PyMEOS-Examples notebook (Polars + Polaris + PyMEOS) | Future example |
| Polaris cluster deployment recipe | This document |
| OAuth2 credential vending end-to-end demo | Future PR (Docker compose + scripted teardown) |

See also: `docs/beta-testing-edge-to-cloud.md` (the substrate this composes with), the [Apache Polaris docs](https://polaris.apache.org/), the [DuckDB Iceberg extension docs](https://duckdb.org/docs/extensions/iceberg.html).
