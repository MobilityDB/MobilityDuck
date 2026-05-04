# Contributing to MobilityDuck

MobilityDuck is the DuckDB extension for MEOS-stored mobility data. Contributions, gap reports, and questions are all welcome.

## Reporting a gap or bug

Please open an issue at https://github.com/MobilityDB/MobilityDuck/issues with:

- The MobilityDuck / MEOS / DuckDB versions you are running. Run `SELECT mobilityduck_full_version();` from the DuckDB shell — it returns the extension version, the linked MEOS commit, the DuckDB version, and the full toolchain.
- The function signature or SQL query you tried.
- The expected behaviour (typically what MobilityDB on PostgreSQL does for the same input — [`doc/PARITY.md`](doc/PARITY.md) is the reference for *"should work the same"*).
- The actual behaviour, including any error messages.

## Filing a feature request

For new functions, OGC-conformance improvements, or planner / index integrations, an issue with the proposed signature and use case is enough to start a conversation. Cross-reference any related tracking issue on the [MobilityDB main repo](https://github.com/MobilityDB/MobilityDB/issues) — MEOS-side API gaps that affect MobilityDuck are tracked there.

## Pull requests

- Branch from `master`. Use `feat/`, `fix/`, `docs/`, `refactor/` prefixes.
- Keep PRs focused — one logical change per PR.
- Reference related issues with `Fixes #N` or `Refs #N`.
- For feature parity work, add or update tests under `test/` mirroring the MobilityDB regression that proves the parity claim.

## Governance

MobilityDuck is part of the [MobilityDB ecosystem](https://libmeos.org/). Significant design decisions are typically discussed first on the MobilityDB main repo so the broader community can weigh in.

## License

By contributing you agree that your contributions are licensed under the project's [MIT License](LICENSE).
