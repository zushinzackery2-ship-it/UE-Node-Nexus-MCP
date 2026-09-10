"""File-based scene transport; accepted mirror files are never bridge outputs."""

from __future__ import annotations

from typing import Any

from ..sync_files import read_json
from ..sync_project import BridgeCall, ProjectContext, SyncError, call_ok, ensure_root_registered
from .paths import identity, storage
from .state import SceneRecord


def selector(item: SceneRecord, *snapshots: dict[str, Any] | None, rebind: bool = False) -> dict[str, Any]:
    actors = dict()
    for snapshot in snapshots:
        for row in (snapshot or dict()).get("actors", []):
            actors[row["id"]] = dict(id=row["id"], level_path=row.get("level_path", item.map_path))
    return dict(map_path=item.map_path, name=item.name, actors=list(actors.values()), rebind=rebind)


def validate_snapshot(raw: dict[str, Any] | None, item: SceneRecord) -> dict[str, Any]:
    if not raw or raw.get("kind") != "scene" or not isinstance(raw.get("actors"), list):
        raise SyncError("scene_export_invalid", f"unreadable scene snapshot: {item.key}")
    if identity(raw.get("map_path", ""), raw.get("name", "")) != item.key or not raw.get("revision"):
        raise SyncError("scene_export_invalid", f"unexpected scene snapshot: {item.key}")
    return raw


def export_scene(bridge: BridgeCall, context: ProjectContext, item: SceneRecord,
                 request: dict[str, Any], phase: str) -> dict[str, Any]:
    ensure_root_registered(bridge, context)
    file = storage(context.project, item.key, phase)
    file.unlink(missing_ok=True)
    call_ok(bridge, "scene_export", dict(request, out_file=str(file)))
    return validate_snapshot(read_json(file), item)


def status_scene(bridge: BridgeCall, context: ProjectContext, request: dict[str, Any]) -> dict[str, Any]:
    return call_ok(bridge, "scene_status", request).get("data") or dict()
