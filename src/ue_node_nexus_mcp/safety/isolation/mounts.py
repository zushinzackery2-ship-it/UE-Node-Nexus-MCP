"""Resolve content mounts from installed plugin manifests and project enablement."""

from __future__ import annotations

import hashlib
from pathlib import Path
import re

from .descriptors import read_descriptor
from ...instances.errors import require


BASE_MOUNTS = frozenset(("Game", "Engine", "Script"))


def plugin_mounts(project: Path, engine: Path) -> dict:
    descriptor = read_descriptor(project)
    project_root = Path(project).resolve().parent
    manifests = dict()
    for root in (engine / "Engine/Plugins", project.parent / "Plugins"):
        if root.is_dir():
            for path in root.rglob("*.uplugin"):
                data = read_descriptor(path)
                manifests[path.stem] = (path, data)
    overrides = dict((item["Name"], item.get("Enabled", True)) for item in descriptor.get("Plugins", []))
    enabled = set(name for name, (_, data) in manifests.items()
                  if overrides.get(name, data.get("EnabledByDefault", False)))
    pending = list(enabled)
    while pending:
        name = pending.pop()
        for dependency in manifests[name][1].get("Plugins", []):
            target = dependency["Name"]
            if dependency.get("Enabled", True) and overrides.get(target, True) and target in manifests and target not in enabled:
                enabled.add(target)
                pending.append(target)
    result = dict()
    for name in sorted(enabled):
        path, data = manifests[name]
        if not data.get("CanContainContent"):
            continue
        directory = path.parent.resolve()
        result[name] = dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                            content=str(directory / "Content"), project_local=directory.is_relative_to(project_root))
    return result


def writable_mounts(mounts: dict) -> set[str]:
    """Content roots a validation worker may save: the project mount and project-local plugins."""
    return set(("Game",)) | set(name for name, record in mounts.items() if record.get("project_local"))


def project_mounts(project: Path, environment: dict) -> dict:
    """Bind live native registrations only to real directories owned by this project."""
    from .snapshot import IGNORED, real_path

    root = project.absolute().parent
    measured = environment.get("project_content_mounts", {})
    require(isinstance(measured, dict), "isolation_mount_invalid", "project mount measurements must be a map")
    result = {}
    for name, relative in measured.items():
        require(isinstance(name, str) and re.fullmatch(r"[A-Za-z0-9_]+", name) and name not in ("Engine", "Script"),
                "isolation_mount_invalid", "invalid project content root", mount=name)
        require(isinstance(relative, str) and relative and "\\" not in relative and ":" not in relative,
                "isolation_path_rejected", "mount directory must be project-relative", mount=name, path=relative)
        parts = relative.split("/")
        require(all(part not in ("", ".", "..") and part.casefold() not in {key.casefold() for key in IGNORED}
                    for part in parts) and not Path(relative).is_absolute(),
                "isolation_path_rejected", "mount directory must be a copied project input", mount=name, path=relative)
        directory = root / relative
        for ancestor in (directory, *directory.parents):
            if ancestor.exists() or ancestor.is_symlink():
                real_path(ancestor)
            if ancestor == root:
                break
        require(directory.is_dir() and directory.resolve().is_relative_to(root.resolve()),
                "isolation_path_rejected", "mount directory must exist inside the project", mount=name, path=str(directory))
        result[name] = dict(content=str(directory), relative=relative, project_local=True, measured=True)
    return result


def requested_mounts(value) -> set[str]:
    if isinstance(value, dict):
        return set().union(*(requested_mounts(child) for child in value.values())) if value else set()
    if isinstance(value, list):
        return set().union(*(requested_mounts(child) for child in value)) if value else set()
    if isinstance(value, str) and value.startswith("/"):
        return set((value.split("/", 2)[1],))
    return set()
