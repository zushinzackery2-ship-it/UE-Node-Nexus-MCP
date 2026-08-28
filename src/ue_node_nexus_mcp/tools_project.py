from __future__ import annotations

from typing import Any, Literal

from .contracts import require_list, require_non_empty_string
from .runtime import call_bridge as _call
from .runtime import default_tool


@default_tool()
def project_context_get() -> dict[str, Any]:
    """Return the active Unreal project paths, command line, and /Game mount checks."""
    return _call("project_context_get", {})


@default_tool()
def project_input_mappings_get(
    format: Literal["compact", "full"] = "compact",
) -> dict[str, Any]:
    """Read legacy Project Settings input action and axis mappings."""
    return _call(
        "project_input_mappings_get",
        {
            "format": format,
        },
    )


@default_tool()
def input_action_create(
    asset_path: str,
    value_type: Literal["bool", "axis1d", "axis2d", "axis3d"] = "bool",
    dry_run: bool = True,
    save: bool = False,
) -> dict[str, Any]:
    """Create an Enhanced Input UInputAction asset (e.g. /Game/Input/IA_Jump)."""
    require_non_empty_string(asset_path, "asset_path")
    return _call(
        "input_action_create",
        {
            "asset_path": asset_path,
            "value_type": value_type,
            "dry_run": dry_run,
            "save": save,
        },
    )


@default_tool()
def input_mapping_context_create(
    asset_path: str,
    dry_run: bool = True,
    save: bool = False,
) -> dict[str, Any]:
    """Create an Enhanced Input UInputMappingContext asset."""
    require_non_empty_string(asset_path, "asset_path")
    return _call(
        "input_mapping_context_create",
        {
            "asset_path": asset_path,
            "dry_run": dry_run,
            "save": save,
        },
    )


@default_tool()
def input_mapping_context_entry_add(
    asset_path: str,
    action_path: str,
    key: str,
    dry_run: bool = True,
    save: bool = False,
) -> dict[str, Any]:
    """Map an existing InputAction to a key (e.g. SpaceBar) inside a mapping context."""
    require_non_empty_string(asset_path, "asset_path")
    require_non_empty_string(action_path, "action_path")
    require_non_empty_string(key, "key")
    return _call(
        "input_mapping_context_entry_add",
        {
            "asset_path": asset_path,
            "action_path": action_path,
            "key": key,
            "dry_run": dry_run,
            "save": save,
        },
    )


@default_tool()
def input_mapping_context_get(asset_path: str) -> dict[str, Any]:
    """List the action-to-key mappings stored in an InputMappingContext asset."""
    require_non_empty_string(asset_path, "asset_path")
    return _call(
        "input_mapping_context_get",
        {
            "asset_path": asset_path,
        },
    )


@default_tool()
def project_input_mappings_patch(
    operations: list[dict[str, Any]],
    dry_run: bool = True,
    save_config: bool = True,
    format: Literal["compact", "full"] = "compact",
) -> dict[str, Any]:
    """Add or remove legacy Project Settings input action and axis mappings."""
    require_list(operations, "operations")
    return _call(
        "project_input_mappings_patch",
        {
            "operations": operations,
            "dry_run": dry_run,
            "save_config": save_config,
            "format": format,
        },
    )
