"""temporal_parquet.py — TemporalParquet reference implementation.

Implements the metadata convention of the TemporalParquet specification,
https://github.com/MobilityDB/MobilityLakehouse/blob/main/spec/temporalparquet.md:
every Parquet file containing MEOS-WKB temporal columns carries a ``temporal``
key in its file-level key_value_metadata (identical placement to GeoParquet's
``geo`` key), and the coverings of each column are the struct columns
``<col>_bbox``, ``<col>_tspan`` and ``<col>_vspan`` the file holds.

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
                srid=25832,
            )
        },
    )

    # Read back — returns the metadata of the file:
    meta = temporal_parquet_read_meta("trajectories.parquet")
    print(meta.columns["traj"].base_type)   # "tgeompoint"

    # Integrity check (needs PyArrow >= 12):
    ok = temporal_parquet_verify("trajectories.parquet")

Requirements: pyarrow >= 12.
"""

from __future__ import annotations

import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Optional

import pyarrow as pa
import pyarrow.parquet as pq

# ─── spec constants ───────────────────────────────────────────────────────────

TEMPORAL_PARQUET_VERSION = "2.0.0"
ENCODING_VERSION = "1.0"
METADATA_KEY = b"temporal"

# Recognised base_type values (informational; readers MUST NOT hard-code this list)
TEMPORAL_BASE_TYPES = {
    # scalar temporals
    "tbool", "tint", "tbigint", "tfloat", "ttext",
    # spatial temporals
    "tgeompoint", "tgeogpoint", "tgeometry", "tgeography",
    # extended temporals
    "tcbuffer", "tnpoint", "tpose", "trgeometry", "tpcpoint", "tpcpatch", "th3index",
    # boxes
    "stbox", "tbox",
    # spans / spansets / sets
    "intspan", "floatspan", "datespan", "tstzspan",
    "intspanset", "floatspanset", "datespanset", "tstzspanset",
    "intset", "bigintset", "floatset", "textset", "dateset", "tstzset",
    "geomset", "geogset",
}

# GeoParquet's `edges` for the base types that fix it: a geodetic value moves
# between two instants along the shortest path on the sphere
BASE_TYPE_EDGES = {
    "tgeompoint": "planar", "tgeometry": "planar",
    "tgeogpoint": "spherical", "tgeography": "spherical",
}

# The coverings a file may carry for a column, each a struct column named
# `<col>_<key>`, with the fields a bound may name, in their required order
COVERING_FIELDS = {
    "bbox": ("xmin", "ymin", "zmin", "xmax", "ymax", "zmax"),
    "vspan": ("vmin", "vmax"),
    "tspan": ("tmin", "tmax"),
}

# Marks a field that is absent, as distinct from a field set to JSON null
ABSENT: Any = object()

# ─── per-column metadata ──────────────────────────────────────────────────────

@dataclass
class TemporalColumnMeta:
    """Metadata for one MEOS-WKB temporal column.

    All fields mirror the TemporalParquet specification. Optional fields may be
    omitted (None → absent in JSON); ``crs`` is absent by default and may be
    set to a PROJJSON object or to None, which writes JSON null.
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
    crs: Any = ABSENT                      # PROJJSON dict, None (null), or ABSENT
    edges: Optional[str] = None            # "planar" | "spherical"
    has_z: Optional[bool] = None

    # Coverings: key -> {bound: [column, field]}
    covering: Optional[dict] = None

    def resolved_edges(self) -> Optional[str]:
        """The edges of the column: as given, or as its base type fixes them."""
        return self.edges or BASE_TYPE_EDGES.get(self.base_type)

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
        crs = self.crs
        if crs is ABSENT and self.srid == 0:
            crs = None
        if crs is not ABSENT:
            d["crs"] = crs
        edges = self.resolved_edges()
        if edges is not None:
            d["edges"] = edges
            d["geodetic"] = edges != "planar"
        if self.has_z is not None:
            d["has_z"] = self.has_z
        if self.covering:
            d["covering"] = self.covering
        return d

    @classmethod
    def from_dict(cls, d: dict) -> "TemporalColumnMeta":
        edges = d.get("edges")
        if edges is None and d.get("geodetic") is not None:
            edges = "spherical" if d["geodetic"] else "planar"
        return cls(
            base_type=d["base_type"],
            encoding=d.get("encoding", "MEOS-WKB"),
            encoding_version=d.get("encoding_version", ENCODING_VERSION),
            subtype=d.get("subtype"),
            interpolation=d.get("interpolation"),
            srid=d.get("srid"),
            crs=d["crs"] if "crs" in d else ABSENT,
            edges=edges,
            has_z=d.get("has_z"),
            covering=d.get("covering"),
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

# ─── coverings ────────────────────────────────────────────────────────────────

def coverings_in_schema(schema: pa.Schema, column: str) -> dict:
    """Return the covering declaration for *column* from the struct columns
    ``<column>_bbox``, ``<column>_tspan`` and ``<column>_vspan`` in *schema*:
    each bound the struct holds maps to the path of the field carrying it."""
    covering: dict = {}
    for key, fields in COVERING_FIELDS.items():
        target = f"{column}_{key}"
        if target not in schema.names:
            continue
        kind = schema.field(target).type
        if not pa.types.is_struct(kind):
            continue
        present = {kind.field(i).name for i in range(kind.num_fields)}
        bounds = {f: [target, f] for f in fields if f in present}
        if bounds:
            covering[key] = bounds
    return covering

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
    ``COPY … TO … (FORMAT PARQUET)`` after calling ``asBinary()``. Each column
    that declares no covering gets the coverings its struct columns provide.

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

    for col_name, col_meta in columns.items():
        if col_meta.covering is None:
            col_meta.covering = coverings_in_schema(table.schema, col_name) or None

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
    """Check that every declared temporal column is readable binary data and
    that every covering it declares names a field the file holds.

    Returns True if the file passes these checks, False when the ``temporal``
    key is absent or a covering names a missing field.
    """
    meta = temporal_parquet_read_meta(path)
    if meta is None:
        print(f"[WARN] {path}: no 'temporal' metadata key found", file=sys.stderr)
        return False

    schema = pq.read_schema(path)
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
        for key, bounds in (col_meta.covering or {}).items():
            for bound, ref in bounds.items():
                target, field = ref[0], ref[-1]
                kind = schema.field(target).type if target in schema.names else None
                if kind is None or not pa.types.is_struct(kind) or \
                        field not in {kind.field(i).name for i in range(kind.num_fields)}:
                    print(f"  [FAIL] {col_name}: covering {key}.{bound} names "
                          f"{ref}, which the file does not hold", file=sys.stderr)
                    ok = False
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
        if cm.srid is not None: extras.append(f"srid={cm.srid}")
        if cm.crs is None:   extras.append("crs=null")
        elif cm.crs is not ABSENT: extras.append("crs=PROJJSON")
        if cm.resolved_edges(): extras.append(f"edges={cm.resolved_edges()}")
        if cm.has_z:         extras.append("has_z")
        if cm.covering:      extras.append("covering=" + "+".join(cm.covering))
        suffix = "  " + ", ".join(extras) if extras else ""
        print(f"    {name:30s}  {cm.base_type}{suffix}")


# ─── CLI ──────────────────────────────────────────────────────────────────────

def _read_crs(value: str) -> Any:
    """The CRS a column spec names: `null`, or the path of a PROJJSON file."""
    if value.lower() == "null":
        return None
    with open(value, encoding="utf-8") as f:
        return json.load(f)


def _cli_annotate(args) -> None:
    col_specs: dict[str, TemporalColumnMeta] = {}
    for spec in args.column:
        parts = dict(kv.split("=", 1) for kv in spec.split(","))
        name = parts.pop("name")
        base_type = parts.pop("base_type")
        has_z_str = parts.get("has_z")
        col_specs[name] = TemporalColumnMeta(
            base_type=base_type,
            subtype=parts.get("subtype"),
            interpolation=parts.get("interp") or parts.get("interpolation"),
            srid=int(parts["srid"]) if "srid" in parts else None,
            crs=_read_crs(parts["crs"]) if "crs" in parts else ABSENT,
            edges=parts.get("edges"),
            has_z=(has_z_str.lower() in ("1", "true", "yes")) if has_z_str else None,
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
        metavar="name=<col>,base_type=<type>[,srid=N,crs=<projjson file>|null,"
                "edges=E,has_z=B,subtype=S,interp=I]",
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
