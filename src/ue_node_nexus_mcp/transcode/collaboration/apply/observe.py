"""Isolated exports capture current memory without changing workspace bases."""

from __future__ import annotations

from pathlib import Path
from uuid import uuid4

from ...paths import object_path
from ...sync_project import SyncError, call_ok, export_operation
from ...sync_status import query_ue
from ..history import History
from ..semantic.snapshot import from_raw
from ..store.io import atomic_write, canonical, confined, digest, read_json


def bind(bridge, context, store) -> dict:
    if not context.bridge_available:
        raise SyncError("bridge_unavailable", "this operation requires the bound editor")
    data = call_ok(bridge, "transcode_root_set", dict(root=str(context.root), collaboration_root=str(store.root), project_id=store.project_id)).get("data") or dict()
    if data.get("collaboration_version") != 1 or not data.get("editor_epoch"):
        raise SyncError("protocol_mismatch", "collaboration requires matching core and VFX plugins with protocol 1")
    return data


def scene_selector(asset: str, snapshot: dict | None, requested: dict | None = None) -> dict:
    if requested:
        return requested
    map_path, _, name = asset.partition("#")
    raw = (snapshot or dict()).get("raw", dict())
    actors = [dict(id=row["id"], level_path=row.get("level_path", map_path)) for row in raw.get("actors", [])]
    return dict(map_path=map_path, name=name, actors=actors)


def capture(bridge, context, store, assets: list[str] | None = None, *,
            reference: str | None = None, discover=False, selectors=None, persist=True) -> dict:
    binding = bind(bridge, context, store)
    history = History(store)
    previous = store.ref("refs/ue/observed")
    baseline = reference or previous
    entries = history.entries(previous)
    identities = history.entries(baseline)
    identities.update(entries)
    selected = set(assets or [])
    if assets is None:
        selected.update(identities)
    core_paths = [asset for asset in selected if "#" not in asset]
    infos, known = query_ue(bridge, context, core_paths, discover=discover, include_stubs=True)
    if not known:
        raise SyncError("observation_failed", "editor did not return a complete asset status")
    if discover:
        selected.update(infos)
    identifier = uuid4().hex
    directory = store.root / "observations" / identifier
    groups, raw_by_asset = dict(), dict()
    for asset in sorted(selected):
        prior = store.objects.data(identities[asset], "snapshot") if asset in identities else None
        if "#" in asset:
            file = directory / (digest(asset) + ".json")
            selector = scene_selector(asset, prior, (selectors or dict()).get(asset))
            call_ok(bridge, "scene_export", dict(selector, out_file=str(file)))
            raw_by_asset[asset] = read_json(file)
        elif asset in infos:
            groups.setdefault(export_operation(infos[asset].kind), []).append(asset)
        else:
            entries.pop(asset, None)
    for operation, paths in groups.items():
        out_dir = directory / operation
        data = call_ok(bridge, operation, dict(asset_paths=paths, out_dir=str(out_dir), include_stubs=True)).get("data") or dict()
        for row in data.get("assets", []):
            path, file = (row["asset_path"], row["file"]) if isinstance(row, dict) else (row[0], row[2])
            file = confined(out_dir, file)
            raw_by_asset[object_path(path)] = read_json(file)
        missing = set(paths) - raw_by_asset.keys()
        if missing:
            raise SyncError("observation_failed", "one or more selected assets were not exported", dict(assets=sorted(missing), skipped=data.get("skipped")))
    revisions = dict()
    for asset, raw in raw_by_asset.items():
        if not raw.get("live_revision") or raw.get("editor_epoch") != binding["editor_epoch"]:
            raise SyncError("protocol_mismatch", "export is missing an authoritative memory revision", dict(asset=asset))
        if raw.get("schema_key") != context.schema_key:
            raise SyncError("schema_stale", "editor schema changed; refresh before recording an observation", dict(asset=asset))
        if raw.get("unavailable"):
            raise SyncError("scene_unavailable", "scene has unloaded members", dict(asset=asset))
        prior = store.objects.data(identities[asset], "snapshot") if asset in identities else None
        snapshot = from_raw(raw, prior, schema=context.schema)
        entries[asset] = store.snapshot(snapshot, context.schema)
        revisions[asset] = dict(revision=raw["live_revision"], editor_epoch=raw["editor_epoch"], dirty=raw.get("dirty", False),
                                saved_hash=raw.get("saved_hash", ""), content_revision=raw.get("content_revision"))
    tree = history.tree(entries)
    commit = previous
    if not previous or history.commit(previous)["tree"] != tree:
        commit = history.create(tree, [previous] if previous else [], "Observe current UE memory", "UE", "external_ue", observed=True, saved=all(not item["dirty"] for item in revisions.values()))
    record = dict(id=identifier, commit=commit, tree=tree, revisions=revisions, assets=sorted(selected),
                  editor_epoch=binding["editor_epoch"], previous=previous, raw_files=str(directory), generation=0)
    atomic_write(directory / "observation.json", canonical(record))
    if persist:
        store.put_record("observation", identifier, record, [commit], expected=0)
        if previous != commit:
            store.move("refs/ue/observed", commit, previous, "UE", "fetch")
    store.event("observation", observation_id=identifier, commit_id=commit, assets=len(selected), exports=len(raw_by_asset), persisted=persist)
    return dict(record, raw=raw_by_asset)
