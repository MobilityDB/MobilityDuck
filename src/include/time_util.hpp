/*****************************************************************************
 *
 * This MobilityDB code is provided under The PostgreSQL License.
 * Copyright (c) 2016-2026, Université libre de Bruxelles and MobilityDB
 * contributors
 *
 * MobilityDB includes portions of PostGIS version 3 source code released
 * under the GNU General Public License (GPLv2 or later).
 * Copyright (c) 2001-2026, PostGIS contributors
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without a written
 * agreement is hereby granted, provided that the above copyright notice and
 * this paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL UNIVERSITE LIBRE DE BRUXELLES BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
 * LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION,
 * EVEN IF UNIVERSITE LIBRE DE BRUXELLES HAS BEEN ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * UNIVERSITE LIBRE DE BRUXELLES SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON
 * AN "AS IS" BASIS, AND UNIVERSITE LIBRE DE BRUXELLES HAS NO OBLIGATIONS TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 *****************************************************************************/

#pragma once

#include "meos_wrapper_simple.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/common/types/timestamp.hpp"

namespace duckdb {

constexpr int32_t EPOCH_OFFSET_DAYS = 10957;
constexpr int64_t EPOCH_OFFSET_MICROS = EPOCH_OFFSET_DAYS * 86400000000;

// MEOS's DEFAULT_TIME_ORIGIN, the torigin MobilityDB's tiling, splitting and
// binning functions default to: '2000-01-03' (a Monday).
// In MEOS PG-epoch microseconds: 2 days * 86_400 * 1_000_000.
constexpr int64_t DEFAULT_TIME_ORIGIN_MEOS = 2LL * 86400LL * 1000000LL;

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

/* A DuckDB interval passed as the `const Interval *` a MEOS function reads: the unnamed object built
 * at the argument converts through #IntervaltToInterval and lives to the end of the call's full expression,
 * so a generated call reads `f(t, MeosIntervalArg(a2))` with no statement of its own, as
 * #DuckDBToMeosTimestamp marshals a timestamp argument in one expression */
struct MeosIntervalArg {
    MeosInterval v;
    explicit MeosIntervalArg(interval_t duckdb_interval) : v(IntervaltToInterval(duckdb_interval)) {}
    operator const MeosInterval *() const { return &v; }
};

/* An interval SQL literal (a parameter's DEFAULT '0 minutes') passed as the `const Interval *` a
 * MEOS function reads, parsed by the MEOS interval_in as the SQL default is, released at the end
 * of the call's full expression, as #MeosIntervalArg passes a column value */
struct MeosIntervalLiteral {
    MeosInterval *p;
    explicit MeosIntervalLiteral(const char *text) : p(interval_in(text, -1)) {}
    ~MeosIntervalLiteral() { free(p); }
    MeosIntervalLiteral(const MeosIntervalLiteral &) = delete;
    MeosIntervalLiteral &operator=(const MeosIntervalLiteral &) = delete;
    operator const MeosInterval *() const { return p; }
};

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

}
