"""Immutable classified records with a single atomically published index."""

from __future__ import annotations

import re
from pathlib import Path

from ..storage.io import atomic_write, canonical, digest, read_json, confined
from ..errors import SyncError
from .names import CLASS_TIERS, OWNER_TIERS, RECORD_TIERS, class_spellings
from .records import FAMILIES, normalize

# The first release wrote one table per family; ``migrate`` reads them.
LEGACY_FILES = dict(material_expression="classes.material_expression.json", k2node="classes.k2node.json",
                    asset="classes.asset.json", component="classes.component.json",
                    niagara_renderer="classes.niagara_renderer.json", material_functions="material_functions.json",
                    niagara_modules="niagara_modules.json", functions="functions.cache.json",
                    callable_functions="functions.index.json", types="types.json")
# Families whose records are classes: a short spelling is a class name, not a member.
CLASS_FAMILIES = frozenset(("material_expression", "k2node", "asset", "component", "niagara_renderer"))


def publish(directory: Path, key: str, tables: dict, environment: dict | None = None,
            coverage: dict | None = None, incremental=False) -> dict:
    from ..transaction.lock import MirrorLock

    with MirrorLock(directory, directory / "index.lock"):
        return _publish(directory, key, tables, environment, coverage, incremental)


def ensure_format(directory: Path, key: str) -> dict:
    """The format-2 manifest of ``directory``, converting first-release tables once.

    Conversion rewrites only this catalog's own files, so an offline reader needs
    no editor; the lock makes concurrent first readers convert exactly once.
    """
    from ..transaction.lock import MirrorLock

    with MirrorLock(directory, directory / "index.lock"):
        manifest = read_json(directory / "key.json")
        if manifest.get("format") == 2:
            return manifest
        return _publish(directory, key, legacy_tables(directory), None, None, False)


def legacy_tables(source: Path) -> dict:
    return dict((family, read_json(source / name)) for family, name in LEGACY_FILES.items() if (source / name).is_file())


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
                                 category=record["category"], coverage=record["coverage"], bridge=record["bridge"],
                                 deprecated=bool(record.get("deprecated")))
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
    return publish(directory, key, legacy_tables(source or directory), environment, coverage)


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


def lookup_record(lock, family: str, name: str) -> dict | None:
    """The record ``name`` denotes: a key, a path, an owner-qualified member or a short name."""
    if not name:
        return None
    index = lock.index(family)
    key = index.find([name, name + "." + name.rsplit("/", 1)[-1]], RECORD_TIERS)
    return read_entry(lock, family, key, index.entries[key]) if key else None


def reference(lock, family: str, path: str, short: str) -> str:
    """The spelling the mirror writes for the record at ``path``: ``short`` while it resolves back to it.

    A short name that several records answer to, or that names another record,
    must stay out of the mirror: the same spelling would mean one record to the
    validator and another to the editor. Class tables list every reflected class,
    Blueprint-generated ones included, so a class they lack is newer than the
    catalog and keeps its name; the module index lists only the scripts loaded at
    export, so an unlisted script is written in full.
    """
    if lock is None or not path or not short or not lock.info().get("tables", dict()).get(family):
        return short or path
    index = lock.index(family)
    classes = family in CLASS_FAMILIES
    try:
        key = index.find(class_spellings(family, short) if classes else [short], CLASS_TIERS if classes else RECORD_TIERS)
    except SyncError as exc:
        if exc.code != "schema_ambiguous":
            raise
        return path
    if key is None:
        return short if classes else path
    return short if str(index.entries[key].get("path") or key) == path else path


def lookup_class(lock, family: str, name: str, *, aliases: bool = True) -> dict | None:
    if not name:
        return None
    index = lock.index(family)
    key = index.find(class_spellings(family, name), CLASS_TIERS if aliases else OWNER_TIERS)
    return read_entry(lock, family, key, index.entries[key]) if key else None


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
