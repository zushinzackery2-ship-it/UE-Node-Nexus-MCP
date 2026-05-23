from __future__ import annotations

from typing import Any, Literal

from .contracts import require_list, require_non_empty_string
from .runtime import default_tool
from .tools_niagara_common import _call_niagara


@default_tool("niagara")
def niagara_system_properties_get(
    asset_path: str,
    property_names: list[str] | None = None,
    include_non_editable: bool = False,
    format: Literal["compact", "full", "indexed", "tiny"] = "indexed",
    include_types: bool = False,
) -> dict[str, Any]:
    """Read editable Niagara system UObject properties; indexed omits values and types by default."""
    require_non_empty_string(asset_path, "asset_path")
    return _call_niagara(
        "niagara_system_properties_get",
        {
            "asset_path": asset_path,
            "property_names": property_names,
            "include_non_editable": include_non_editable,
            "format": format,
            "include_types": include_types,
        },
    )


@default_tool("niagara")
def niagara_system_properties_set(
    asset_path: str,
    params: list[dict[str, Any]],
    dry_run: bool = True,
    allow_non_editable: bool = False,
    save: bool = False,
    format: Literal["summary", "full"] = "summary",
) -> dict[str, Any]:
    """Patch editable Niagara system UObject properties; summary keeps default responses compact."""
    require_non_empty_string(asset_path, "asset_path")
    require_list(params, "params")
    return _call_niagara(
        "niagara_system_properties_set",
        {
            "asset_path": asset_path,
            "params": params,
            "dry_run": dry_run,
            "allow_non_editable": allow_non_editable,
            "save": save,
            "format": format,
        },
    )


@default_tool("niagara")
def niagara_renderer_properties_get(
    asset_path: str,
    emitter_index: int,
    renderer_index: int,
    property_names: list[str] | None = None,
    include_non_editable: bool = False,
    format: Literal["compact", "full", "indexed", "tiny"] = "indexed",
    include_types: bool = False,
) -> dict[str, Any]:
    """Read editable renderer UObject properties; indexed omits values and types by default."""
    require_non_empty_string(asset_path, "asset_path")
    return _call_niagara(
        "niagara_renderer_properties_get",
        {
            "asset_path": asset_path,
            "emitter_index": emitter_index,
            "renderer_index": renderer_index,
            "property_names": property_names,
            "include_non_editable": include_non_editable,
            "format": format,
            "include_types": include_types,
        },
    )


@default_tool("niagara")
def niagara_renderer_properties_set(
    asset_path: str,
    emitter_index: int,
    renderer_index: int,
    params: list[dict[str, Any]],
    dry_run: bool = True,
    allow_non_editable: bool = False,
    save: bool = False,
    format: Literal["summary", "full"] = "summary",
) -> dict[str, Any]:
    """Patch editable UObject properties for a Niagara renderer; summary omits per-property items."""
    require_non_empty_string(asset_path, "asset_path")
    require_list(params, "params")
    payload = {
        "asset_path": asset_path,
        "emitter_index": emitter_index,
        "renderer_index": renderer_index,
        "params": params,
        "dry_run": dry_run,
        "allow_non_editable": allow_non_editable,
        "save": save,
        "format": format,
    }
    return _call_niagara("niagara_renderer_properties_set", payload)
