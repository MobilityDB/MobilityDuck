# MEOS-DuckDB Type Conversion

## Overview
Utility functions to resolve type conflicts between DuckDB and MEOS (Mobility and Time-Evolving Objects) systems. The main issue is different epoch references:
- **DuckDB**: Uses January 1, 2000 as epoch (day 0)
- **MEOS/PostgreSQL**: Uses January 1, 1970 as epoch (day 0)

This creates a 30-year offset that must be handled in all temporal conversions.

## Constants
```cpp
constexpr int32_t EPOCH_OFFSET_DAYS = 10957;        // Days between 1970-01-01 and 2000-01-01
constexpr int64_t EPOCH_OFFSET_MICROS = ...;        // Same offset in microseconds
```

## Date and Timestamp Functions

### DuckDB → MEOS Conversion
- `ToMeosDate(duckdb::date_t d)` → `int32_t`
  - Converts DuckDB date to MEOS format by subtracting epoch offset
- `ToMeosTimestamp(duckdb::timestamp_t ts)` → `int64_t`
  - Converts DuckDB timestamp to MEOS format by subtracting epoch offset

### MEOS → DuckDB Conversion  
- `FromMeosDate(int32_t pg_date)` → `duckdb::date_t`
  - Converts MEOS date to DuckDB format by adding epoch offset
- `FromMeosTimestamp(int64_t pg_ts)` → `duckdb::timestamp_t`
  - Converts MEOS timestamp to DuckDB format by adding epoch offset

## Timezone-Aware Timestamp Functions
- `DuckDBToMeosTimestamp(timestamp_tz_t)` → `timestamp_tz_t`
  - Converts DuckDB timezone-aware timestamp to MEOS format
- `MeosToDuckDBTimestamp(timestamp_tz_t)` → `timestamp_tz_t`  
  - Converts MEOS timezone-aware timestamp to DuckDB format

## Interval Functions
Intervals represent durations and don't need epoch adjustment, only field mapping:

- `IntervalToIntervalt(MeosInterval *interval)` → `interval_t`
  - Maps MEOS interval fields (`month`, `day`, `time`) to DuckDB (`months`, `days`, `micros`)
- `IntervaltToInterval(interval_t duckdb_interval)` → `MeosInterval`
  - Maps DuckDB interval fields to MEOS format

## Key Points
- All functions are `inline` for performance
- Temporal types require epoch offset adjustment (±10957 days)
- Intervals only need field name mapping (no epoch adjustment)
- Functions are in the `duckdb` namespace

## Usage Examples
```cpp
// Date conversion
duckdb::date_t duck_date = duckdb::Date::FromString("2023-01-01");
int32_t meos_date = ToMeosDate(duck_date);
duckdb::date_t converted_back = FromMeosDate(meos_date);

// Interval conversion (no epoch offset)
interval_t duck_interval = {1, 30, 3600000000};  // 1 month, 30 days, 1 hour
MeosInterval meos_interval = IntervaltToInterval(duck_interval);
```