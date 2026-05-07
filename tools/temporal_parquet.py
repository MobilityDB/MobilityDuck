"""temporal_parquet.py — TemporalParquet reference implementation.

Implements the metadata convention proposed in doc/temporal-parquet/README.md:
every Parquet file containing MEOS-WKB temporal columns carries a ``temporal``
key in its file-level key_value_metadata (identical placement to GeoParquet's
``geo`` key).

Usage
-----
    # Annotate a Parquet file written by MobilityDuck (BLOB columns):
    temporal_parquet_write(
        source="trajectories_raw.parquet",
        dest="trajectories.parquet",
        columns={
            "traj": TemporalColumnMeta(
                base_type="tgeompoint",
                subtype="Sequence",
                interpolation="linear",
                srid=4326,
            )
        },
    )

    # Read back — returns the metadata dict and column names:
    meta = temporal_parquet_read_meta("trajectories.parquet")
    print(meta["columns"]["traj"]["base_type"])   # "tgeompoint"

    # Round-trip integrity check (needs PyArrow >= 12):
    ok = temporal_parquet_verify("trajectories.parquet")

Requirements: pyarrow >= 12.
"""

from __future__ import annotations

import json
import sys
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Optional

import pyarrow as pa
import pyarrow.parquet as pq

# ─── spec constants ───────────────────────────────────────────────────────────

TEMPORAL_PARQUET_VERSION = "1.0.0"
ENCODING_VERSION = "1.0"
METADATA_KEY = b"temporal"

# Recognised base_type values (informational; readers MUST NOT hard-code this list)
TEMPORAL_BASE_TYPES = {
    # scalar temporals
    "tbool", "tint", "tfloat", "ttext",
    # spatial temporals
    "tgeompoint", "tgeogpoint", "tgeometry", "tgeography",
    # extended temporals
    "tcbuffer", "tnpoint", "tpose", "trgeo", "tpcpoint", "tpcpatch", "th3index",
    # boxes
    "stbox", "tbox",
    # spans / spansets / sets
    "intspan", "floatspan", "datespan", "tstzspan",
    "intspanset", "floatspanset", "datespanset", "tstzspanset",
    "intset", "bigintset", "floatset", "textset", "dateset", "tstzset",
    "geomset", "geogset",
}

# ─── per-column metadata ──────────────────────────────────────────────────────

@dataclass
class TemporalColumnMeta:
    """Metadata for one MEOS-WKB temporal column.

    All fields mirror the JSON schema in doc/temporal-parquet/README.md.
    Optional fields may be omitted (None → absent in JSON).
    """

    base_type: str
    """MobilityDB/MEOS type name, e.g. 'tgeompoint', 'tint', 'tstzspan'."""

    encoding: str = "MEOS-WKB"
    encoding_version: str = ENCODING_VERSION

    # Lifted temporal subtypes only:
    subtype: Optional[str] = None          # "Instant" | "Sequence" | "SequenceSet"
    interpolation: Optional[str] = None    # "discrete" | "step" | "linear"

    # Spatial columns:
    srid: Optional[int] = None
    geodetic: Optional[bool] = None
    has_z: Optional[bool] = None

    def to_dict(self) -> dict:
        d: dict = {
            "encoding": self.encoding,
            "encoding_version": self.encoding_version,
            "base_type": self.base_type,
        }
        if self.subtype is not None:
            d["subtype"] = self.subtype
        if self.interpolation is not None:
            d["interpolation"] = self.interpolation
        if self.srid is not None:
            d["srid"] = self.srid
        if self.geodetic is not None:
            d["geodetic"] = self.geodetic
        if self.has_z is not None:
            d["has_z"] = self.has_z
        return d

    @classmethod
    def from_dict(cls, d: dict) -> "TemporalColumnMeta":
        return cls(
            base_type=d["base_type"],
            encoding=d.get("encoding", "MEOS-WKB"),
            encoding_version=d.get("encoding_version", ENCODING_VERSION),
            subtype=d.get("subtype"),
            interpolation=d.get("interpolation"),
            srid=d.get("srid"),
            geodetic=d.get("geodetic"),
            has_z=d.get("has_z"),
        )

# ─── file-level metadata ──────────────────────────────────────────────────────

@dataclass
class TemporalParquetMeta:
    """The ``temporal`` metadata object written to the Parquet file footer."""

    columns: dict[str, TemporalColumnMeta]
    version: str = TEMPORAL_PARQUET_VERSION
    primary_temporal_column: Optional[str] = None

    def to_json(self) -> str:
        obj: dict = {"version": self.version, "columns": {}}
        if self.primary_temporal_column:
            obj["primary_temporal_column"] = self.primary_temporal_column
        for col_name, col_meta in self.columns.items():
            obj["columns"][col_name] = col_meta.to_dict()
        return json.dumps(obj)

    @classmethod
    def from_json(cls, s: str) -> "TemporalParquetMeta":
        obj = json.loads(s)
        cols = {
            name: TemporalColumnMeta.from_dict(meta)
            for name, meta in obj.get("columns", {}).items()
        }
        return cls(
            version=obj.get("version", TEMPORAL_PARQUET_VERSION),
            primary_temporal_column=obj.get("primary_temporal_column"),
            columns=cols,
        )

# ─── public API ───────────────────────────────────────────────────────────────

def temporal_parquet_write(
    source: str | Path,
    dest: str | Path,
    columns: dict[str, TemporalColumnMeta],
    primary_temporal_column: Optional[str] = None,
) -> None:
    """Read *source* Parquet, inject ``temporal`` metadata, write to *dest*.

    *source* is a Parquet file whose MEOS-WKB columns are typed as BLOB
    (``BYTE_ARRAY`` in Parquet terms) — exactly as written by MobilityDuck's
    ``COPY … TO … (FORMAT PARQUET)`` after calling ``asBinary()``.

    Parameters
    ----------
    source:
        Input Parquet path (written by MobilityDuck).
    dest:
        Output path.  May equal *source* for in-place annotation.
    columns:
        Mapping of column name → :class:`TemporalColumnMeta`.
    primary_temporal_column:
        Name of the "main" temporal column (optional convenience hint for readers).
    """
    source, dest = Path(source), Path(dest)
    table = pq.read_table(source)

    # Validate that declared columns are present and BLOB-typed
    for col_name in columns:
        if col_name not in table.schema.names:
            raise ValueError(
                f"Column '{col_name}' not found in Parquet schema "
                f"(available: {table.schema.names})"
            )
        col_type = table.schema.field(col_name).type
        if col_type not in (pa.large_binary(), pa.binary()):
            raise ValueError(
                f"Column '{col_name}' is {col_type}, expected binary/large_binary "
                f"(MEOS-WKB BLOB).  Make sure to call asBinary() before COPY TO."
            )

    meta = TemporalParquetMeta(
        columns=columns,
        primary_temporal_column=primary_temporal_column
        or (next(iter(columns)) if len(columns) == 1 else None),
    )

    existing = table.schema.metadata or {}
    existing[METADATA_KEY] = meta.to_json().encode()
    annotated = table.replace_schema_metadata(existing)
    pq.write_table(annotated, dest)


def temporal_parquet_read_meta(path: str | Path) -> Optional[TemporalParquetMeta]:
    """Return the :class:`TemporalParquetMeta` from *path*, or None if absent."""
    pf = pq.ParquetFile(path)
    raw_meta = pf.schema_arrow.metadata
    if raw_meta is None or METADATA_KEY not in raw_meta:
        return None
    return TemporalParquetMeta.from_json(raw_meta[METADATA_KEY].decode())


def temporal_parquet_verify(path: str | Path) -> bool:
    """Check that every declared temporal column is readable binary data.

    Returns True if the file passes basic integrity checks.  Raises on schema
    violations so callers can distinguish missing-metadata (returns None) from
    bad metadata (raises).
    """
    meta = temporal_parquet_read_meta(path)
    if meta is None:
        print(f"[WARN] {path}: no 'temporal' metadata key found", file=sys.stderr)
        return False

    table = pq.read_table(path, columns=list(meta.columns))
    ok = True
    for col_name, col_meta in meta.columns.items():
        col = table.column(col_name)
        null_count = col.null_count
        non_null = len(col) - null_count
        sample_size = min(5, non_null)
        print(
            f"  {col_name:30s}  base_type={col_meta.base_type:<14s}  "
            f"rows={len(col)}  nulls={null_count}  "
            f"sample_bytes={[len(v.as_py()) for v in col.drop_null()[:sample_size]]}"
        )
    return ok


def temporal_parquet_describe(path: str | Path) -> None:
    """Print a human-readable summary of the TemporalParquet metadata."""
    path = Path(path)
    meta = temporal_parquet_read_meta(path)
    pf = pq.ParquetFile(path)

    print(f"File  : {path}")
    print(f"Rows  : {pf.metadata.num_rows}")
    print(f"Groups: {pf.metadata.num_row_groups}")

    if meta is None:
        print("TemporalParquet metadata : absent")
        return

    print(f"\nTemporalParquet {meta.version}")
    if meta.primary_temporal_column:
        print(f"  primary_temporal_column: {meta.primary_temporal_column}")
    print(f"  columns ({len(meta.columns)}):")
    for name, cm in meta.columns.items():
        extras = []
        if cm.subtype:       extras.append(f"subtype={cm.subtype}")
        if cm.interpolation: extras.append(f"interp={cm.interpolation}")
        if cm.srid:          extras.append(f"srid={cm.srid}")
        if cm.has_z:         extras.append("has_z")
        if cm.geodetic:      extras.append("geodetic")
        suffix = "  " + ", ".join(extras) if extras else ""
        print(f"    {name:30s}  {cm.base_type}{suffix}")


# ─── CLI ──────────────────────────────────────────────────────────────────────

def _cli_annotate(args) -> None:
    col_specs: dict[str, TemporalColumnMeta] = {}
    for spec in args.column:
        parts = dict(kv.split("=", 1) for kv in spec.split(","))
        name = parts.pop("name")
        base_type = parts.pop("base_type")
        srid = int(parts["srid"]) if "srid" in parts else None
        geodetic_str = parts.get("geodetic")
        geodetic = (geodetic_str.lower() in ("1", "true", "yes")) if geodetic_str else None
        col_specs[name] = TemporalColumnMeta(
            base_type=base_type,
            subtype=parts.get("subtype"),
            interpolation=parts.get("interpolation"),
            srid=srid,
            geodetic=geodetic,
        )
    dest = args.dest or args.source
    temporal_parquet_write(args.source, dest, col_specs)
    print(f"Wrote TemporalParquet metadata → {dest}")


def _cli_describe(args) -> None:
    temporal_parquet_describe(args.path)


def _cli_verify(args) -> None:
    ok = temporal_parquet_verify(args.path)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(
        prog="temporal_parquet",
        description="TemporalParquet reference implementation",
    )
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_ann = sub.add_parser("annotate", help="Inject temporal metadata into a Parquet file")
    p_ann.add_argument("source", help="Input Parquet file (MEOS-WKB BLOB columns)")
    p_ann.add_argument("--dest", help="Output path (default: overwrite source)")
    p_ann.add_argument(
        "--column",
        action="append",
        required=True,
        metavar="name=<col>,base_type=<type>[,srid=N,subtype=S,interp=I]",
        help="Column spec (repeat for multiple temporal columns)",
    )
    p_ann.set_defaults(func=_cli_annotate)

    p_desc = sub.add_parser("describe", help="Print TemporalParquet metadata summary")
    p_desc.add_argument("path")
    p_desc.set_defaults(func=_cli_describe)

    p_ver = sub.add_parser("verify", help="Verify TemporalParquet integrity")
    p_ver.add_argument("path")
    p_ver.set_defaults(func=_cli_verify)

    parsed = parser.parse_args()
    parsed.func(parsed)
