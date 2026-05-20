# Timezone testing policy — MEOS ecosystem

**Applies to:** MobilityDB, MobilityDuck, PyMEOS, JMEOS, meos-rs, and any other binding or tool using MEOS.

## Problem

MEOS formats timestamps using the PostgreSQL-derived internal timezone, which is
thread-local and defaults to the system/POSIX timezone. A test that hardcodes the
UTC offset `+00` in its expected value will fail on any machine where the system
timezone differs:

```sql
-- Written on a UTC machine; breaks on UTC+1, UTC+2, etc.
SELECT tint '[1@2000-01-01]'::TEXT
----
[1@2000-01-01 00:00:00+00]   ← passes only on UTC systems
```

`+00` is the worst choice because it looks like "no offset" but is actually a
hardcoded assumption. Any other fixed offset (`+01`, `-08`) at least makes it
obvious that a specific timezone is required.

## The root fix: pin the test timezone to something non-UTC

MEOS reads the `TZ` environment variable at first initialisation
(via `select_default_timezone → getenv("TZ")`). Setting it before the test
process starts is the correct, lightweight fix — no code changes, no per-thread
hacks, no effect on production behaviour.

```
TZ=Europe/Brussels   # UTC+1 winter / UTC+2 summer
```

This is directly analogous to PostgreSQL's own practice of using
`America/Los_Angeles` (`PST8PDT`, UTC-8/UTC-7) for its regression tests.
Non-UTC offsets matter because they expose bugs that UTC silently hides
(sign errors, DST boundary logic, off-by-an-hour conversions, etc.).

## Platform-specific approach

Different test frameworks have different capabilities; apply the right
tool for each.

### pg_regress (MobilityDB PostgreSQL)

pg_regress compares plain-text expected files line by line. There is no
programmatic comparison hook, so hardcoded offsets are **unavoidable**.
The correct approach is:

1. Set `PGTZ=Europe/Brussels` (or the project's chosen zone) in the regress
   environment.
2. Use only winter dates in test fixtures to get a stable `+01` offset — avoid
   dates that cross DST boundaries.
3. Expected files contain `+01` consistently; CI sets the same env variable.

### DuckDB sqllogictest (MobilityDuck)

Two approaches are available — prefer them in the order listed:

#### a) Numeric/boolean value accessors (first choice)

Accessor functions return non-timestamp types; the result never contains an
offset at all:

```sql
SELECT minValue(tint '[1@2000-01-01, 2@2000-01-02, 3@2000-01-03]')  -- 1
SELECT maxValue(tint '[1@2000-01-01, 2@2000-01-02, 3@2000-01-03]')  -- 3
SELECT startValue(tbool '[t@2000-01-01, f@2000-01-02]')              -- true
SELECT endValue(tbool '[t@2000-01-01, f@2000-01-02]')                -- false
SELECT round(minValue(tfloat '[1.5@2000-01-01, 3.5@2000-01-02]'), 6) -- 1.5
SELECT duration(ttext '[hello@2000-01-01, world@2000-01-02]')        -- 1 day
```

#### b) Nosort cross-validation (DuckDB sqllogictest only)

Both queries go through MEOS at the same timezone, so even if their output
contains an offset, the two sides are always equal — the comparison is
TZ-neutral:

```sql
query IT nosort label
SELECT id, tintFromBinary(val)::VARCHAR FROM tbl ORDER BY id

query IT nosort label
SELECT id, tintFromBinary(asBinary(tintFromBinary(val)))::VARCHAR FROM tbl ORDER BY id
```

This pattern is **not portable** to pg_regress, pytest, JUnit, or Spark tests.

#### c) BLOB byte comparison (for metadata round-trips)

```sql
SELECT value = expected_json_string::BLOB AS ok
FROM parquet_kv_metadata(file)
WHERE key = 'my_key'::BLOB
```

### pytest (PyMEOS)

Use Python accessor methods that return non-timestamp types:

```python
assert t.min_value() == 1
assert t.max_value() == 3
assert t.start_value() == True
assert t.duration() == timedelta(days=2)
```

### JUnit / Kotlin (JMEOS)

Same principle — compare domain values, not formatted strings:

```java
assertEquals(1, t.minValue());
assertEquals(3, t.maxValue());
assertEquals(Duration.ofDays(2), t.duration());
```

### Spark

Set `spark.sql.session.timeZone` to the project's chosen zone and use that
offset consistently in expected strings, or extract domain values with the
MEOS UDF equivalents before asserting.

## What NOT to do

```sql
-- ✗ Hardcoded UTC offset — fails on non-UTC systems
SELECT tint '[1@2000-01-01]'::TEXT
----
[1@2000-01-01 00:00:00+00]

-- ✗ Hardcoded single-TZ offset — fails everywhere else
SELECT tint '[1@2000-01-01]'::TEXT
----
[1@2000-01-01 00:00:00+01]

-- ✗ Forcing MEOS timezone in extension/binding code — breaks users
meos_initialize_timezone("UTC");  // per-thread in DuckDB wrapper → bad

-- ✗ DuckDB-only nosort used in pg_regress or pytest test
```

## Using +00 in INPUT (always fine)

Using `+00` in **input** literals anchors the absolute UTC time regardless of
the display timezone. This is correct and encouraged:

```sql
-- ✓ Input literal anchors to UTC; only the *display* will shift with TZ
COPY (SELECT asBinary(tgeompoint '[POINT(1 2)@2026-01-01 00:00:00+00]') AS traj)
TO 'file.parquet' (FORMAT PARQUET)
```

## Migration status (2026-05-07)

| File / test suite | Framework | Status |
|---|---|---|
| `test/sql/parquet/temporal_parquet.test` | DuckDB sqllogictest | ✓ Fully TZ-neutral (accessors + nosort) |
| `test/sql/parquet/` (new tests) | DuckDB sqllogictest | ✓ Policy in force |
| `test/sql/parity/*.test` | DuckDB sqllogictest | ✗ Still use `+00` — needs `TZ=Europe/Brussels` sweep |
| `test/sql/tint.test`, `tfloat.test`, … | DuckDB sqllogictest | ✗ Still use `+00` — needs sweep |
| `test/sql/tgeompoint.test`, `tgeometry.test` | DuckDB sqllogictest | ✗ Still use `+00` — needs sweep |
| `test/sql/stbox.test` | DuckDB sqllogictest | ✗ Still use `+00` — needs sweep |
| MobilityDB `expected/*.out` | pg_regress | ✓ Uses America/Los_Angeles (PST8PDT) — existing approach is correct |
| PyMEOS tests | pytest | ✗ Audit needed — accessor approach may not be consistently applied |
