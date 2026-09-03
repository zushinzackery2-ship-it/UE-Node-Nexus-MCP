"""Typed wrappers for the transcode (text mirror) bridge operations.

These exist so ``ue_capability_get(operation=..., detail="schema")`` can derive
payload schemas; the ``ue_sync`` facade drives them internally. Direct use is
for diagnosis only.
"""

from __future__ import annotations

from typing import Any

from .runtime import call_bridge, default_tool


@default_tool()
def transcode_root_set(root: str) -> dict[str, Any]:
    """Register the absolute mirror root directory; export/apply refuse paths outside it."""
    return call_bridge("transcode_root_set", {"root": root})


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
) -> dict[str, Any]:
    """Apply plan verbs (see transcode/plan.py) in one transaction; ``ids`` maps local ids to node GUIDs."""
    return call_bridge("transcode_apply", {
        "asset_path": asset_path, "kind": kind, "plan": plan, "ids": ids or {}, "create": create,
        "asset_class": asset_class, "out_dir": out_dir, "dry_run": dry_run, "compile": compile, "save": save,
    })


@default_tool()
def schema_export(out_dir: str) -> dict[str, Any]:
    """Write the reflection schema lock files into ``out_dir``."""
    return call_bridge("schema_export", {"out_dir": out_dir})


@default_tool()
def transcode_watch_set(enabled: bool, out_dir: str = "") -> dict[str, Any]:
    """Toggle automatic raw export of mirrored assets on editor save."""
    return call_bridge("transcode_watch_set", {"enabled": enabled, "out_dir": out_dir})


@default_tool()
def vfx_transcode_export(asset_paths: list[str], out_dir: str, include_stubs: bool = False) -> dict[str, Any]:
    """Niagara counterpart of transcode_export."""
    return call_bridge("vfx_transcode_export", {"asset_paths": asset_paths, "out_dir": out_dir, "include_stubs": include_stubs})


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
) -> dict[str, Any]:
    """Niagara counterpart of transcode_apply."""
    return call_bridge("vfx_transcode_apply", {
        "asset_path": asset_path, "kind": kind, "plan": plan, "ids": ids or {}, "create": create,
        "asset_class": asset_class, "out_dir": out_dir, "dry_run": dry_run, "compile": compile, "save": save,
    })
