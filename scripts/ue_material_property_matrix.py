from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any, Protocol


class BridgeLike(Protocol):
    def call(self, operation: str, payload: dict[str, Any]) -> dict[str, Any]:
        ...


@dataclass(frozen=True)
class MaterialNodeCase:
    node_class: str
    create_params: dict[str, Any]
    writes: dict[str, Any]
    required_kinds: set[str]


NODE_CASES = [
    MaterialNodeCase(
        node_class="Constant",
        create_params={"R": 0.25},
        writes={"R": 0.75},
        required_kinds={"number"},
    ),
    MaterialNodeCase(
        node_class="ComponentMask",
        create_params={"R": True, "G": False, "B": False, "A": False},
        writes={"R": False, "G": True},
        required_kinds={"bool"},
    ),
    MaterialNodeCase(
        node_class="TextureSample",
        create_params={"Texture": "/Engine/EngineResources/DefaultTexture.DefaultTexture"},
        writes={"Texture": "/Engine/EngineResources/DefaultTexture.DefaultTexture"},
        required_kinds={"object", "enum"},
    ),
]


def object_path(package_path: str) -> str:
    name = package_path.rsplit("/", 1)[-1]
    return f"{package_path}.{name}"


def params_by_name(response: dict[str, Any]) -> dict[str, dict[str, Any]]:
    params = response.get("data", {}).get("params", [])
    return {
        item["name"]: item
        for item in params
        if isinstance(item, dict) and isinstance(item.get("name"), str)
    }


def values_match(actual: str, expected: Any) -> bool:
    if isinstance(expected, bool):
        return actual.lower() == ("true" if expected else "false")
    if isinstance(expected, (int, float)):
        try:
            return abs(float(actual) - float(expected)) < 0.0001
        except ValueError:
            return False
    return str(expected) in actual


def assert_schema_contains(params: dict[str, dict[str, Any]], required_kinds: set[str], context: str) -> None:
    actual_kinds = {
        str(param.get("kind"))
        for param in params.values()
        if param.get("editable") is True
    }
    missing = required_kinds - actual_kinds
    if missing:
        raise RuntimeError(f"{context} schema missing kinds {sorted(missing)}; actual={sorted(actual_kinds)}")
    for param in params.values():
        if "kind" not in param or "type" not in param or "default_value" not in param:
            raise RuntimeError(f"{context} incomplete schema for {param.get('name')}: {param}")
        if param.get("kind") == "enum" and not param.get("enum_values"):
            raise RuntimeError(f"{context} enum schema missing values for {param.get('name')}")
        if param.get("kind") == "object" and "object_class" not in param:
            raise RuntimeError(f"{context} object schema missing class for {param.get('name')}")


def assert_writes_read_back(params: dict[str, dict[str, Any]], writes: dict[str, Any], context: str) -> None:
    for name, expected in writes.items():
        if name not in params:
            raise RuntimeError(f"{context} missing written param {name}")
        actual = str(params[name].get("value", ""))
        if not values_match(actual, expected):
            raise RuntimeError(f"{context} param {name} readback mismatch: {actual!r} != {expected!r}")


def run_material_property_matrix(
    client: BridgeLike,
    project_file: Path,
    package_root: str,
    keep_assets: bool,
    collect_disk_residue: Any,
) -> None:
    run_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    material_package = f"{package_root.rstrip('/')}/MaterialProps_{run_id}/M_PropertyMatrix"
    material_path = object_path(material_package)
    client.call("folder_create", {"folder_path": material_package.rsplit("/", 1)[0], "dry_run": False})
    client.call("asset_create", {"asset_kind": "material", "asset_path": material_package, "dry_run": False, "save": False})

    created_node_ids: list[str] = []
    try:
        for index, case in enumerate(NODE_CASES):
            class_schema = params_by_name(client.call("node_class_params_get", {"graph_kind": "material", "node_class": case.node_class}))
            assert_schema_contains(class_schema, case.required_kinds, f"{case.node_class} class")

            create_response = client.call(
                "node_create",
                {
                    "asset_path": material_path,
                    "graph_kind": "material",
                    "graph_name": None,
                    "node_class": case.node_class,
                    "position": {"x": index * 260, "y": index * 80},
                    "params": case.create_params,
                    "dry_run": False,
                },
            )
            node_id = create_response.get("data", {}).get("node_id")
            if not isinstance(node_id, str) or not node_id:
                raise RuntimeError(f"{case.node_class} create did not return node_id")
            created_node_ids.append(node_id)

            initial_params = params_by_name(client.call("node_params_get", {"asset_path": material_path, "graph_kind": "material", "node_id": node_id}))
            assert_schema_contains(initial_params, case.required_kinds, f"{case.node_class} instance")

            client.call(
                "node_params_set",
                {
                    "asset_path": material_path,
                    "graph_kind": "material",
                    "node_id": node_id,
                    "params": case.writes,
                    "dry_run": False,
                    "compile_after": True,
                },
            )
            final_params = params_by_name(client.call("node_params_get", {"asset_path": material_path, "graph_kind": "material", "node_id": node_id}))
            assert_writes_read_back(final_params, case.writes, case.node_class)

        client.call("asset_compile", {"asset_path": material_path})
        client.call("asset_save", {"asset_path": material_path, "only_if_dirty": True, "fail_if_open_editor_conflict": True})
        client.call("editor_save_all", {"save_map_packages": True, "save_content_packages": True})
    finally:
        if not keep_assets:
            client.call("asset_delete", {"asset_path": material_path, "dry_run": False, "allow_referenced": False})
            client.call("asset_redirectors_fixup", {"folder_path": package_root, "dry_run": False})
            client.call("editor_save_all", {"save_map_packages": True, "save_content_packages": True})
            remaining = collect_disk_residue(project_file, package_root)
            if remaining:
                raise RuntimeError(f"disk residue remained under {package_root}: {remaining}")
