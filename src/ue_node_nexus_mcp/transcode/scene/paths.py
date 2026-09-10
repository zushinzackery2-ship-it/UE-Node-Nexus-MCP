"""Scene identity and storage are independent of asset package paths."""

from __future__ import annotations

import hashlib
import re
from pathlib import Path

from ..paths import package_name
from ..sync_project import SyncError


def identity(map_path: str, name: str) -> str:
    level = package_name(map_path)
    if not level.startswith("/Game/") or any(part in ("", ".", "..") for part in level[6:].split("/")):
        raise SyncError("invalid_scene_map", "scene map must be a /Game package")
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_-]*", name):
        raise SyncError("invalid_scene_name", "scene name must be an identifier")
    return f"{level}#{name}"


def scene_path(project: Path, map_path: str, name: str) -> Path:
    key = identity(map_path, name)
    level = key.split("#", 1)[0]
    result = project / "Scenes" / level[6:] / f"{name}.scene.nexus"
    result.resolve().relative_to(project.resolve())
    return result


def storage(project: Path, key: str, kind: str) -> Path:
    token = hashlib.sha256(key.encode("utf-8")).hexdigest()
    return project / ".nexus" / "scenes" / kind / f"{token}.json"


def state_path(project: Path) -> Path:
    return project / ".nexus" / "scenes" / "state.json"


def is_scene_file(path: str | Path) -> bool:
    return str(path).endswith(".scene.nexus")
