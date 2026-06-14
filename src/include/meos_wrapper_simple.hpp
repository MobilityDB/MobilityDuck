#pragma once

#include <cstddef>
#include <cstdint>
// Include MEOS headers
extern "C" {
    #include <meos.h>
    #include <meos_geo.h>
    #include <meos_internal.h>
    #include <meos_internal_geo.h>
}

// The reverse-argument (box/span-first) overloads of the symmetric box/span ×
// temporal topological predicates — overlaps / same / adjacent for tstzspan,
// stbox, numspan and tbox against a temporal — are provided directly by MEOS as
// of ecosystem-pin-2026-06-14c (declared in <meos.h> / <meos_geo.h>), so the
// binding uses them from MEOS.  The former static-inline commutativity polyfills
// were removed here: they now clash with the MEOS declarations ("declared
// 'extern' and later 'static'").  The non-symmetric predicates
// (contains/contained/overbefore/overafter) keep both MEOS directions.

// Create explicit aliases for MEOS types
// Use the original MEOS type names but with explicit qualification
using MeosInterval = ::Interval;  // Explicitly use global MEOS Interval
using MeosTimestamp = ::Timestamp;  // Explicitly use global MEOS Timestamp
