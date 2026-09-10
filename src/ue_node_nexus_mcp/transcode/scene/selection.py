"""Resolve managed and newly authored scene files without conflating asset paths."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from ..sync_files import mirrored_assets
from ..sync_project import ProjectContext, SyncError
from .files import read_base
from .paths import identity, is_scene_file, scene_path, storage
from .state import SceneRecord, SceneState


def new_record(context: ProjectContext, map_path: str, name: str, file: Path | None = None) -> SceneRecord:
    key = identity(map_path, name)
    map_path = key.split("#", 1)[0]
    file = file or scene_path(context.project, map_path, name)
    return SceneRecord(key, map_path, name, file.relative_to(context.project).as_posix(),
                       storage(context.project, key, "base").relative_to(context.project).as_posix())


def resolve_file(context: ProjectContext, text: str) -> Path:
    path = Path(text)
    candidates = [path] if path.is_absolute() else [context.project / path, context.root / path, Path.cwd() / path]
    for candidate in candidates:
        if candidate.exists() and candidate.resolve().is_relative_to(context.project.resolve()):
            return candidate.resolve()
    candidate = candidates[0].resolve()
    if not candidate.is_relative_to(context.project.resolve()):
        raise SyncError("invalid_scene_path", f"scene path leaves mirror project: {text}")
    return candidate


def records(context: ProjectContext, state: SceneState) -> list[SceneRecord]:
    found = dict(state.records)
    known_files = dict(((context.project / item.file).resolve(), item.key) for item in found.values())
    for file in sorted((context.project / "Scenes").rglob("*.scene.nexus")):
        if file.resolve() in known_files:
            continue
        if not file.resolve().is_relative_to(context.project.resolve()):
            raise SyncError("invalid_scene_path", f"scene link leaves mirror: {file}")
        relative = file.relative_to(context.project / "Scenes")
        item = new_record(context, "/Game/" + relative.parent.as_posix(), file.name[:-len(".scene.nexus")], file)
        if item.key in found:
            raise SyncError("duplicate_scene", f"multiple files claim {item.key}")
        found[item.key] = item
    return sorted(found.values(), key=lambda item: item.key)


def select(context: ProjectContext, state: SceneState, paths: list[str] | None,
           options: dict[str, Any]) -> tuple[list[str] | None, list[SceneRecord]]:
    available = records(context, state)
    explicit = options.get("scene")
    selected = dict()
    if explicit is not None:
        if not isinstance(explicit, dict) or set(explicit) - set(("map_path", "name", "actor_paths")):
            raise SyncError("invalid_scene", "scene accepts map_path, name and actor_paths")
        if not isinstance(explicit.get("map_path"), str) or not isinstance(explicit.get("name"), str):
            raise SyncError("invalid_scene", "scene requires map_path and name")
        actor_paths = explicit.get("actor_paths", [])
        if not isinstance(actor_paths, list) or not all(isinstance(path, str) and path for path in actor_paths):
            raise SyncError("invalid_scene", "actor_paths must contain nonempty actor paths")
        item = new_record(context, explicit["map_path"], explicit["name"])
        selected[item.key] = state.records.get(item.key, item)
    if not paths:
        return ([], list(selected.values())) if explicit is not None else (None, available)
    assets = []
    for text in paths:
        if text.startswith("/Game"):
            assets.append(text)
            continue
        file = resolve_file(context, text)
        matches = [item for item in available if (context.project / item.file).resolve() == file
                   or (context.project / item.file).resolve().is_relative_to(file)]
        if is_scene_file(file) and not matches:
            raise SyncError("scene_missing", f"no scene matches {text}")
        selected.update((item.key, item) for item in matches)
        scene_root = (context.project / "Scenes").resolve()
        if not is_scene_file(file) and not file.is_relative_to(scene_root):
            if file.is_dir() and matches:
                assets.extend(asset for asset, (_, mirror) in mirrored_assets(context.project).items()
                              if mirror.resolve().is_relative_to(file))
            else:
                assets.append(text)
    return assets, sorted(selected.values(), key=lambda item: item.key)


def check_membership(project: Path, state: SceneState, item: SceneRecord, raw: dict[str, Any]) -> None:
    ids = set(row["id"] for row in raw.get("actors", []))
    for other in state.records.values():
        if other.key == item.key or other.map_path != item.map_path:
            continue
        overlap = ids & set(row["id"] for row in (read_base(project, other) or dict()).get("actors", []))
        if overlap:
            raise SyncError("scene_membership_conflict", f"actors already belong to {other.key}: {', '.join(sorted(overlap))}")
