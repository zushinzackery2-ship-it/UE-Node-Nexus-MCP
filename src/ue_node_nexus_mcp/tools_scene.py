"""Internal file transport for the scene text mirror."""

from __future__ import annotations

from typing import Any

from .runtime import call_bridge, hidden_tool


@hidden_tool()
def scene_export(map_path: str, name: str, out_file: str, actors: list[dict[str, Any]] | None = None,
                 actor_paths: list[str] | None = None, rebind: bool = False) -> dict[str, Any]:
    """Export loaded scene members to a JSON file inside the registered mirror root."""
    return call_bridge("scene_export", dict(map_path=map_path, name=name, out_file=out_file,
                                            actors=actors or [], actor_paths=actor_paths or [], rebind=rebind))


@hidden_tool()
def scene_status(map_path: str, name: str, actors: list[dict[str, Any]] | None = None,
                 actor_paths: list[str] | None = None, rebind: bool = False) -> dict[str, Any]:
    """Inspect loaded membership, revision and dirty packages without writing metadata."""
    return call_bridge("scene_status", dict(map_path=map_path, name=name, actors=actors or [],
                                            actor_paths=actor_paths or [], rebind=rebind))


@hidden_tool()
def scene_apply(plan_file: str, out_file: str, dry_run: bool = True, save: bool = True,
                apply_id: str | None = None, repository: str = "", collaboration_version: int = 1,
                expected_revision: str | None = None, read_set: list[dict[str, Any]] | None = None) -> dict[str, Any]:
    """Preflight and apply one scene plan; JSON outputs use pending storage."""
    from .tools_transcode import _protocol

    return call_bridge("scene_apply", dict(plan_file=plan_file, out_file=out_file, dry_run=dry_run, save=save,
                      **_protocol(apply_id, repository, collaboration_version, expected_revision, False, read_set)))
