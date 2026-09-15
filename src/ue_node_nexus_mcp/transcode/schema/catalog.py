"""Immutable classified records with a single atomically published index."""

from __future__ import annotations

import re
from pathlib import Path

from ..collaboration.store.io import atomic_write, canonical, digest, read_json, confined
from ..sync_project import SyncError
from .lock import CLASS_FILES, K2_ALIASES, _PREFIXES, _SUFFIXES
from .records import FAMILIES, normalize

LEGACY_FILES = dict(CLASS_FILES, material_functions="material_functions.json", niagara_modules="niagara_modules.json",
                    functions="functions.cache.json", callable_functions="functions.index.json", types="types.json")


def publish(directory: Path, key: str, tables: dict, environment: dict | None = None,
            coverage: dict | None = None, incremental=False) -> dict:
    from ..transaction.lock import MirrorLock

    with MirrorLock(directory, directory / "index.lock"):
        return _publish(directory, key, tables, environment, coverage, incremental)


def _publish(directory, key, tables, environment, coverage, incremental) -> dict:
    old = read_json(directory / "key.json") if (directory / "key.json").is_file() else dict()
    index = dict(old.get("tables", dict())) if incremental else dict()
    for family, records in tables.items():
        entries = dict(index.get(family, dict())) if incremental else dict()
        for name, raw in records.items():
            record = normalize(family, name, raw, key)
            record.pop("content_hash", None)
            props = record.get("props", dict())
            if len(props) > 256:
                keys, pages = sorted(props), []
                for start in range(0, len(keys), 256):
                    page = dict((name, props[name]) for name in keys[start:start + 256])
                    page_hash = digest(page)
                    path = f"{FAMILIES[family]}/pages/{page_hash}.json"
                    atomic_write(directory / path, canonical(page))
                    pages.append(dict(file=path, hash=page_hash))
                record.update(props=dict(), props_pages=pages)
            record["content_hash"] = digest(record)
            record_hash = digest(record)
            filename = re.sub(r"[^A-Za-z0-9_.-]", "_", record["path"])[-100:]
            path = f"{FAMILIES[family]}/{filename}.{record_hash}.json"
            if not (directory / path).is_file():
                atomic_write(directory / path, canonical(record))
            entries[name] = dict(file=path, hash=record_hash, path=record["path"], aliases=record["aliases"],
                                 category=record["category"], coverage=record["coverage"], bridge=record["bridge"])
        index[family] = entries
    manifest = dict(format=2, key=key, environment=environment or old.get("environment", dict()),
                    generation=old.get("generation", 0) + 1, tables=index,
                    coverage=coverage or old.get("coverage", dict()), contexts=old.get("contexts", dict()))
    manifest["content_hash"] = digest(dict(tables=index, environment=manifest["environment"]))
    atomic_write(directory / "indexes" / f"{manifest['content_hash']}.json", canonical(manifest))
    atomic_write(directory / "key.json", canonical(manifest))
    markdown(directory, manifest)
    return manifest


def migrate(directory: Path, key: str, source: Path | None = None, environment=None, coverage=None) -> dict:
    source = source or directory
    tables = dict()
    for family, filename in LEGACY_FILES.items():
        file = source / filename
        if file.is_file():
            tables[family] = read_json(file)
    return publish(directory, key, tables, environment, coverage)


def read_entry(lock, family: str, name: str, entry: dict) -> dict:
    lock.used[(family, name)] = entry["hash"]
    if entry["hash"] in lock.records:
        return lock.records[entry["hash"]]
    path = confined(lock.directory, entry["file"])
    record = read_json(path)
    if digest(record) != entry["hash"]:
        raise SyncError("schema_corrupt", f"schema record checksum mismatch: {path}")
    if record.get("props_pages"):
        props = dict(record.get("props", dict()))
        for page in record["props_pages"]:
            data = read_json(confined(lock.directory, page["file"]))
            if digest(data) != page["hash"]:
                raise SyncError("schema_corrupt", "property page checksum mismatch", page)
            props.update(data)
        record["props"] = props
    lock.records[entry["hash"]] = record
    return record


def _entry_names(entry: dict) -> set[str]:
    """Names an index entry answers to, derived from the entry alone.

    Catalog entries carry ``path``, so the real class name is available even when
    a catalog published before ``records.class_aliases`` widened ``aliases``.
    """
    names = set(entry.get("aliases") or ())
    path = str(entry.get("path") or "")
    simple = path.rsplit(".", 1)[-1].rsplit("/", 1)[-1] if path else ""
    if simple:
        names.add(simple)
    return names


def lookup_record(lock, family: str, name: str) -> dict | None:
    entries = lock.info().get("tables", dict()).get(family, dict())
    candidates = [name, name + "." + name.rsplit("/", 1)[-1]]
    for key in candidates:
        if key in entries:
            return read_entry(lock, family, key, entries[key])
    matches = [(key, entry) for key, entry in entries.items() if name in _entry_names(entry) or key.endswith("." + name)]
    if len(matches) > 1:
        raise SyncError("schema_ambiguous", "use the full UE path", dict(candidates=[key for key, _ in matches]))
    return read_entry(lock, family, *matches[0]) if matches else None


def lookup_class(lock, family: str, name: str, manifest: dict) -> dict | None:
    entries = manifest.get("tables", dict()).get(family, dict())
    candidate = K2_ALIASES.get(name, name) if family == "k2node" else name
    candidates = [candidate]
    candidates.extend(f"{prefix}{candidate}{suffix}" for prefix in _PREFIXES.get(family, ("",)) for suffix in _SUFFIXES.get(family, ("",)))
    for key in candidates:
        if key in entries:
            return read_entry(lock, family, key, entries[key])
    names = set(candidates)
    matches = [(key, entry) for key, entry in entries.items() if names.intersection(_entry_names(entry))]
    if len(matches) == 1:
        return read_entry(lock, family, *matches[0])
    if len(matches) > 1:
        raise SyncError("schema_ambiguous", "use the full UE type path", dict(candidates=[entry["path"] for _, entry in matches]))
    return None


def markdown(directory: Path, manifest: dict) -> None:
    categories = dict((name, []) for name in sorted(set(FAMILIES.values())))
    for family, entries in manifest["tables"].items():
        for name, entry in entries.items():
            label = name.replace("|", "\\|").replace("\n", " ")
            categories[FAMILIES[family]].append(f"| {label} | [{entry['path']}]({Path(entry['file']).name}) | {entry['coverage']} | {entry['bridge'].get('write', 'unknown')} |")
    index = [f"# Schema {manifest['key']}", "", f"Generation: {manifest['generation']}. JSON records are the source for validation and editing.", ""]
    for category, rows in categories.items():
        index.append(f"- [{category}]({category}/index.md): {len(rows)} records")
        content = [f"# {category}", "", "| Name | Record | Coverage | Bridge write |", "|---|---|---|---|", *rows, ""]
        atomic_write(directory / category / "index.md", "\n".join(content).encode("utf-8"))
    index.extend(["", "Dynamic records are in contexts/. Query schema(target=..., context=...) for target-specific pins and overrides.", "",
                  "Availability covers the registered types reported by the current providers. Missing metadata is explicitly marked; use refresh to re-observe the editor.", ""])
    atomic_write(directory / "index.md", "\n".join(index).encode("utf-8"))


def validate_binding(lock, binding: dict) -> None:
    if binding["schema_key"] != lock.key:
        raise SyncError("schema_stale", "schema environment changed")
    tables = lock.info().get("tables", dict())
    for item in binding.get("entries", []):
        current = tables.get(item["family"], dict()).get(item["name"], dict()).get("hash")
        if current != item["hash"]:
            raise SyncError("schema_stale", "a schema record used by this intent changed", item)
