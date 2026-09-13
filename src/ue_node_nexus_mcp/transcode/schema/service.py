"""Reflection collection, targeted model queries and context snapshots."""

from __future__ import annotations

from pathlib import Path
from uuid import uuid4

from ..collaboration.store.io import atomic_write, canonical, digest, read_json
from ..paths import object_path, schema_dir
from ..sync_project import SyncError, call_ok, ensure_root_registered
from .catalog import migrate, publish, read_entry
from .lock import SchemaLock
from .records import FAMILIES


def refresh(bridge, context) -> SchemaLock:
    ensure_root_registered(bridge, context)
    staging = context.root / ".nexus" / "schema" / (".collect-" + uuid4().hex)
    data = call_ok(bridge, "schema_export", dict(out_dir=str(staging))).get("data") or dict()
    key = str(data.get("schema_key") or context.schema_key)
    if not key or not (staging / "key.json").is_file():
        raise SyncError("schema_export_failed", "provider did not publish a schema identity")
    core = read_json(staging / "key.json")
    environment = dict(core.get("environment", dict()), project_file=context.project_file,
                       engine_version=context.engine_version, schema_key=key)
    coverage = dict(core="registered_types", blueprint_pins="context_required", functions="registered_callable_index")
    try:
        call_ok(bridge, "vfx_transcode_export", dict(schema_out_dir=str(staging), asset_paths=[]))
        coverage["niagara"] = "provider_loaded"
    except SyncError as exc:
        coverage["niagara"] = dict(state="provider_unavailable", code=exc.code, reason=str(exc))
        context.warnings.append(f"Niagara schema provider: {exc}")
    if data.get("functions"):
        atomic_write(staging / "functions.cache.json", canonical(data["functions"]))
    target = schema_dir(context.root, key)
    migrate(target, key, staging, environment, coverage)
    context.schema_key = key
    context.schema = SchemaLock(target, key)
    return context.schema


def query(lock: SchemaLock, category=None, text=None, limit=40, cursor=0, details=False) -> dict:
    if category and category not in set(FAMILIES.values()):
        raise SyncError("invalid_category", "category must be blueprint, material, niagara, scene, asset or common")
    manifest = lock.info()
    matches = []
    for family, entries in manifest.get("tables", dict()).items():
        if category and FAMILIES[family] != category:
            continue
        for name, entry in entries.items():
            if text and not any(text.lower() in alias.lower() for alias in entry["aliases"]):
                continue
            matches.append((family, name, entry))
    matches.sort(key=lambda item: (item[0], item[1]))
    start, count = max(0, int(cursor)), max(1, min(int(limit), 200))
    rows = []
    for family, name, entry in matches[start:start + count]:
        row = dict(name=name, family=family, **entry)
        row["file"] = str(lock.directory / entry["file"])
        if details:
            row["definition"] = read_entry(lock, family, name, entry)
        rows.append(row)
    return dict(schema_key=lock.key, generation=manifest.get("generation"), index=str(lock.directory / "index.md"),
                coverage=manifest.get("coverage", dict()), total=len(matches), rows=rows,
                cursor=start + count if start + count < len(matches) else None)


def details(bridge, context, reference: str) -> None:
    ensure_root_registered(bridge, context)
    staging = context.root / ".nexus" / "schema" / (".details-" + uuid4().hex)
    data = call_ok(bridge, "schema_export", dict(out_dir=str(staging), functions=[reference], details_only=True)).get("data") or dict()
    records = data.get("functions", dict())
    if not records:
        raise SyncError("schema_unknown", "function was not resolved in the current project", dict(query=reference))
    publish(context.schema.directory, context.schema_key, dict(functions=records), incremental=True)
    context.schema = SchemaLock(context.schema.directory, context.schema_key)


def target_context(bridge, context, target: str, conditions: dict | None = None) -> dict:
    from ..sync_status import query_ue

    conditions = conditions or dict()
    target = object_path(target)
    infos, known = query_ue(bridge, context, [target])
    if not known or target not in infos:
        raise SyncError("context_unavailable", "target is unloaded, missing or its provider is unavailable", dict(target=target))
    kind = infos[target].kind
    operation = "vfx_transcode_export" if kind.startswith("niagara_") else "transcode_export"
    directory = context.project / ".nexus" / "pending" / ("context-" + uuid4().hex)
    data = call_ok(bridge, operation, dict(asset_paths=[target], out_dir=str(directory), include_stubs=True)).get("data") or dict()
    files = [row.get("file") if isinstance(row, dict) else row[2] for row in data.get("assets", [])]
    if not files:
        raise SyncError("context_unavailable", "target export produced no context")
    raw = read_json(Path(files[0]))
    from ..collaboration.semantic.snapshot import raw_evidence

    definition = raw_evidence(raw)
    record = dict(target=target, kind=kind, schema_key=context.schema_key, context=conditions,
                  context_hash=digest(conditions), revision=raw.get("revision"), editor_epoch=raw.get("editor_epoch"),
                  interface_hash=digest(definition), definition=definition, coverage="context_required" if conditions else "target_export",
                  freshness="observed", conditions_evaluated=False if conditions else True)
    record_hash = digest(record)
    path = context.schema.directory / "contexts" / f"{record_hash}.json"
    atomic_write(path, canonical(record))
    manifest = context.schema.info()
    manifest.setdefault("contexts", dict())[target + ":" + record["context_hash"]] = dict(file=str(path.relative_to(context.schema.directory)), hash=record_hash, revision=record["revision"])
    atomic_write(context.schema.directory / "key.json", canonical(manifest))
    return dict(file=str(path), **record)


def run(bridge, context, options: dict) -> dict:
    if options.get("revision"):
        return historical(context, options)
    refresh_requested = options.get("refresh", not bool(options))
    if refresh_requested:
        if not context.bridge_available:
            raise SyncError("bridge_unavailable", "refresh requires the bound editor")
        refresh(bridge, context)
    lock = context.schema
    if lock is None or not lock.available:
        raise SyncError("schema_missing", "initialize this project's schema first")
    if lock.info().get("format") != 2:
        migrate(lock.directory, lock.key, environment=dict(project_file=context.project_file))
    if options.get("function"):
        if context.bridge_available and options.get("refresh", True):
            details(bridge, context, options["function"])
            lock = context.schema
        options = dict(options, query=options["function"], details=True)
    target = options.get("target")
    if target and context.bridge_available and options.get("refresh", True):
        result = target_context(bridge, context, target, options.get("context"))
        return dict(schema_key=lock.key, context=result)
    if target:
        key = object_path(target) + ":" + digest(options.get("context") or dict())
        entry = lock.info().get("contexts", dict()).get(key)
        if not entry:
            raise SyncError("context_required", "no bound target context is cached", dict(target=target))
        return dict(schema_key=lock.key, context=read_json(lock.directory / entry["file"]), freshness="unknown")
    result = query(lock, options.get("category"), options.get("query"), options.get("limit", 40), options.get("cursor", 0), options.get("details", False))
    result["freshness"] = "observed" if refresh_requested else "cached" if context.bridge_available else "unknown"
    return result


def historical(context, options: dict) -> dict:
    from ..collaboration.history import History
    from ..collaboration.store import Store

    store = Store(context.project / ".nexus" / "collaboration", context.project_file)
    workspace = store.record("workspace", options.get("workspace_id", ""))
    history = History(store)
    revision = history.resolve(options["revision"], workspace["head"] if workspace else None)
    records = dict()
    for identifier in history.entries(revision).values():
        snapshot = store.objects.data(identifier, "snapshot")
        for reference in snapshot.get("schema_objects", []):
            records[reference] = store.objects.data(reference, "schema")
    rows = [dict(object_id=key, definition=record) for key, record in sorted(records.items())
            if (not options.get("category") or record["category"] == options["category"])
            and (not options.get("query") or any(options["query"].lower() in value.lower() for value in record["aliases"]))]
    from ..collaboration.workspace.commands import paged

    return paged(rows, options, revision=revision, freshness="historical")
