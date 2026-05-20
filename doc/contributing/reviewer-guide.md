<!--
Copyright(c) MobilityDB Contributors

This documentation is licensed under a
Creative Commons Attribution-Share Alike 3.0 License
https://creativecommons.org/licenses/by-sa/3.0/
-->

# MobilityDuck PR Reviewer Guide

Quick reference for anyone reviewing open pull requests.
Updated in the same commit as any PR that changes PR state or adds new branches.
**Last updated: 2026-05-10 — 22 open PRs (net consolidation today): folds #115+#119 → #120, #116+#118 → #121, #122+#123+#124+#125 → #126.  Squashed in place: #117, #111.  PR #112 TZ-neutral test fix landed (1 commit).**

---

## How to find this guide

- **In the repo:** `doc/contributing/reviewer-guide.md`
- **Rule:** every commit that opens, closes, or restructures a PR must update this file in the same commit. A one-liner status change is enough; a fuller rewrite is needed when the dependency graph changes.

---

## CI legend

| Symbol | Meaning |
|--------|---------|
| ✅ | All checks green |
| ❌ | Real failure — needs investigation before review |
| ⏳ | CI running |
| ❓ | No CI result yet (doc-only, draft, or external PR) |
| ⚠️ | Non-blocking failure (e.g. macOS/Windows `continue-on-error`, Codacy ACTION_REQUIRED — maintainer overrides in UI) |

---

## Dependency chains — land in this order

```
#121  consolidate/main-hygiene-batch  (revert single-tz + sync MEOS API drift; subsumes #116+#118)
  └─► #111  fix/per-thread-meos-init  (thread-safety foundation)
        └─► #58   fix/splitnspans-spanset-result + aggregates batch (12 commits)
        └─► #61   fix/geomset-srid-parameter
        └─► #106  fix/span-arithmetic-correctness
        └─► #107  fix/span-distance-return-types
              └─► consolidate/ batch (#97–#105)     (parity surface — independent of each other)
                    └─► #108  feat/parity-math-similarity-tbox
                    └─► #109  feat/parity-elevation-restrict
                    └─► #110  feat/parity-split-complement
                    └─► #112  feat/wkb-roundtrip-all-types  (TZ-neutral test fix landed)
                    └─► #113  feat/edge-to-cloud-quickstart
                    └─► #114  feat/berlinmod-geo-functions2
                    └─► #126  feat/parity-additions-batch    (bearing + covers + stbox-dim + seqSetGaps; subsumes #122+#123+#124+#125)
                    └─► #120  consolidate/pr-coordination-and-tz-lint (subsumes #115+#119)
                    └─► #117  doc/reviewer-guide   (visibility wiring; cross-repo with MobilityDB #931 / MobilitySpark #8)
```

**#121 should land first** — it reverts single-timezone state on `main` and syncs MEOS API drift, unblocking every other branch from local-build failures.  Then **#111** (per-thread MEOS init) is the thread-safety foundation all parity work builds on.

The **consolidate/** PRs (#97–#105) are independent parity drops covering different function families; they can land in any order once the four correctness fixes (#58, #61, #106, #107) and #111 are in.

---

## Tier 1 — Merge immediately (bug fixes + visibility, trivially reviewable)

| PR | Branch | Description | CI |
|----|--------|-------------|----|
| #117 | `doc/reviewer-guide` | **This guide** — PR review ordering, tiers, dependency chains; visibility wiring (PR template + README banner) | ✅ |
| #121 | `consolidate/main-hygiene-batch` | Revert single-tz on main + sync MEOS API drift (subsumes #116+#118); unblocks every other branch | ✅ |
| #120 | `consolidate/pr-coordination-and-tz-lint` | docs/PR-COORDINATION.md + scripts/lint-tz-pinned-tests.py (subsumes #115+#119) | ✅ |
| #111 | `fix/per-thread-meos-init` | Replace global MEOS mutex with per-thread initialization | ✅ |
| #107 | `fix/span-distance-return-types` | Fix distance return types; add `+` / `shift` alias for tstzset/tstzspan+interval | ✅ |
| #106 | `fix/span-arithmetic-correctness` | Fix SpanSet serialization size and floatspan distance datum | ✅ |
| #61 | `fix/geomset-srid-parameter` | `set(LIST(GEOMETRY), INTEGER)` overload — explicit SRID | ✅ |
| #58 | `fix/splitnspans-spanset-result` | `splitNspans` fix on spanset + 10 aggregate-additions (12-commit rolling-topic, scope-creep tolerated) | ✅ |

---

## Tier 2 — Parity surface — consolidate/ batch (independent, all CI green)

These cover different function families and can land in any order.

| PR | Branch | Description | CI |
|----|--------|-------------|----|
| #105 | `consolidate/docs` | CONTRIBUTING.md + PARITY.md user guide + PARITY-INVENTORY.md | ✅ |
| #104 | `consolidate/geo-types-parity` | tgeometry + tgeography + tgeogpoint — full parity surface | ✅ |
| #100 | `consolidate/analytics-parity` | Temporal analytics parity — simplify / similarity / tnumber math | ✅ |
| #98 | `consolidate/spatial-predicates-parity` | tspatial predicates parity — topological / comparison / position | ✅ |
| #97 | `consolidate/temporal-ops-parity` | Temporal ops parity — boxops / comparison / position / precision / same | ✅ |
| #103 | `consolidate/aggregates-parity` | Aggregate functions parity — extent / SkipList aggregates / tCentroid | ❌ |
| #102 | `consolidate/tiles-bins-parity` | Tile and bin functions parity — emitters / table functions / getters | ❌ |
| #99 | `consolidate/tgeompoint-ops-parity` | tgeompoint operations parity — distance / affine / transforms / geoMeasure | ❌ |

---

## Tier 3 — Recent feature additions (land after consolidate/ batch)

| PR | Branch | Description | CI | Notes |
|----|--------|-------------|----|----|
| #126 | `feat/parity-additions-batch` | bearing + eCovers/tCovers + stbox dim + seqSetGaps (subsumes #122+#123+#124+#125) | ✅ | |
| #114 | `feat/berlinmod-geo-functions2` | nearestApproachDistance, expandSpace, `&&` for TGEOMPOINT | ✅ | |
| #113 | `feat/edge-to-cloud-quickstart` | Edge-to-cloud quickstart, temporalFooter(), SRID/geodetic fix, tgeogpoint tests | ✅ | |
| #110 | `feat/parity-split-complement` | timeSplit / valueSplit / quadSplit emitters | ✅ | |
| #109 | `feat/parity-elevation-restrict` | atElevation / minusElevation via public MEOS primitives | ✅ | |
| #108 | `feat/parity-math-similarity-tbox` | Unskip tnumber math, tbox, and similarity parity tests | ✅ | |
| #112 | `feat/wkb-roundtrip-all-types` | Complete binary + hex-WKB round-trip I/O for all types (TZ-neutral test fix landed) | ✅ | |

---

## Review checklist

For every MobilityDuck PR, verify:

- [ ] PostgreSQL License header on every new `.cpp` / `.hpp` file
- [ ] New function registered in the correct `RegisterXxx()` function
- [ ] SQL name matches MobilityDB alias (RFC #861 portable SQL contract)
- [ ] NULL input handled (returns NULL or appropriate default)
- [ ] DBL\_MAX sentinel from MEOS mapped to NULL for distance functions
- [ ] New parity test added in `test/` with `nosort` tag where result order is non-deterministic
- [ ] CI green before requesting merge (fix ❌ PRs in-branch, not in a follow-up)
