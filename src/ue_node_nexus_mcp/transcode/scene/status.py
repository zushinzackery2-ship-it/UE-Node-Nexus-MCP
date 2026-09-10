"""Scene status compares local text and the loaded editor snapshot independently."""

from __future__ import annotations

from typing import Any

from ..sync_files import canonical_hash, read_text
from ..sync_project import BridgeCall, ProjectContext, SyncError
from .backend import selector, status_scene
from .files import read_base
from .state import SceneRecord


def inspect(bridge: BridgeCall, context: ProjectContext, item: SceneRecord) -> dict[str, Any]:
    base = read_base(context.project, item)
    text = read_text(context.project / item.file)
    local_changed = text is None or canonical_hash(text) != item.file_hash
    row = dict(scene=item.key, file=item.file, kind="scene", ue_known=False,
               state="local-new" if base is None else "unknown", ue_dirty=None)
    if not context.bridge_available:
        return row
    try:
        live = status_scene(bridge, context, selector(item, base, rebind=bool((base or dict()).get("allow_identity_adoption"))))
    except SyncError as exc:
        row.update(state="unavailable", error=exc.code, message=str(exc))
        return row
    row.update(ue_known=True, ue_dirty=bool(live.get("dirty")), live_revision=live.get("revision"),
               missing=live.get("missing", []), unavailable=live.get("unavailable", []))
    row["identity_adoption_pending"] = bool((base or dict()).get("allow_identity_adoption"))
    if live.get("unavailable"):
        row["state"] = "unavailable"
    elif base is None:
        row["state"] = "local-new"
    elif text is None:
        row["state"] = "local-deleted"
    else:
        changed = live.get("revision") != item.live_revision
        row["state"] = ("both-modified" if changed else "local-modified") if local_changed else ("ue-modified" if changed else "clean")
        if base.get("actors") and live.get("actor_count") == 0 and live.get("missing") and not local_changed:
            row["state"] = "ue-deleted"
    return row


def status_scenes(bridge: BridgeCall, context: ProjectContext, items: list[SceneRecord], include_clean: bool) -> dict[str, Any]:
    rows = [inspect(bridge, context, item) for item in items]
    counts = dict()
    for row in rows:
        counts[row["state"]] = counts.get(row["state"], 0) + 1
    return dict(rows=[row for row in rows if include_clean or row["state"] != "clean"], counts=counts, total=len(rows))
