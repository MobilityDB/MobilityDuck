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

/* The casts between the time types of DuckDB and those of MEOS. Each converts through a MEOS
 * conversion of the counts from the Unix epoch, which DuckDB's types count from too, and builds
 * the value with the constructor of its type, as the casts of the other bindings do: a value
 * MEOS cannot state raises its error, which MobilityduckMeosErrorHandler throws. DuckDB and MEOS
 * both hold the infinities of a timestamp and a date, each as its own count, so an infinity maps
 * to the other side's through date_is_finite and timestamp_is_finite and MEOS's own reading of
 * 'infinity', never through a count. */

/* The infinity of a MEOS timestamp, as MEOS reads it */
inline TimestampTz MeosTimestampInfinity(bool positive) {
    static const TimestampTz pos = timestamptz_in("infinity", -1);
    static const TimestampTz neg = timestamptz_in("-infinity", -1);
    return positive ? pos : neg;
}

/* The infinity of a MEOS date, as MEOS reads it */
inline DateADT MeosDateInfinity(bool positive) {
    static const DateADT pos = date_in("infinity");
    static const DateADT neg = date_in("-infinity");
    return positive ? pos : neg;
}

// DuckDB → MEOS
inline int32_t ToMeosDate(duckdb::date_t d) {
    if (!Date::IsFinite(d))
        return MeosDateInfinity(d == date_t::infinity());
    return unix_days_to_date(d.days);
}

inline int64_t ToMeosTimestamp(duckdb::timestamp_t ts) {
    if (!Timestamp::IsFinite(ts))
        return MeosTimestampInfinity(ts == timestamp_t::infinity());
    return unix_us_to_timestamptz(Timestamp::GetEpochMicroSeconds(ts));
}

// MEOS → DuckDB
inline duckdb::date_t FromMeosDate(int32_t meos_date) {
    if (!date_is_finite(meos_date))
        return meos_date > 0 ? date_t::infinity() : date_t::ninfinity();
    return date_t(date_to_unix_days(meos_date));
}

inline duckdb::timestamp_t FromMeosTimestamp(int64_t meos_ts) {
    if (!timestamp_is_finite(meos_ts))
        return meos_ts > 0 ? timestamp_t::infinity() : timestamp_t::ninfinity();
    return Timestamp::FromEpochMicroSeconds(timestamptz_to_unix_us(meos_ts));
}

/* A MEOS interval as a DuckDB interval_t, through interval_to_parts: both hold months, days and
 * microseconds */
inline interval_t IntervalToIntervalt(MeosInterval *interval) {
    int32 months, days;
    int64 micros;
    interval_to_parts(interval, &months, &days, &micros);
    interval_t duckdb_interval;
    duckdb_interval.months = months;
    duckdb_interval.days = days;
    duckdb_interval.micros = micros;
    return duckdb_interval;
}

/* A DuckDB interval_t as a MEOS interval, through interval_from_parts */
inline MeosInterval IntervaltToInterval(interval_t duckdb_interval) {
    MeosInterval *p = interval_from_parts(duckdb_interval.months, duckdb_interval.days,
        duckdb_interval.micros);
    MeosInterval meos_interval = *p;
    free(p);
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

/* A DuckDB timestamp_tz_t as the MEOS timestamptz a generated call passes, through
 * #ToMeosTimestamp */
inline timestamp_tz_t DuckDBToMeosTimestamp(timestamp_tz_t duckdb_ts) {
    timestamp_tz_t meos_ts;
    meos_ts.value = ToMeosTimestamp(timestamp_t(duckdb_ts.value));
    return meos_ts;
}

/* A MEOS timestamptz as a DuckDB timestamp_tz_t, through #FromMeosTimestamp */
inline timestamp_tz_t MeosToDuckDBTimestamp(timestamp_tz_t meos_ts) {
    timestamp_tz_t duckdb_ts;
    duckdb_ts.value = FromMeosTimestamp(meos_ts.value).value;
    return duckdb_ts;
}

}
