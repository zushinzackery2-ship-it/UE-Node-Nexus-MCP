"""Persist an attempted scene transaction and verify a partial retry against it."""

from __future__ import annotations

from typing import Any

from ..sync_files import read_json, write_json
from ..sync_project import ProjectContext, SyncError, now_iso
from ..values import values_equal
from .paths import storage
from .state import SceneRecord


def load(context: ProjectContext, item: SceneRecord) -> dict[str, Any] | None:
    path = storage(context.project, item.key, "recovery")
    value = read_json(path)
    if path.exists() and (not value or value.get("scene") != item.key):
        raise SyncError("scene_recovery_invalid", f"invalid recovery journal: {path}")
    return value


def record(context: ProjectContext, item: SceneRecord, text: str | None, before: dict[str, Any],
           desired: dict[str, Any], aliases: dict[str, str], plan: dict[str, Any],
           response: dict[str, Any], after: dict[str, Any] | None = None) -> str:
    path = storage(context.project, item.key, "recovery")
    token = plan.get("request_token")
    data = response.get("data") or dict()
    revision = (after or dict()).get("revision", "") if token and (after or dict()).get("request_token") == token else ""
    if not revision and token and data.get("request_token") == token:
        revision = data.get("revision", "")
    write_json(path, dict(scene=item.key, time=now_iso(), source=item.file, local_text=text,
                         before=before, desired=desired, aliases=aliases, plan=plan, response=response,
                         after_revision=revision))
    return path.relative_to(context.project).as_posix()


def _fields(row: dict[str, Any], defaults: dict[str, Any]) -> dict[str, Any]:
    fields = dict((key, row.get(key, defaults.get(key, ""))) for key in ("class", "level_path", "label", "folder", "parent", "transform"))
    fields["properties"] = dict(defaults.get("defaults", dict()), **row.get("properties", dict()))
    data = row.get("instance_data")
    if data is not None:
        fields["custom_data_count"] = data["custom_data_count"]
        fields["instances"] = dict((entry["id"], dict(transform=entry["transform"], custom_data=entry["custom_data"])) for entry in data["instances"])
    return fields


def _tree(raw: dict[str, Any], reference: dict[str, Any]) -> dict[str, Any]:
    refs = dict((row["id"], row) for row in reference.get("actors", []))
    result = dict()
    for actor in raw.get("actors", []):
        previous = refs.get(actor["id"], actor)
        entry = _fields(actor, previous)
        old_components = dict((row["id"], row) for row in previous.get("components", []))
        entry["components"] = dict((row["id"], _fields(row, old_components.get(row["id"], row))) for row in actor.get("components", []))
        result[actor["id"]] = entry
    return result


def _compatible(before: Any, desired: Any, current: Any) -> bool:
    if isinstance(current, dict):
        old = before if isinstance(before, dict) else dict()
        new = desired if isinstance(desired, dict) else dict()
        return all(_compatible(old.get(key), new.get(key), current.get(key)) for key in set(old) | set(new) | set(current))
    if current is None:
        return before is None or desired is None
    return any(candidate is not None and values_equal(str(current), str(candidate)) for candidate in (before, desired))


def can_resume(journal: dict[str, Any], current: dict[str, Any]) -> bool:
    if journal.get("after_revision") == current.get("revision"):
        return True
    before = journal.get("before") or dict()
    desired = journal.get("desired") or dict()
    if before.get("revision") == current.get("revision"):
        return True
    return _compatible(_tree(before, before), _tree(desired, before), _tree(current, before))
