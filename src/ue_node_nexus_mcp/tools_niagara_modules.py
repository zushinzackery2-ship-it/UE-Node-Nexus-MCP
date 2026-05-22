from __future__ import annotations

from typing import Any, Literal

from .contracts import require_non_empty_string
from .runtime import call_bridge as _call
from .runtime import default_tool


@default_tool("niagara")
def niagara_modules_list(
    asset_path: str,
    emitter_index: int | None = None,
    usage: str | None = None,
    format: Literal["compact", "full"] = "compact",
) -> dict[str, Any]:
    """List Niagara module stack nodes by emitter and script usage."""
    require_non_empty_string(asset_path, "asset_path")
    return _call(
        "niagara_modules_list",
        {
            "asset_path": asset_path,
            "emitter_index": emitter_index,
            "usage": usage,
            "format": format,
        },
    )


@default_tool("niagara")
def niagara_module_add(
    asset_path: str,
    module_script_path: str,
    emitter_index: int,
    usage: str = "ParticleUpdateScript",
    target_index: int | None = None,
    suggested_name: str | None = None,
    dry_run: bool = True,
    save: bool = False,
) -> dict[str, Any]:
    """Add an existing Niagara module script to an emitter stack."""
    require_non_empty_string(asset_path, "asset_path")
    require_non_empty_string(module_script_path, "module_script_path")
    return _call(
        "niagara_module_add",
        {
            "asset_path": asset_path,
            "module_script_path": module_script_path,
            "emitter_index": emitter_index,
            "usage": usage,
            "target_index": target_index,
            "suggested_name": suggested_name,
            "dry_run": dry_run,
            "save": save,
        },
    )


@default_tool("niagara")
def niagara_module_remove(
    asset_path: str,
    emitter_index: int,
    usage: str,
    module_index: int,
    dry_run: bool = True,
    save: bool = False,
) -> dict[str, Any]:
    """Remove one Niagara module stack node from an emitter stack."""
    require_non_empty_string(asset_path, "asset_path")
    return _call(
        "niagara_module_remove",
        {
            "asset_path": asset_path,
            "emitter_index": emitter_index,
            "usage": usage,
            "module_index": module_index,
            "dry_run": dry_run,
            "save": save,
        },
    )


@default_tool("niagara")
def niagara_module_set_enabled(
    asset_path: str,
    emitter_index: int,
    usage: str,
    module_index: int,
    enabled: bool,
    dry_run: bool = True,
    save: bool = False,
) -> dict[str, Any]:
    """Enable or disable one Niagara module stack node."""
    require_non_empty_string(asset_path, "asset_path")
    return _call(
        "niagara_module_set_enabled",
        {
            "asset_path": asset_path,
            "emitter_index": emitter_index,
            "usage": usage,
            "module_index": module_index,
            "enabled": enabled,
            "dry_run": dry_run,
            "save": save,
        },
    )
