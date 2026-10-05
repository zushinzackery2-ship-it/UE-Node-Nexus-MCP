"""Resolve content mounts from installed plugin manifests and project enablement."""

from __future__ import annotations

import hashlib
from pathlib import Path

from .descriptors import read_descriptor


BASE_MOUNTS = frozenset(("Game", "Engine", "Script"))


def plugin_mounts(project: Path, engine: Path) -> dict:
    descriptor = read_descriptor(project)
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
    return dict((name, dict(path=str(manifests[name][0]), sha256=hashlib.sha256(manifests[name][0].read_bytes()).hexdigest()))
                for name in sorted(enabled) if manifests[name][1].get("CanContainContent"))


def requested_mounts(value) -> set[str]:
    if isinstance(value, dict):
        return set().union(*(requested_mounts(child) for child in value.values())) if value else set()
    if isinstance(value, list):
        return set().union(*(requested_mounts(child) for child in value)) if value else set()
    if isinstance(value, str) and value.startswith("/"):
        return set((value.split("/", 2)[1],))
    return set()
