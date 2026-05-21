from __future__ import annotations

from datetime import datetime
from pathlib import Path
from typing import Any, Protocol


class BridgeLike(Protocol):
    def call(self, operation: str, payload: dict[str, Any]) -> dict[str, Any]:
        ...


def object_path(package_path: str) -> str:
    name = package_path.rsplit("/", 1)[-1]
    return f"{package_path}.{name}"


def _cleanup_assets(client: BridgeLike, package_root: str) -> None:
    assets = client.call(
        "asset_list",
        {
            "class_names": [],
            "package_paths": [package_root],
            "recursive": True,
            "limit": 200,
            "cursor": None,
            "format": "full",
        },
    )
    for item in assets.get("data", {}).get("items", []):
        if isinstance(item, dict) and isinstance(item.get("object_path"), str):
            client.call(
                "asset_delete",
                {
                    "asset_path": item["object_path"],
                    "dry_run": False,
                    "allow_referenced": True,
                    "cleanup_after_delete": True,
                },
            )
    client.call("folder_delete", {"folder_path": package_root, "dry_run": False, "recursive": True})


def _first_mesh_component(client: BridgeLike) -> str:
    response = client.call(
        "level_mesh_instances_list",
        {
            "include_materials": True,
            "class_names": [],
            "limit": 20,
            "cursor": None,
            "format": "full",
        },
    )
    for item in response.get("data", {}).get("items", []):
        if isinstance(item, dict) and int(item.get("material_slot_count", 0)) > 0:
            component_path = item.get("component_path")
            if isinstance(component_path, str) and component_path:
                return component_path
    raise RuntimeError(f"no mesh component with material slots found: {response}")


def _transform_location(response: dict[str, Any]) -> dict[str, float]:
    location = response["data"]["transform"]["location"]
    return {"x": float(location["x"]), "y": float(location["y"]), "z": float(location["z"])}


def _assert_near_location(response: dict[str, Any], expected: dict[str, float], label: str) -> None:
    actual = _transform_location(response)
    for axis, value in expected.items():
        if abs(actual[axis] - value) > 0.01:
            raise RuntimeError(f"{label} transform {axis} mismatch: expected {value}, got {actual[axis]}")


def _build_material(client: BridgeLike, material_path: str) -> None:
    client.call("asset_create", {"asset_kind": "material", "asset_path": material_path, "dry_run": False, "save": False})
    client.call(
        "graph_build_apply",
        {
            "asset_path": object_path(material_path),
            "graph_kind": "material",
            "dry_run": False,
            "compile_after": True,
            "format": "compact",
            "nodes": [
                {
                    "id": "base_color",
                    "node_class": "VectorParameter",
                    "position": {"x": -300, "y": 0},
                    "params": {
                        "ParameterName": "BaseColor",
                        "DefaultValue": {"r": 0.1, "g": 0.6, "b": 0.9, "a": 1.0},
                    },
                },
                {
                    "id": "roughness",
                    "node_class": "ScalarParameter",
                    "position": {"x": -300, "y": 180},
                    "params": {"ParameterName": "Roughness", "DefaultValue": 0.35},
                },
            ],
            "links": [],
            "material_outputs": [
                {"from": "base_color.RGB", "to": "MaterialOutput.BaseColor"},
                {"from": "roughness.Value", "to": "MaterialOutput.Roughness"},
            ],
        },
    )
    client.call("asset_save", {"asset_path": object_path(material_path), "only_if_dirty": True, "fail_if_open_editor_conflict": True})


def _create_instance(client: BridgeLike, instance_path: str, parent_material: str) -> None:
    client.call(
        "asset_create",
        {
            "asset_kind": "material_instance",
            "asset_path": instance_path,
            "parent_asset_path": object_path(parent_material),
            "dry_run": False,
            "save": False,
        },
    )
    client.call(
        "material_instance_params_set",
        {
            "asset_path": object_path(instance_path),
            "dry_run": False,
            "compile_after": True,
            "params": [
                {"type": "scalar", "name": "Roughness", "value": 0.62},
                {"type": "vector", "name": "BaseColor", "value": {"r": 0.8, "g": 0.2, "b": 0.1, "a": 1.0}},
            ],
        },
    )
    client.call("asset_save", {"asset_path": object_path(instance_path), "only_if_dirty": True, "fail_if_open_editor_conflict": True})


def run_level_material_matrix(
    client: BridgeLike,
    project_file: Path,
    package_root: str,
    keep_assets: bool,
    collect_disk_residue: Any,
) -> None:
    run_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    run_root = f"{package_root.rstrip('/')}/LevelMat_{run_id}"
    material_path = f"{run_root}/M_LevelMaterialMatrix"
    instance_path = f"{run_root}/MI_LevelMaterialMatrix"

    _cleanup_assets(client, package_root)
    client.call("folder_create", {"folder_path": run_root, "dry_run": False})
    component_path = _first_mesh_component(client)
    original_slots = client.call("component_materials_get", {"component_path": component_path})
    original_material = original_slots["data"]["materials"][0]["material_path"]
    original_actor_transform: dict[str, Any] | None = None

    try:
        actor_path = original_slots["data"]["actor_path"]
        client.call("level_actor_get", {"actor_path": actor_path, "include_components": True})
        original_actor_transform = client.call("level_actor_transform_get", {"actor_path": actor_path})
        original_location = _transform_location(original_actor_transform)
        moved_location = {
            "x": original_location["x"] + 1.0,
            "y": original_location["y"],
            "z": original_location["z"],
        }
        transform_mark_dirty = not actor_path.startswith("/Temp/")
        client.call(
            "level_actor_transform_set",
            {
                "actor_path": actor_path,
                "location": moved_location,
                "dry_run": True,
                "mark_dirty": transform_mark_dirty,
            },
        )
        _assert_near_location(client.call("level_actor_transform_get", {"actor_path": actor_path}), original_location, "dry-run")
        client.call(
            "level_actor_transform_set",
            {
                "actor_path": actor_path,
                "location": moved_location,
                "dry_run": False,
                "mark_dirty": transform_mark_dirty,
            },
        )
        _assert_near_location(client.call("level_actor_transform_get", {"actor_path": actor_path}), moved_location, "applied")

        client.call("object_properties_get", {"object_path": component_path, "property_names": ["Mobility", "RelativeLocation"], "include_non_editable": False, "format": "compact"})
        property_plan = client.call(
            "object_properties_set",
            {
                "object_path": component_path,
                "params": [{"name": "RelativeLocation", "value": {"x": 0.0, "y": 0.0, "z": 0.0}}],
                "dry_run": True,
                "allow_non_editable": False,
            },
        )
        property_items = property_plan.get("data", {}).get("items", [])
        if not property_items or not property_items[0].get("value_text"):
            raise RuntimeError(f"structured property dry-run did not produce import text: {property_plan}")

        _build_material(client, material_path)
        _create_instance(client, instance_path, material_path)
        client.call(
            "component_materials_set",
            {
                "component_path": component_path,
                "slot_index": 0,
                "material_path": object_path(instance_path),
                "dry_run": False,
            },
        )

        resolved = client.call(
            "material_interface_resolve",
            {"component_path": component_path, "slot_index": 0, "include_params": True},
        )
        if resolved["data"].get("root_material_path") != object_path(material_path):
            raise RuntimeError(f"root material mismatch: {resolved}")

        usage = client.call(
            "material_usage_find",
            {"asset_path": object_path(material_path), "limit": 20, "cursor": None, "format": "full"},
        )
        if int(usage.get("data", {}).get("count", 0)) < 1:
            raise RuntimeError(f"material usage did not find assigned component: {usage}")

        params = client.call("component_material_instance_params_get", {"component_path": component_path, "slot_index": 0})
        if int(params.get("data", {}).get("param_count", 0)) < 2:
            raise RuntimeError(f"component material params missing expected values: {params}")

        client.call(
            "component_material_instance_params_set",
            {
                "component_path": component_path,
                "slot_index": 0,
                "dry_run": False,
                "create_dynamic": True,
                "params": [{"type": "scalar", "name": "Roughness", "value": 0.21}],
            },
        )
        client.call("component_material_instance_params_get", {"component_path": component_path, "slot_index": 0})
    finally:
        if original_actor_transform is not None:
            actor_path = original_slots["data"]["actor_path"]
            original = original_actor_transform["data"]["transform"]
            client.call(
                "level_actor_transform_set",
                {
                    "actor_path": actor_path,
                    "location": original["location"],
                    "rotation": original["rotation"],
                    "scale": original["scale"],
                    "dry_run": False,
                    "mark_dirty": not actor_path.startswith("/Temp/"),
                },
            )
        if original_material:
            client.call(
                "component_materials_set",
                {
                    "component_path": component_path,
                    "slot_index": 0,
                    "material_path": original_material,
                    "dry_run": False,
                },
            )
        client.call("editor_save_all", {"save_map_packages": True, "save_content_packages": True})
        if not keep_assets:
            _cleanup_assets(client, package_root)
            remaining = collect_disk_residue(project_file, package_root)
            if remaining:
                raise RuntimeError(f"disk residue remained under {package_root}: {remaining}")
