from __future__ import annotations

from typing import Any, Literal

from .contracts import require_list, require_non_empty_string
from .runtime import call_bridge as _call
from .runtime import default_tool


@default_tool("niagara")
def niagara_system_create(
    asset_path: str,
    template_asset_path: str | None = None,
    create_default_nodes: bool = True,
    dry_run: bool = True,
    save: bool = False,
) -> dict[str, Any]:
    """Create a Niagara system, optionally by copying an existing system template."""
    require_non_empty_string(asset_path, "asset_path")
    return _call(
        "niagara_system_create",
        {
            "asset_path": asset_path,
            "template_asset_path": template_asset_path,
            "create_default_nodes": create_default_nodes,
            "dry_run": dry_run,
            "save": save,
        },
    )


@default_tool("niagara")
def niagara_template_duplicate(
    source_asset_path: str,
    destination_asset_path: str,
    dry_run: bool = True,
    save: bool = False,
) -> dict[str, Any]:
    """Duplicate a Niagara system template to a destination asset path."""
    require_non_empty_string(source_asset_path, "source_asset_path")
    require_non_empty_string(destination_asset_path, "destination_asset_path")
    return _call(
        "niagara_template_duplicate",
        {
            "source_asset_path": source_asset_path,
            "destination_asset_path": destination_asset_path,
            "dry_run": dry_run,
            "save": save,
        },
    )


@default_tool("niagara")
def niagara_system_summary_get(asset_path: str, format: Literal["compact", "full"] = "compact") -> dict[str, Any]:
    """Return a compact Niagara system summary with emitter, renderer, and user-parameter counts."""
    require_non_empty_string(asset_path, "asset_path")
    return _call("niagara_system_summary_get", {"asset_path": asset_path, "format": format})


@default_tool("niagara")
def niagara_emitters_list(asset_path: str, format: Literal["compact", "full"] = "compact") -> dict[str, Any]:
    """List emitters in a Niagara system using a low-context row format by default."""
    require_non_empty_string(asset_path, "asset_path")
    return _call("niagara_emitters_list", {"asset_path": asset_path, "format": format})


@default_tool("niagara")
def niagara_user_params_get(asset_path: str, format: Literal["compact", "full"] = "compact") -> dict[str, Any]:
    """List exposed Niagara User parameters and their current values where supported."""
    require_non_empty_string(asset_path, "asset_path")
    return _call("niagara_user_params_get", {"asset_path": asset_path, "format": format})


@default_tool("niagara")
def niagara_user_params_set(
    asset_path: str,
    params: list[dict[str, Any]],
    dry_run: bool = True,
    save: bool = False,
) -> dict[str, Any]:
    """Set existing or addable simple Niagara User parameters with typed validation."""
    require_non_empty_string(asset_path, "asset_path")
    require_list(params, "params")
    return _call("niagara_user_params_set", {"asset_path": asset_path, "params": params, "dry_run": dry_run, "save": save})


@default_tool("niagara")
def niagara_materials_get(asset_path: str, format: Literal["compact", "full"] = "compact") -> dict[str, Any]:
    """List materials assigned to Niagara sprite, ribbon, and mesh renderers."""
    require_non_empty_string(asset_path, "asset_path")
    return _call("niagara_materials_get", {"asset_path": asset_path, "format": format})


@default_tool("niagara")
def niagara_materials_set(
    asset_path: str,
    material_path: str,
    emitter_index: int | None = None,
    renderer_index: int | None = None,
    dry_run: bool = True,
    save: bool = False,
) -> dict[str, Any]:
    """Assign a material to matching Niagara sprite, ribbon, or mesh renderers."""
    require_non_empty_string(asset_path, "asset_path")
    require_non_empty_string(material_path, "material_path")
    return _call(
        "niagara_materials_set",
        {
            "asset_path": asset_path,
            "material_path": material_path,
            "emitter_index": emitter_index,
            "renderer_index": renderer_index,
            "dry_run": dry_run,
            "save": save,
        },
    )


@default_tool("niagara")
def niagara_compile(asset_path: str, wait: bool = True, save: bool = False) -> dict[str, Any]:
    """Request a Niagara system compile and return readiness diagnostics."""
    require_non_empty_string(asset_path, "asset_path")
    return _call("niagara_compile", {"asset_path": asset_path, "wait": wait, "save": save})
