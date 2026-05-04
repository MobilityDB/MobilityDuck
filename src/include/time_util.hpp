#pragma once

#include "meos_wrapper_simple.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/common/types/timestamp.hpp"

namespace duckdb {

constexpr int32_t EPOCH_OFFSET_DAYS = 10957;
constexpr int64_t EPOCH_OFFSET_MICROS = EPOCH_OFFSET_DAYS * 86400000000;

// DuckDB → MEOS
inline int32_t ToMeosDate(duckdb::date_t d) {
    return static_cast<int32_t>(d) - EPOCH_OFFSET_DAYS;
}

inline int64_t ToMeosTimestamp(duckdb::timestamp_t ts) {
    // return ts.value - EPOCH_OFFSET_MICROS;
    return Timestamp::GetEpochMicroSeconds(ts) - EPOCH_OFFSET_MICROS;
}

// MEOS → DuckDB
inline duckdb::date_t FromMeosDate(int32_t pg_date) {
    return duckdb::date_t(pg_date + EPOCH_OFFSET_DAYS);
}

inline duckdb::timestamp_t FromMeosTimestamp(int64_t pg_ts) {    
    // return duckdb::timestamp_t(pg_ts + EPOCH_OFFSET_MICROS);
    return Timestamp::FromEpochMicroSeconds(pg_ts + EPOCH_OFFSET_MICROS);
}

inline interval_t IntervalToIntervalt(MeosInterval *interval) {
    interval_t duckdb_interval;
    duckdb_interval.months = interval->month;
    duckdb_interval.days = interval->day;
    duckdb_interval.micros = interval->time;
    return duckdb_interval;
}

inline MeosInterval IntervaltToInterval(interval_t duckdb_interval) {
    MeosInterval meos_interval;
    meos_interval.time = duckdb_interval.micros;
    meos_interval.day = duckdb_interval.days;
    meos_interval.month = duckdb_interval.months;
    return meos_interval;
}

inline timestamp_tz_t DuckDBToMeosTimestamp(timestamp_tz_t duckdb_ts) {
    timestamp_tz_t meos_ts;
    meos_ts.value = duckdb_ts.value - EPOCH_OFFSET_MICROS;
    return meos_ts;
}

inline timestamp_tz_t MeosToDuckDBTimestamp(timestamp_tz_t meos_ts) {
    timestamp_tz_t duckdb_ts;
    duckdb_ts.value = meos_ts.value + EPOCH_OFFSET_MICROS;
    return duckdb_ts;
}

// Convert a MEOS temporal distance in seconds (double) to a DuckDB interval_t.
// Days component is extracted first so that exact multiples of 86400 s display
// as "N days" rather than "NNN:00:00".
inline interval_t SecsToInterval(double secs) {
    interval_t iv;
    iv.months = 0;
    iv.days   = static_cast<int32_t>(secs / 86400.0);
    iv.micros = static_cast<int64_t>((secs - iv.days * 86400.0) * 1e6 + 0.5);
    return iv;
}

}