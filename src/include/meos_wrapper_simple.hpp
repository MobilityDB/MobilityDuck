#pragma once

#include <cstddef>
#include <cstdint>
// Include MEOS headers
extern "C" {
    #include <meos.h>
    #include <meos_geo.h>
    #include <meos_internal.h>
    #include <meos_internal_geo.h>
    // Per-family public headers (alphabetical, per the family-block convention):
    // family type declarations and cross-family conversions (e.g.
    // tcbuffer_to_tfloat, ttext_to_tjsonb, tbigint_to_tquadbin) live here, not in
    // the meos.h umbrella. The port builds every family the generated UDFs call
    // into. meos_pointcloud.h must be wrapped here (C linkage) now that the
    // pointcloud value surface (tpcpoint/tpcpatch startValue/atValue/…) generates
    // and links against libmeos. (Alphabetical also keeps meos_pose.h before
    // meos_rgeo.h, which depends on it.)
    #include <meos_cbuffer.h>
    #include <meos_h3.h>
    #include <meos_json.h>
    #include <meos_npoint.h>
    #include <meos_pointcloud.h>
    #include <meos_pose.h>
    #include <meos_quadbin.h>
    #include <meos_rgeo.h>
    // PG-compat date/timestamp arithmetic (add_date_int, add_timestamptz_interval)
    // lives in the pgtypes public leaf headers, not the meos.h umbrella.
    #include <pg_date.h>
    #include <pg_timestamp.h>
}

// Create explicit aliases for MEOS types
// Use the original MEOS type names but with explicit qualification
using MeosInterval = ::Interval;  // Explicitly use global MEOS Interval
using MeosTimestamp = ::Timestamp;  // Explicitly use global MEOS Timestamp 
