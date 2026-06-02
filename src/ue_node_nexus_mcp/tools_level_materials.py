from __future__ import annotations

from typing import Any, Literal

from .contracts import require_exactly_one, require_list, require_non_empty_string
from .runtime import call_bridge as _call
from .runtime import default_tool


@default_tool()
def level_actor_get(actor_path: str, include_components: bool = False) -> dict[str, Any]:
    """Return one placed actor's identity, transform, and optional component list."""
    require_non_empty_string(actor_path, "actor_path")
    return _call("level_actor_get", {"actor_path": actor_path, "include_components": include_components})


@default_tool()
def level_actor_transform_get(actor_path: str) -> dict[str, Any]:
    """Return one placed actor's world transform."""
    require_non_empty_string(actor_path, "actor_path")
    return _call("level_actor_transform_get", {"actor_path": actor_path})


@default_tool()
def object_properties_get(
    object_path: str,
    property_names: list[str] | None = None,
    include_non_editable: bool = False,
    format: Literal["compact", "full"] = "compact",
) -> dict[str, Any]:
    """Read reflected UObject properties with compact defaults and optional full schema text."""
    require_non_empty_string(object_path, "object_path")
    return _call(
        "object_properties_get",
        {
            "object_path": object_path,
            "property_names": property_names or [],
            "include_non_editable": include_non_editable,
            "format": format,
        },
    )


@default_tool()
def level_mesh_instances_list(
    include_materials: bool = False,
    class_names: list[str] | None = None,
    limit: int = 200,
    cursor: str | None = None,
    format: Literal["indexed", "compact", "full"] = "indexed",
) -> dict[str, Any]:
    """List mesh components in the current editor level, optionally including material slots."""
    return _call(
        "level_mesh_instances_list",
        {
            "include_materials": include_materials,
            "class_names": class_names or [],
            "limit": limit,
            "cursor": cursor,
            "format": format,
        },
    )


@default_tool()
def component_materials_get(component_path: str) -> dict[str, Any]:
    """Return material slots for a mesh component, including MI/root material identity."""
    require_non_empty_string(component_path, "component_path")
    return _call("component_materials_get", {"component_path": component_path})


@default_tool()
def component_materials_set(component_path: str, slot_index: int, material_path: str, dry_run: bool = True) -> dict[str, Any]:
    """Set one mesh component material slot."""
    require_non_empty_string(component_path, "component_path")
    require_non_empty_string(material_path, "material_path")
    return _call(
        "component_materials_set",
        {"component_path": component_path, "slot_index": slot_index, "material_path": material_path, "dry_run": dry_run},
    )


@default_tool()
def material_interface_resolve(
    asset_path: str | None = None,
    material_path: str | None = None,
    component_path: str | None = None,
    slot_index: int | None = None,
    include_params: bool = False,
) -> dict[str, Any]:
    """Resolve a material interface to parent chain, root material, and optional parameters.

    Exactly one target must be supplied: ``asset_path``, ``material_path``, or
    ``component_path`` (with ``slot_index``). The bridge enforces the same hard
    contract (target_conflict / invalid_slot / material_not_found).
    """
    require_exactly_one(
        {
            "asset_path": bool(asset_path),
            "material_path": bool(material_path),
            "component_path": bool(component_path),
        },
        "asset_path, material_path, or component_path",
    )
    if component_path and slot_index is None:
        raise ValueError("slot_index is required with component_path")
    return _call(
        "material_interface_resolve",
        {
            "asset_path": asset_path,
            "material_path": material_path,
            "component_path": component_path,
            "slot_index": slot_index,
            "include_params": include_params,
        },
    )


@default_tool()
def material_usage_find(
    asset_path: str | None = None,
    material_path: str | None = None,
    limit: int = 100,
    cursor: str | None = None,
    format: Literal["indexed", "full"] = "indexed",
) -> dict[str, Any]:
    """Find current-level mesh components using a material, material instance, or root material."""
    return _call(
        "material_usage_find",
        {"asset_path": asset_path, "material_path": material_path, "limit": limit, "cursor": cursor, "format": format},
    )


@default_tool()
def component_material_instance_params_get(component_path: str, slot_index: int) -> dict[str, Any]:
    """Read effective scalar/vector/texture/static parameters for one component material slot."""
    require_non_empty_string(component_path, "component_path")
    return _call("component_material_instance_params_get", {"component_path": component_path, "slot_index": slot_index})


@default_tool()
def component_material_instance_params_set(
    component_path: str,
    slot_index: int,
    params: list[dict[str, Any]],
    dry_run: bool = True,
    create_dynamic: bool = False,
) -> dict[str, Any]:
    """Set parameters on a component slot dynamic material instance, optionally creating one."""
    require_non_empty_string(component_path, "component_path")
    require_list(params, "params")
    return _call(
        "component_material_instance_params_set",
        {
            "component_path": component_path,
            "slot_index": slot_index,
            "params": params,
            "dry_run": dry_run,
            "create_dynamic": create_dynamic,
        },
    )
