"""Read-only export with local edit protection and explicit identity re-adoption."""

from __future__ import annotations

from typing import Any

from ..sync_files import canonical_hash, read_text
from ..sync_project import BridgeCall, ProjectContext, SyncError
from .backend import export_scene, selector
from .files import read_base, write_snapshot
from .paths import storage
from .selection import check_membership
from .state import SceneRecord, SceneState


def pull_one(bridge: BridgeCall, context: ProjectContext, state: SceneState, item: SceneRecord,
             options: dict[str, Any]) -> dict[str, Any]:
    base = read_base(context.project, item)
    text = read_text(context.project / item.file)
    if base and text is None and options.get("force") != "ue":
        raise SyncError("scene_local_deleted", f"local deletion was preserved: {item.file}; force=ue explicitly restores UE")
    if text is not None and (base is None or canonical_hash(text) != item.file_hash) and options.get("force") != "ue":
        raise SyncError("scene_local_modified", f"pull would replace local edits: {item.file}; force=ue explicitly adopts UE")
    request = selector(item, base, rebind=options.get("force") == "ue")
    explicit = options.get("scene")
    if explicit is not None and "actor_paths" in explicit:
        request = dict(map_path=item.map_path, name=item.name, actor_paths=explicit["actor_paths"], rebind=request["rebind"])
    raw = export_scene(bridge, context, item, request, "pull")
    if raw.get("unavailable") or (explicit is not None and raw.get("missing")):
        raise SyncError("scene_unavailable", f"selected actors are not all loaded: {item.key}",
                        dict(unavailable=raw.get("unavailable", []), missing=raw.get("missing", [])))
    check_membership(context.project, state, item, raw)
    if options.get("force") == "ue":
        raw["allow_identity_adoption"] = True
    write_snapshot(context.project, state, raw, (base or dict()).get("aliases", dict()), item, text)
    storage(context.project, item.key, "recovery").unlink(missing_ok=True)
    storage(context.project, item.key, "pull").unlink(missing_ok=True)
    return dict(scene=item.key, file=item.file, action="pulled", actors=len(raw["actors"]),
                identity_adopted=options.get("force") == "ue")


def pull_scenes(bridge: BridgeCall, context: ProjectContext, state: SceneState,
                items: list[SceneRecord], options: dict[str, Any]) -> dict[str, Any]:
    rows, diagnostics = [], []
    for item in items:
        try:
            rows.append(pull_one(bridge, context, state, item, options))
        except (OSError, SyncError) as exc:
            code = exc.code if isinstance(exc, SyncError) else "scene_io_failed"
            rows.append(dict(scene=item.key, file=item.file, action="failed", error=code, message=str(exc)))
            diagnostics.append(f"{item.file}: {code}: {exc}")
    return dict(rows=rows, diagnostics=diagnostics, error_count=len(diagnostics))
