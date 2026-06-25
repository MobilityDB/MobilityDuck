# MobilityDuck generation — the canonical per-binding generator policy

This document is the contract for how MobilityDuck is generated, under the ecosystem-wide
per-binding generator policy.

## The policy (ecosystem-wide)

Every MobilityDB language/surface binding is a **pure projection of the MEOS-API catalog**,
and **each binding owns its own generator, in its own repo**, in a canonical layout. The
single source of truth is the **catalog** (`MEOS-API/output/meos-idl.json`, generated from
the MEOS C headers). A binding is an independent, plug-and-play module that owns its
generation.

Each binding repo satisfies the same invariants: in-repo generator; own
`tools/pin/compose-order.txt`; vendored/pinned catalog; thin language projection
(language-neutral decisions live in the catalog); full automation toward a zero-hand-written
surface (generate-then-retire; the last green-CI version is the equivalence probe).

## Current state and the canonical target

MobilityDuck's UDF layer is **hand-written C++** today. The canonical target is to **generate
the DuckDB scalar UDFs from `meos-idl.json`** via an in-repo generator
(`tools/codegen_duck_udfs.py`), organized **by `@ingroup` group** (one registration unit per
group, the same structure as the MEOS reference manual and the JMEOS/Spark generators),
family-gated by `#ifdef MEOS_ENABLE_<FAMILY>` emitted from catalog metadata. Marshalling
crosses MEOS values in-process as DuckDB `BLOB` (`BlobToTemporal` / `TemporalToBlob`, and the
per-family `BlobTo<X>` helpers), with the per-thread MEOS-init guard asserted in every
emitted body.

## Generate-then-retire — the green-CI version is the probe

The hand-written UDFs are replaced **family by family, never wipe-first**:

1. generate the full surface, build the extension green;
2. **prove generated ⊇ hand** against the **last green-CI version** (the equivalence probe)
   — `scripts/parity-audit.py` + the full sqllogictest suite + the BerlinMOD benchmark;
3. retire the hand registrations for that family (drop the coexistence prefix);
4. repeat. End state: zero hand-registered scalar UDFs; only non-generatable hand code
   survives, each justified (type registration, casts, aggregates, table functions). As the
   generated surface lands, most of the hand feature PRs in `tools/pin/compose-order.txt` are
   mooted.

## Pinning

The MEOS surface (vcpkg portfile + the vendored `meos-idl.json`) is pinned to a MobilityDB
`ecosystem-pin-*`. That pin is the *catalog/surface* input; MobilityDuck's own
`tools/pin/compose-order.txt` governs *this repo's* PR accumulate. See it for the composing
set and the disposition of every open PR.
