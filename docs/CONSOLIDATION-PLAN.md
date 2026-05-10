# Consolidation plan — open parity work

This document captures the file-level overlap between the parity commits
already on `main` and the open `consolidate/*` PRs, and suggests the
resolution path.  Maintained as a working artefact during consolidation;
delete once everything is merged.

## Direct overlaps

### Per-thread MEOS init (PR #111) vs. single-timezone commits on `main`

| On main | PR #111 |
|---|---|
| `39921f1 fix(tz): single-timezone model` adds `meos_initialize_timezone("Europe/Brussels")` + `AutoLoadExtension(icu)` + `SetOptionByName("TimeZone", "Europe/Brussels")` to `LoadInternal`. | `c237f6c fix(threads): replace global mutex with per-thread MEOS initialization` REMOVES the entire `meos_initialize()` block from `LoadInternal`; per-thread guard initializes MEOS lazily on first use. |
| `08a5598 docs(tz): clarify two-timezone reality in comments` comments framing the Brussels override. | `9dd765a test(stbox): make timestamp assertions timezone-neutral` establishes the project policy: tests use `stbox_eq()` / `=` / `asText(...)` comparisons, never offset-bearing string matches. |

**Resolution**: when PR #111 merges, revert `39921f1` and `08a5598` from
`main`.  Any test files pinned to `+01` that the single-timezone
commits introduced (`040_tgeometry_parity.test`, `041_tgeography_parity.test`,
`042_tgeogpoint_parity.test`, plus the `update_test_expected.py` rewrite of
~25 test files) need to be rewritten as timezone-neutral, **not flipped
back to `+00`**.

### Parity work on `main` vs. `consolidate/*` PRs

| Main commit | Files | Overlapping PR |
|---|---|---|
| `c8cad6d feat(parity): Binary/HexWKB I/O for sets, spans, spansets` | `src/temporal/{set,span,spanset}{,_functions}.cpp` | #103 `consolidate/aggregates-parity` |
| `91102ae feat(parity): tgeometry/tgeography Binary/HexWKB/MFJSON/Text parsers` | `src/geo/{tgeometry,tgeography}_in_out.cpp` | #102 `consolidate/tiles-bins-parity`, #104 `consolidate/geo-types-parity` |
| `afac6eb feat(parity): tbool/tint/tfloat/ttext FromHexWKB and FromMFJSON parsers` | `src/temporal/temporal.cpp` | #97 `consolidate/temporal-ops-parity`, #100 `consolidate/analytics-parity`, #112 `feat/wkb-roundtrip-all-types` |
| `88227cd feat(parity): tgeo_teq/tne for tgeometry and tgeography` | `src/geo/{tgeometry,tgeography}_ops.cpp` | #98 `consolidate/spatial-predicates-parity`, #104 |
| `e958b59 feat(parity): tgeo_teq/tne aliases + audit fixes` | `src/geo/tgeompoint.cpp`, `scripts/parity-audit.py` | #98, #99 `consolidate/tgeompoint-ops-parity` |
| `e41c8d9 feat(parity): tbool_and/or/not, ttext_cat, mobilitydb_version aliases` | `src/temporal/temporal.cpp`, `src/mobilityduck_extension.cpp` | #97, #99, #102, #103 |
| `cb88cc0 docs(parity): exclude PG-only entries from headline coverage` | `scripts/parity-audit.py`, `docs/parity-status.md` | none direct |

**Resolution options** (per consolidation PR — pick one):

1. **Rebase the consolidation PR on top of the main commits**, then drop
   the now-duplicate registrations from the PR diff.  Cleanest when the
   consolidation PR is the larger / more comprehensive surface.

2. **Revert the main commit and fold its registrations into the
   consolidation PR**.  Cleanest when the consolidation PR has not yet
   added a particular alias but the main commit has.

3. **Keep both, accept the diff churn**.  Only viable when the main
   commit and the consolidation PR add the same name-list and the diff
   is truly identical — which is rare given audit/policy drift between
   the two streams.

The maintainer makes the call per PR.  `cb88cc0` (audit script
infrastructure) is independent of the consolidations and can stay on
`main` regardless.

## Independent items (no current overlap)

These pushed branches do not collide with any open PR and can land
independently:

- `docs/pr-coordination-policy` (this batch) — adds
  `docs/PR-COORDINATION.md`.

## Notes for the maintainer

- The audit script `scripts/parity-audit.py` is the source of truth for
  coverage tracking.  Regenerate `docs/parity-status.md` after each
  consolidation merge so the headline number reflects the merged
  surface.  At time of writing the audit reports 90.3% addressable
  parity on `main`.

- Cross-platform fanout — every name renamed or removed from MEOS
  (`meosType → MeosType`, `tcontains_geo_tgeo` arg-count drop, etc.)
  needs a corresponding sync in MobilityDuck.  The parallel branch
  `perf/duck-stack-attempt2` already has these (`7a8cc22`, `c37b9e2`);
  these need to land on `main` too, ideally before the consolidation
  PRs.
