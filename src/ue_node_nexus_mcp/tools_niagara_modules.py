from __future__ import annotations

from typing import Any, Literal

from .contracts import require_list, require_non_empty_string
from .runtime import default_tool
from .tools_niagara_common import _call_niagara


@default_tool("vfx")
def niagara_modules_list(
    asset_path: str,
    emitter_index: int | None = None,
    usage: str | None = None,
    format: Literal["compact", "full", "indexed", "tiny"] = "indexed",
    include_script_paths: bool = False,
) -> dict[str, Any]:
    """List module stack nodes; indexed omits script paths unless requested."""
    require_non_empty_string(asset_path, "asset_path")
    return _call_niagara(
        "niagara_modules_list",
        {
            "asset_path": asset_path,
            "emitter_index": emitter_index,
            "usage": usage,
            "format": format,
            "include_script_paths": include_script_paths,
        },
    )


@default_tool("vfx")
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
    return _call_niagara(
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


@default_tool("vfx")
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
    return _call_niagara(
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


@default_tool("vfx")
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
    return _call_niagara(
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


@default_tool("vfx")
def niagara_module_inputs_get(
    asset_path: str,
    emitter_index: int,
    usage: str,
    module_index: int,
) -> dict[str, Any]:
    """Read editable Niagara module input names, types, defaults, and overrides."""
    require_non_empty_string(asset_path, "asset_path")
    return _call_niagara(
        "niagara_module_inputs_get",
        {
            "asset_path": asset_path,
            "emitter_index": emitter_index,
            "usage": usage,
            "module_index": module_index,
        },
    )


@default_tool("vfx")
def niagara_module_inputs_set(
    asset_path: str,
    emitter_index: int,
    usage: str,
    module_index: int,
    params: list[dict[str, Any]],
    dry_run: bool = True,
    save: bool = False,
) -> dict[str, Any]:
    """Set Niagara module input override default values by input name."""
    require_non_empty_string(asset_path, "asset_path")
    require_list(params, "params")
    return _call_niagara(
        "niagara_module_inputs_set",
        {
            "asset_path": asset_path,
            "emitter_index": emitter_index,
            "usage": usage,
            "module_index": module_index,
            "params": params,
            "dry_run": dry_run,
            "save": save,
        },
    )
