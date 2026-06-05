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

// The symmetric box/span × temporal topological predicates (overlaps, same,
// adjacent) are canonicalized in MEOS to a single argument direction
// (temporal-first). The reverse-argument overloads the DuckDB surface exposes
// derive from that one kernel by commutativity (swap the operands). The
// non-symmetric predicates (contains/contained/overbefore/overafter) keep both
// MEOS directions and are used directly.
static inline bool overlaps_tstzspan_temporal(const Span *s, const Temporal *t) { return overlaps_temporal_tstzspan(t, s); }
static inline bool same_tstzspan_temporal    (const Span *s, const Temporal *t) { return same_temporal_tstzspan(t, s); }
static inline bool adjacent_tstzspan_temporal(const Span *s, const Temporal *t) { return adjacent_temporal_tstzspan(t, s); }
static inline bool overlaps_stbox_tspatial(const STBox *b, const Temporal *t) { return overlaps_tspatial_stbox(t, b); }
static inline bool same_stbox_tspatial    (const STBox *b, const Temporal *t) { return same_tspatial_stbox(t, b); }
static inline bool adjacent_stbox_tspatial(const STBox *b, const Temporal *t) { return adjacent_tspatial_stbox(t, b); }
static inline bool overlaps_numspan_tnumber(const Span *s, const Temporal *t) { return overlaps_tnumber_numspan(t, s); }
static inline bool same_numspan_tnumber    (const Span *s, const Temporal *t) { return same_tnumber_numspan(t, s); }
static inline bool adjacent_numspan_tnumber(const Span *s, const Temporal *t) { return adjacent_tnumber_numspan(t, s); }
static inline bool overlaps_tbox_tnumber(const TBox *b, const Temporal *t) { return overlaps_tnumber_tbox(t, b); }
static inline bool same_tbox_tnumber    (const TBox *b, const Temporal *t) { return same_tnumber_tbox(t, b); }
static inline bool adjacent_tbox_tnumber(const TBox *b, const Temporal *t) { return adjacent_tnumber_tbox(t, b); }

// Create explicit aliases for MEOS types
// Use the original MEOS type names but with explicit qualification
using MeosInterval = ::Interval;  // Explicitly use global MEOS Interval
using MeosTimestamp = ::Timestamp;  // Explicitly use global MEOS Timestamp
