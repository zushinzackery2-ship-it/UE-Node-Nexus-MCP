"""Schema lock: reflection snapshot exported by the UE plugin (``schema_export``).

Files under ``<root>/.nexus/schema/<key>/``::

    key.json                          {"key", "engine_version", "plugins_hash", "generated_at"}
    classes.material_expression.json  {"<ClassName>": {"path", "props": {...}, "inputs": [...], "outputs": [...]}}
    classes.k2node.json               {"<ClassName>": {"path", "props": {...}, "pins": [{"name","dir","type"}], "dynamic_pins": bool}}
    classes.asset.json                {"<ClassName>": {"path", "props"}}
    classes.component.json            {"<ClassName>": {"path", "props"}}
    classes.niagara_renderer.json     {"<ClassName>": {"path", "props"}}
    material_functions.json           {"<object path>": {"inputs": [{"name","type"}], "outputs": [{"name"}]}}
    niagara_modules.json              {"<object path>": {"short", "inputs": [{"name","type","default"}]}}
    functions.cache.json              {"<Owner>.<Func>": {"params": [{"name","type","dir","default"}], "pure": bool}}

Each ``props`` entry: {"type", "kind", "default", "enum_values"?, "clamp_min"?, "clamp_max"?, "object_class"?}.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .paths import schema_dir

CLASS_FILES = {
    "material_expression": "classes.material_expression.json",
    "k2node": "classes.k2node.json",
    "asset": "classes.asset.json",
    "component": "classes.component.json",
    "niagara_renderer": "classes.niagara_renderer.json",
}
_PREFIXES = {
    "material_expression": ("MaterialExpression",),
    "k2node": ("K2Node_", "EdGraphNode_", "AnimGraphNode_"),
    "asset": ("",),
    "component": ("",),
    "niagara_renderer": ("Niagara",),
}
_SUFFIXES = {"niagara_renderer": ("RendererProperties",)}
K2_ALIASES = {
    "Branch": "K2Node_IfThenElse",
    "Sequence": "K2Node_ExecutionSequence",
    "Comment": "EdGraphNode_Comment",
    "Reroute": "K2Node_Knot",
}


@dataclass
class ClassInfo:
    name: str
    path: str
    props: dict[str, dict[str, Any]] = field(default_factory=dict)
    inputs: list[str] = field(default_factory=list)
    outputs: list[str] = field(default_factory=list)
    pins: list[dict[str, Any]] = field(default_factory=list)
    dynamic_pins: bool = False

    def prop(self, name: str) -> dict[str, Any] | None:
        return self.props.get(name)


class SchemaLock:
    def __init__(self, directory: Path, key: str) -> None:
        self.directory = directory
        self.key = key
        self._classes: dict[str, dict[str, ClassInfo]] = {}
        self._material_functions: dict[str, Any] | None = None
        self._niagara_modules: dict[str, Any] | None = None
        self._functions: dict[str, Any] | None = None

    @property
    def available(self) -> bool:
        return (self.directory / "key.json").is_file()

    def info(self) -> dict[str, Any]:
        return _read_json(self.directory / "key.json") or {}

    def _family(self, family: str) -> dict[str, ClassInfo]:
        if family not in self._classes:
            data = _read_json(self.directory / CLASS_FILES[family]) or {}
            classes: dict[str, ClassInfo] = {}
            for name, record in data.items():
                classes[name] = ClassInfo(
                    name=name,
                    path=str(record.get("path", "")),
                    props=dict(record.get("props") or {}),
                    inputs=[str(item) for item in record.get("inputs") or []],
                    outputs=[str(item) for item in record.get("outputs") or []],
                    pins=list(record.get("pins") or []),
                    dynamic_pins=bool(record.get("dynamic_pins", False)),
                )
            self._classes[family] = classes
        return self._classes[family]

    def has_family(self, family: str) -> bool:
        return (self.directory / CLASS_FILES[family]).is_file()

    def resolve_class(self, family: str, name: str) -> ClassInfo | None:
        """Resolve a short name, prefixed name or ``/Script/Module.Class`` path."""
        classes = self._family(family)
        if not classes:
            return None
        candidate = name.strip()
        if family == "k2node":
            candidate = K2_ALIASES.get(candidate, candidate)
        if candidate in classes:
            return classes[candidate]
        if "." in candidate or "/" in candidate:
            for info in classes.values():
                if info.path == candidate:
                    return info
            candidate = candidate.rsplit(".", 1)[-1]
            if candidate in classes:
                return classes[candidate]
        for prefix in _PREFIXES.get(family, ("",)):
            for suffix in _SUFFIXES.get(family, ("",)):
                full = f"{prefix}{candidate}{suffix}"
                if full in classes:
                    return classes[full]
        return None

    def class_names(self, family: str) -> list[str]:
        return sorted(self._family(family))

    def material_function(self, path: str) -> dict[str, Any] | None:
        if self._material_functions is None:
            self._material_functions = _read_json(self.directory / "material_functions.json") or {}
        return _lookup_asset(self._material_functions, path)

    def niagara_module(self, name: str) -> tuple[str | None, dict[str, Any] | None, list[str]]:
        """Return (path, record, ambiguous candidates) for a short name or path."""
        if self._niagara_modules is None:
            self._niagara_modules = _read_json(self.directory / "niagara_modules.json") or {}
        modules = self._niagara_modules
        if not modules:
            return None, None, []
        record = _lookup_asset(modules, name)
        if record is not None:
            return _asset_key(modules, name), record, []
        matches = [path for path, item in modules.items() if str(item.get("short", "")) == name or path.rsplit(".", 1)[-1] == name]
        if len(matches) == 1:
            return matches[0], modules[matches[0]], []
        return None, None, matches

    def function(self, owner: str, name: str) -> dict[str, Any] | None:
        if self._functions is None:
            self._functions = _read_json(self.directory / "functions.cache.json") or {}
        for key, record in self._functions.items():
            key_owner, _, key_name = key.rpartition(".")
            if key_name == name and (key_owner == owner or key_owner.rsplit(".", 1)[-1] == owner):
                return record
        return None

    def remember_functions(self, records: dict[str, Any]) -> None:
        if self._functions is None:
            self._functions = _read_json(self.directory / "functions.cache.json") or {}
        self._functions.update(records)
        self.directory.mkdir(parents=True, exist_ok=True)
        (self.directory / "functions.cache.json").write_text(json.dumps(self._functions, indent=1, ensure_ascii=False), encoding="utf-8")


def _lookup_asset(table: dict[str, Any], path: str) -> dict[str, Any] | None:
    key = _asset_key(table, path)
    return table.get(key) if key else None


def _asset_key(table: dict[str, Any], path: str) -> str | None:
    text = path.strip().strip("'")
    if text in table:
        return text
    if "'" in path:
        text = path.split("'")[1]
        if text in table:
            return text
    if "." not in text.rsplit("/", 1)[-1]:
        with_object = f"{text}.{text.rsplit('/', 1)[-1]}"
        if with_object in table:
            return with_object
    return None


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
    candidates = sorted((path for path in base.iterdir() if (path / "key.json").is_file()), key=lambda item: item.stat().st_mtime, reverse=True)
    if not candidates:
        return None
    return SchemaLock(candidates[0], candidates[0].name)
