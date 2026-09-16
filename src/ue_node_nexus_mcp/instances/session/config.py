"""Explicit project configuration; cwd supplies candidates, never a fuzzy identity."""

import os
from pathlib import Path

from ..errors import InstanceError
from ..identity.paths import project_identity


def consume_arguments(argv: list[str]) -> tuple[str | None, list[str]]:
    project = None
    remaining = []
    index = 0
    while index < len(argv):
        value = argv[index]
        if value == "--project":
            index += 1
            if index >= len(argv):
                raise ValueError("--project requires a .uproject path")
            project = argv[index]
        elif value.startswith("--project="):
            project = value.split("=", 1)[1]
        else:
            remaining.append(value)
        index += 1
    return project, remaining


def resolve_project(explicit: str | None, configured: str | None, bound: dict, cwd: Path) -> dict:
    path = explicit or configured or os.environ.get("UE_NEXUS_PROJECT_PATH") or bound.get("project_path")
    if path:
        return project_identity(path)
    for directory in (cwd, *cwd.parents):
        candidates = list(directory.glob("*.uproject"))
        if len(candidates) == 1:
            return project_identity(candidates[0])
        if candidates:
            raise InstanceError("project_required", "select an exact .uproject path",
                                dict(candidates=[str(item) for item in candidates]))
    raise InstanceError("project_required", "set --project, UE_NEXUS_PROJECT_PATH, or bridge_instance_ensure.project_path")
