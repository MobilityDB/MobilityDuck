# Timezone-neutral testing policy — MEOS ecosystem

**Applies to:** MobilityDB, MobilityDuck, PyMEOS, JMEOS, meos-rs, and any other binding or tool using MEOS.

## Problem

MEOS formats timestamps using the PostgreSQL-derived internal timezone, which is
thread-local and defaults to the system/POSIX timezone. A test that hardcodes a
timezone offset in its expected value will fail on any machine where the system
timezone differs:

```sql
-- Breaks on UTC+1, UTC+2, etc.
SELECT tint '[1@2000-01-01]'::VARCHAR
----
[1@2000-01-01 00:00:00+00]   ← fails if system is UTC+1
```

DuckDB's `SET TimeZone` does not propagate to MEOS; MEOS has its own TLS timezone
state. Forcing MEOS to UTC via a wrapper hack (e.g., `meos_initialize_timezone("UTC")`
per thread in the execution wrapper) is a non-starter: it breaks local-time display for
users and couples test design to implementation internals.

## Rule

**Never hardcode timezone offsets** (`+00`, `+01`, `+02`, …, `-05`, …) in the expected
value of any test, in any project in the MEOS ecosystem.

## Allowed patterns

### 1. Numeric value accessors (best for scalar temporal types)

```sql
-- tint, tfloat, tbool
SELECT minValue(tint '[1@2000-01-01, 2@2000-01-02, 3@2000-01-03]')  -- 1
SELECT maxValue(tint '[1@2000-01-01, 2@2000-01-02, 3@2000-01-03]')  -- 3
SELECT startValue(tbool '[t@2000-01-01, f@2000-01-02]')              -- true
SELECT endValue(tbool '[t@2000-01-01, f@2000-01-02]')                -- false
SELECT round(minValue(tfloat '[1.5@2000-01-01, 3.5@2000-01-02]'), 6) -- 1.5
```

### 2. Duration / interval (no timezone)

```sql
SELECT duration(tint '[1@2000-01-01, 2@2000-01-02, 3@2000-01-03]')  -- 2 days
SELECT duration(ttext '[hello@2000-01-01, world@2000-01-02]')        -- 1 day
```

### 3. Nosort cross-validation (for round-trips and string representation)

Both queries produce the same output (same MEOS timezone on both sides), so the
comparison is TZ-neutral even if the strings contain offsets:

```sql
query T nosort label
SELECT tgeompoint '[POINT(1 2)@2026-01-01 00:00:00+00, ...]'::VARCHAR

query T nosort label
SELECT tgeompointFromBinary(asBinary(tgeompoint '[POINT(1 2)@2026-01-01 00:00:00+00, ...]'))::VARCHAR
```

### 4. BLOB byte comparison (for metadata round-trips)

```sql
-- ✓ Compare BLOB content directly — no encoding/escaping issues
SELECT value = expected_json_string::BLOB AS ok
FROM parquet_kv_metadata(file)
WHERE key = 'my_key'::BLOB
```

### 5. Explicit +00 in INPUT (not expected output)

Using `+00` in input literals is fine — it explicitly anchors the timestamp to UTC
regardless of the server timezone:

```sql
-- ✓ Input with explicit UTC anchor
COPY (SELECT asBinary(tgeompoint '[POINT(1 2)@2026-01-01 00:00:00+00]') AS traj)
TO 'file.parquet' (FORMAT PARQUET)
```

This does NOT make the string output TZ-neutral; it only pins what absolute time
is stored. The display will still use the server's timezone.

## Patterns to avoid

```sql
-- ✗ Hardcoded UTC offset — fails on non-UTC systems
SELECT tint '[1@2000-01-01]'::VARCHAR
----
[1@2000-01-01 00:00:00+00]

-- ✗ Hardcoded local offset — fails everywhere except that exact TZ
SELECT tint '[1@2000-01-01]'::VARCHAR
----
[1@2000-01-01 00:00:00+01]

-- ✗ Forcing MEOS timezone in extension code — breaks user expectations
meos_initialize_timezone("UTC");  // per-thread in wrapper → bad
```

## Migration status (2026-05-07)

| File / test suite | Status |
|---|---|
| `test/sql/parquet/temporal_parquet.test` | ✓ Fully TZ-neutral |
| `test/sql/parquet/` (new tests) | ✓ Policy in force |
| `test/sql/parity/*.test` | ✗ Still use `+00` — needs sweep |
| `test/sql/tint.test`, `tfloat.test`, … | ✗ Still use `+00` — needs sweep |
| `test/sql/tgeompoint.test`, `tgeometry.test` | ✗ Still use `+00` — needs sweep |
| `test/sql/stbox.test` | ✗ Still use `+00` — needs sweep |

The parity tests were generated under the assumption `TZ=UTC`. The sweep must rewrite
every timestamp-bearing expected value to use the patterns above.
