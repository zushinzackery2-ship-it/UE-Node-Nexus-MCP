"""Isolated exports capture current memory without changing workspace bases."""

from __future__ import annotations

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


def remembered(store, context, epoch: str) -> dict:
    """Which snapshot each asset's memory revision was measured against.

    An editor restart or a schema change invalidates every measurement at once;
    anything finer is decided per asset against the snapshot it names.
    """
    record = store.record("memory", "observed")
    if not record or record["editor_epoch"] != epoch or record["schema_key"] != context.schema_key:
        return dict()
    return record["entries"]


def remember(store, context, commit: str, epoch: str, entries: dict, revisions: dict) -> None:
    known = remembered(store, context, epoch)
    memo = dict((asset, known[asset]) for asset in entries if asset in known and known[asset][0] == entries[asset])
    memo.update((asset, [entries[asset], revisions[asset]]) for asset in revisions if asset in entries)
    store.put_record("memory", "observed", dict(commit=commit, editor_epoch=epoch, schema_key=context.schema_key, entries=memo), [commit])


def carried(known: list | None, snapshot: str, info) -> bool:
    """A saved asset whose package hash never moved still holds observed memory.

    The editor answers ``transcode_status`` for the whole project in one call,
    while exporting is per asset; re-exporting thousands of untouched assets to
    rediscover bytes the editor just reported as unchanged is the whole cost.
    """
    if not known or known[0] != snapshot or known[1]["dirty"] or info.dirty:
        return False
    return bool(info.saved_hash) and known[1]["saved_hash"] == info.saved_hash


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
    memory = remembered(store, context, binding["editor_epoch"])
    groups, raw_by_asset, revisions = dict(), dict(), dict()
    for asset in sorted(selected):
        if "#" in asset:
            # Only a scene needs its previous state here; loading every other
            # asset's snapshot to then not export it is the whole project.
            prior = store.objects.data(identities[asset], "snapshot") if asset in identities else None
            file = directory / (digest(asset) + ".json")
            selector = scene_selector(asset, prior, (selectors or dict()).get(asset))
            call_ok(bridge, "scene_export", dict(selector, out_file=str(file)))
            raw_by_asset[asset] = read_json(file)
        elif asset not in infos:
            entries.pop(asset, None)
        elif asset in entries and carried(memory.get(asset), entries[asset], infos[asset]):
            revisions[asset] = memory[asset][1]
        else:
            groups.setdefault(export_operation(infos[asset].kind), []).append(asset)
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
    # One transaction registers the whole export: a project-sized observation
    # otherwise pays a separate durable commit for every asset it recorded.
    with store.db.connection(write=True):
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
        remember(store, context, commit, binding["editor_epoch"], entries, revisions)
    store.event("observation", observation_id=identifier, commit_id=commit, assets=len(selected), exports=len(raw_by_asset), persisted=persist)
    return dict(record, raw=raw_by_asset)
