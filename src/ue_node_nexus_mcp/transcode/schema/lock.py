"""Schema lock: the reflection catalog one ``schema_export`` published.

Files under ``<root>/.nexus/schema/<key>/``: ``key.json`` is the format-2
manifest naming every record file of every family, and ``index.md`` with the
per-category indexes are its readable views. A directory that still holds the
first release's per-family tables is converted in place on first read, so every
reader resolves through one record layout.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from ..storage.paths import schema_dir


@dataclass
class ClassInfo:
    name: str
    path: str
    props: dict[str, dict[str, Any]] = field(default_factory=dict)
    inputs: list[str] = field(default_factory=list)
    outputs: list[str] = field(default_factory=list)
    pins: list[dict[str, Any]] = field(default_factory=list)
    dynamic_pins: bool = False
    inheritance: str = ""

    def prop(self, name: str) -> dict[str, Any] | None:
        return self.props.get(name)


class SchemaLock:
    def __init__(self, directory: Path, key: str) -> None:
        self.directory = directory
        self.key = key
        self.used: dict[tuple[str, str], str] = dict()
        self.records: dict[str, dict] = dict()
        self._manifest: dict = dict()
        self._manifest_stamp = None
        self._indexes: dict = dict()

    @property
    def available(self) -> bool:
        return (self.directory / "key.json").is_file()

    def info(self) -> dict[str, Any]:
        path = self.directory / "key.json"
        if not path.is_file():
            return dict()
        stat = path.stat()
        stamp = (stat.st_mtime_ns, stat.st_size)
        if stamp != self._manifest_stamp:
            manifest = _read_json(path) or dict()
            if manifest.get("format") != 2:
                from .catalog import ensure_format

                manifest = ensure_format(self.directory, self.key)
                stat = path.stat()
                stamp = (stat.st_mtime_ns, stat.st_size)
            self._manifest, self._manifest_stamp = manifest, stamp
            self._indexes.clear()
        return self._manifest

    def index(self, family: str):
        """Ranked spellings of one family, rebuilt only when the manifest moves."""
        manifest = self.info()
        if family not in self._indexes:
            from .names import NameIndex

            self._indexes[family] = NameIndex(family, manifest.get("tables", dict()).get(family, dict()))
        return self._indexes[family]

    def resolve_class(self, family: str, name: str, *, aliases: bool = True) -> ClassInfo | None:
        """Resolve a short name, prefixed name or ``/Script/Module.Class`` path.

        ``aliases=False`` accepts only the path and the real class name, the only
        spellings a reference written from a native class path can take.
        """
        from .catalog import lookup_class

        record = lookup_class(self, family, name.strip(), aliases=aliases)
        if record is None:
            return None
        return ClassInfo(name=record["name"], path=record["path"], props=record.get("props", dict()),
                         inputs=record.get("inputs", []), outputs=record.get("outputs", []),
                         pins=record.get("pins", []), dynamic_pins=record.get("dynamic_pins", False),
                         inheritance=record.get("inheritance", ""))

    def material_function(self, path: str) -> dict[str, Any] | None:
        from .catalog import lookup_record

        return lookup_record(self, "material_functions", path)

    def niagara_module(self, name: str) -> dict[str, Any] | None:
        """The module record a short name or path names; its ``name`` is the script path."""
        from .catalog import lookup_record

        return lookup_record(self, "niagara_modules", name)

    def function(self, owner: str, name: str) -> dict[str, Any] | None:
        from .functions import resolve

        return resolve(self, owner, name)

    def direct_function(self, owner: str, name: str) -> dict[str, Any] | None:
        from .catalog import lookup_record

        key = owner + "." + name
        return lookup_record(self, "functions", key) or lookup_record(self, "callable_functions", key)

    def binding(self) -> dict:
        return dict(schema_key=self.key, entries=[dict(family=family, name=name, hash=hash_value) for (family, name), hash_value in sorted(self.used.items())])


def _read_json(path: Path) -> dict[str, Any] | None:
    if not path.is_file():
        return None
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    return data if isinstance(data, dict) else None


def load_schema_lock(root: Path, key: str) -> SchemaLock:
    return SchemaLock(schema_dir(root, key), key)


def find_any_schema_lock(root: Path) -> SchemaLock | None:
    base = root / ".nexus" / "schema"
    if not base.is_dir():
        return None
    # Dot-prefixed directories are in-progress collections, never a usable catalog.
    candidates = sorted((path for path in base.iterdir() if not path.name.startswith(".") and (path / "key.json").is_file()),
                        key=lambda item: item.stat().st_mtime, reverse=True)
    if not candidates:
        return None
    return SchemaLock(candidates[0], candidates[0].name)
