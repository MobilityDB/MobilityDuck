/*-------------------------------------------------------------------------
 *
 * timestamp.h
 *	  Definitions for the SQL "timestamp" and "interval" types.
 *
 * Portions Copyright (c) 1996-2021, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/utils/timestamp.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef TIMESTAMP_H
#define TIMESTAMP_H

// MEOS
// #include "datatype/timestamp.h"
#include "utils/timestamp_def.h"
#include "pgtime.h"

/*
 * Macros for fmgr-callable functions.
 *
 * For Timestamp, we make use of the same support routines as for int64.
 * Therefore Timestamp is pass-by-reference if and only if int64 is!
 */
#define DatumGetTimestamp(X)  ((MeosTimestamp) DatumGetInt64(X))
#define DatumGetTimestampTz(X)	((TimestampTz) DatumGetInt64(X))
#define DatumGetIntervalP(X)  ((MeosInterval *) DatumGetPointer(X))

#define TimestampGetDatum(X) Int64GetDatum(X)
#define TimestampTzGetDatum(X) Int64GetDatum(X)
#define MeosIntervalPGetDatum(X) PointerGetDatum(X)

#define PG_GETARG_TIMESTAMP(n) DatumGetTimestamp(PG_GETARG_DATUM(n))
#define PG_GETARG_TIMESTAMPTZ(n) DatumGetTimestampTz(PG_GETARG_DATUM(n))
#define PG_GETARG_INTERVAL_P(n) DatumGetIntervalP(PG_GETARG_DATUM(n))

#define PG_RETURN_TIMESTAMP(x) return TimestampGetDatum(x)
#define PG_RETURN_TIMESTAMPTZ(x) return TimestampTzGetDatum(x)
#define PG_RETURN_INTERVAL_P(x) return MeosIntervalPGetDatum(x)


#define TIMESTAMP_MASK(b) (1 << (b))
#define INTERVAL_MASK(b) (1 << (b))

/* Macros to handle packing and unpacking the typmod field for intervals */
#define INTERVAL_FULL_RANGE (0x7FFF)
#define INTERVAL_RANGE_MASK (0x7FFF)
#define INTERVAL_FULL_PRECISION (0xFFFF)
#define INTERVAL_PRECISION_MASK (0xFFFF)
#define INTERVAL_TYPMOD(p,r) ((((r) & INTERVAL_RANGE_MASK) << 16) | ((p) & INTERVAL_PRECISION_MASK))
#define INTERVAL_PRECISION(t) ((t) & INTERVAL_PRECISION_MASK)
#define INTERVAL_RANGE(t) (((t) >> 16) & INTERVAL_RANGE_MASK)

#define TimestampTzPlusMilliseconds(tz,ms) ((tz) + ((ms) * (int64) 1000))

/* Set at postmaster start */
extern TimestampTz PgStartTime;

/* Set at configuration reload */
extern TimestampTz PgReloadTime;

/* Internal routines (not fmgr-callable) */

extern int32 anytimestamp_typmod_check(bool istz, int32 typmod);

extern TimestampTz GetCurrentTimestamp(void);
extern TimestampTz GetSQLCurrentTimestamp(int32 typmod);
extern MeosTimestamp GetSQLLocalTimestamp(int32 typmod);
extern void TimestampDifference(TimestampTz start_time, TimestampTz stop_time,
								long *secs, int *microsecs);
extern long TimestampDifferenceMilliseconds(TimestampTz start_time,
											TimestampTz stop_time);
extern bool TimestampDifferenceExceeds(TimestampTz start_time,
									   TimestampTz stop_time,
									   int msec);

extern TimestampTz time_t_to_timestamptz(pg_time_t tm);
extern pg_time_t timestamptz_to_time_t(TimestampTz t);

extern const char *timestamptz_to_str(TimestampTz t);

extern int	tm2timestamp(struct pg_tm *tm, fsec_t fsec, int *tzp, MeosTimestamp *dt);
extern int	timestamp2tm(MeosTimestamp dt, int *tzp, struct pg_tm *tm,
						 fsec_t *fsec, const char **tzn, pg_tz *attimezone);
extern void dt2time(MeosTimestamp dt, int *hour, int *min, int *sec, fsec_t *fsec);

extern int	interval2tm(MeosInterval span, struct pg_tm *tm, fsec_t *fsec);
extern int	tm2interval(struct pg_tm *tm, fsec_t fsec, MeosInterval *span);

extern MeosTimestamp SetEpochTimestamp(void);
extern void GetEpochTime(struct pg_tm *tm);

extern int	timestamp_cmp_internal(MeosTimestamp dt1, MeosTimestamp dt2);

/* timestamp comparison works for timestamptz also */
#define timestamptz_cmp_internal(dt1,dt2)	timestamp_cmp_internal(dt1, dt2)

extern TimestampTz timestamp2timestamptz_opt_overflow(MeosTimestamp timestamp,
													  int *overflow);
extern int32 timestamp_cmp_timestamptz_internal(MeosTimestamp timestampVal,
												TimestampTz dt2);

extern int	isoweek2j(int year, int week);
extern void isoweek2date(int woy, int *year, int *mon, int *mday);
extern void isoweekdate2date(int isoweek, int wday, int *year, int *mon, int *mday);
extern int	date2isoweek(int year, int mon, int mday);
extern int	date2isoyear(int year, int mon, int mday);
extern int	date2isoyearday(int year, int mon, int mday);

extern bool TimestampTimestampTzRequiresRewrite(void);

#endif							/* TIMESTAMP_H */
