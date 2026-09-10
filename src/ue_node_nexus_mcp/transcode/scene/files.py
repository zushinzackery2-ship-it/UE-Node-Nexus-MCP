"""Scene snapshots commit text, baseline and state through the shared journal."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from ..emitter import emit
from ..parser import parse
from ..push.commit import commit_files
from ..sync_files import backup_text, canonical_hash, read_json, read_text
from ..sync_project import SyncError
from .codec import from_raw
from .paths import identity, state_path
from .state import SceneRecord, SceneState, record


def read_base(project: Path, item: SceneRecord) -> dict[str, Any] | None:
    path = project / item.base_file
    value = read_json(path)
    if path.exists() and value is None:
        raise SyncError("scene_base_invalid", f"cannot read scene baseline: {path}")
    if value is not None and identity(value.get("map_path", ""), value.get("name", "")) != item.key:
        raise SyncError("scene_base_invalid", f"baseline identity differs: {path}")
    return value


def read_document(project: Path, file: Path):
    text = file.read_text(encoding="utf-8")
    document, sink = parse(text, file=file.relative_to(project).as_posix())
    return document, text, sink


def ensure_unchanged(file: Path, expected: str | None) -> None:
    if read_text(file) != expected:
        raise SyncError("local_changed_during_sync", f"local edits were preserved: {file}")


def write_snapshot(project: Path, state: SceneState, raw: dict[str, Any], aliases: dict[str, str],
                   item: SceneRecord, expected: str | None) -> SceneRecord:
    if identity(raw.get("map_path", ""), raw.get("name", "")) != item.key or raw.get("unavailable"):
        raise SyncError("scene_snapshot_invalid", "snapshot identity or loaded membership differs")
    file, base_file = project / item.file, project / item.base_file
    document, new_aliases = from_raw(raw, aliases)
    text = emit(document)
    stored = dict(raw, aliases=new_aliases)
    updated = SceneState(dict(state.records))
    accepted = record(project, item.key, stored, file, base_file, canonical_hash(text))
    updated.put(accepted)
    ensure_unchanged(file, expected)
    if expected is not None and expected != text:
        backup_text(project, file)
    contents = dict()
    if expected != text:
        contents[file] = text
    contents[base_file] = json.dumps(stored, indent=1, ensure_ascii=False)
    contents[state_path(project)] = updated.serialize()
    commit_files(contents)
    state.records = updated.records
    return accepted
