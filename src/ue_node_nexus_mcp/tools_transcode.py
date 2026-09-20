"""Typed wrappers for the transcode (text mirror) bridge operations.

These exist so ``ue_capability_get(operation=..., detail="schema")`` can derive
payload schemas; the ``ue_sync`` facade drives them internally. Direct use is
for diagnosis only.
"""

from __future__ import annotations

from typing import Any

from .runtime import call_bridge, default_tool


@default_tool()
def transcode_root_set(root: str, collaboration_root: str | None = None, project_id: str | None = None) -> dict[str, Any]:
    """Register the absolute mirror root directory; export/apply refuse paths outside it."""
    payload = dict(root=root)
    if collaboration_root:
        payload.update(collaboration_root=collaboration_root, project_id=project_id)
    return call_bridge("transcode_root_set", payload)


@default_tool()
def transcode_status(asset_paths: list[str] | None = None, discover: bool = False, include_stubs: bool = False) -> dict[str, Any]:
    """Rows ``[object_path, class_path, kind, saved_hash, dirty]`` for the given (or all discovered /Game) assets."""
    return call_bridge("transcode_status", {"asset_paths": asset_paths or [], "discover": discover, "include_stubs": include_stubs})


@default_tool()
def transcode_export(asset_paths: list[str], out_dir: str, include_stubs: bool = False) -> dict[str, Any]:
    """Write raw JSON exports under ``out_dir`` (must be inside the registered root)."""
    return call_bridge("transcode_export", {"asset_paths": asset_paths, "out_dir": out_dir, "include_stubs": include_stubs})


@default_tool()
def transcode_apply(
    asset_path: str,
    kind: str,
    plan: list[dict[str, Any]],
    ids: dict[str, str] | None = None,
    create: bool = False,
    asset_class: str = "",
    out_dir: str = "",
    dry_run: bool = True,
    compile: bool = True,
    save: bool = True,
    apply_id: str | None = None, repository: str = "", collaboration_version: int = 1,
    expected_revision: str | None = None, expected_absent: bool = False,
    read_set: list[dict[str, Any]] | None = None, delete_asset: bool = False,
) -> dict[str, Any]:
    """Apply plan verbs (see transcode/plan.py) in one transaction; ``ids`` maps local ids to node GUIDs."""
    return call_bridge("transcode_apply", {
        "asset_path": asset_path, "kind": kind, "plan": plan, "ids": ids or {}, "create": create,
        "asset_class": asset_class, "out_dir": out_dir, "dry_run": dry_run, "compile": compile, "save": save,
        **_protocol(apply_id, repository, collaboration_version, expected_revision, expected_absent, read_set),
        "delete_asset": delete_asset,
    })


@default_tool()
def schema_export(out_dir: str, functions: list[str] | None = None, details_only: bool = False) -> dict[str, Any]:
    """Write the reflection schema lock files into ``out_dir``."""
    return call_bridge("schema_export", dict(out_dir=out_dir, functions=functions or [], details_only=details_only))


@default_tool()
def transcode_watch_set(enabled: bool, out_dir: str = "") -> dict[str, Any]:
    """Toggle automatic raw export of mirrored assets on editor save."""
    return call_bridge("transcode_watch_set", {"enabled": enabled, "out_dir": out_dir})


@default_tool()
def vfx_transcode_export(asset_paths: list[str], out_dir: str = "", include_stubs: bool = False, schema_out_dir: str = "") -> dict[str, Any]:
    """Niagara counterpart of transcode_export."""
    return call_bridge("vfx_transcode_export", dict(asset_paths=asset_paths, out_dir=out_dir, include_stubs=include_stubs, schema_out_dir=schema_out_dir))


@default_tool()
def vfx_transcode_apply(
    asset_path: str,
    kind: str,
    plan: list[dict[str, Any]],
    ids: dict[str, str] | None = None,
    create: bool = False,
    asset_class: str = "",
    out_dir: str = "",
    dry_run: bool = True,
    compile: bool = True,
    save: bool = True,
    apply_id: str | None = None, repository: str = "", collaboration_version: int = 1,
    expected_revision: str | None = None, expected_absent: bool = False,
    read_set: list[dict[str, Any]] | None = None, delete_asset: bool = False,
) -> dict[str, Any]:
    """Niagara counterpart of transcode_apply."""
    return call_bridge("vfx_transcode_apply", {
        "asset_path": asset_path, "kind": kind, "plan": plan, "ids": ids or {}, "create": create,
        "asset_class": asset_class, "out_dir": out_dir, "dry_run": dry_run, "compile": compile, "save": save,
        **_protocol(apply_id, repository, collaboration_version, expected_revision, expected_absent, read_set),
        "delete_asset": delete_asset,
    })


@default_tool()
def transcode_recover(apply_id: str, repository: str, restore: bool = False, preserve_current: bool = False) -> dict[str, Any]:
    """Query the durable receipt or restore an interrupted apply's verified checkpoint."""
    return call_bridge("transcode_recover", dict(apply_id=apply_id, repository=repository, restore=restore,
                                                  preserve_current=preserve_current))


def _protocol(apply_id, repository, version, revision, absent, read_set) -> dict:
    return dict(apply_id=apply_id, repository=repository, collaboration_version=version, expected_revision=revision,
                expected_absent=absent, read_set=read_set or []) if apply_id else dict()
