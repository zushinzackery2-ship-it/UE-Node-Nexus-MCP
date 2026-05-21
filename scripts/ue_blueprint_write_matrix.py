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


def nodes(response: dict[str, Any]) -> list[dict[str, Any]]:
    items = response.get("data", {}).get("nodes", [])
    if not isinstance(items, list):
        return []
    return [item for item in items if isinstance(item, dict)]


def graph_readback_node_count(response: dict[str, Any]) -> int:
    data = response.get("data", {})
    structured_nodes = nodes(response)
    if structured_nodes:
        return len(structured_nodes)
    returned_nodes = data.get("returned_nodes")
    if isinstance(returned_nodes, int):
        return returned_nodes
    if isinstance(returned_nodes, float):
        return int(returned_nodes)
    return 0


def pins(response: dict[str, Any]) -> list[dict[str, Any]]:
    items = response.get("data", {}).get("pins", [])
    if not isinstance(items, list):
        return []
    return [item for item in items if isinstance(item, dict)]


def require_node_id(response: dict[str, Any], context: str) -> str:
    node_id = response.get("data", {}).get("node_id")
    if not isinstance(node_id, str) or not node_id:
        raise RuntimeError(f"{context} did not return node_id")
    return node_id


def find_pin(response: dict[str, Any], direction: str, names: set[str]) -> str:
    for pin in pins(response):
        pin_direction = str(pin.get("direction", ""))
        pin_name = str(pin.get("name", ""))
        pin_id = pin.get("pin_id")
        if pin_direction.lower() == direction.lower() and pin_name.lower() in {name.lower() for name in names} and isinstance(pin_id, str) and pin_id:
            return pin_id
    raise RuntimeError(f"pin not found direction={direction} names={sorted(names)}")


def assert_compile_ok(response: dict[str, Any], context: str) -> None:
    compile_data = response.get("data", {}).get("post_checks", {}).get("compile", {})
    if compile_data and compile_data.get("ok") is False:
        raise RuntimeError(f"{context} compile failed: {compile_data}")


def run_blueprint_write_matrix(
    client: BridgeLike,
    project_file: Path,
    package_root: str,
    keep_assets: bool,
    collect_disk_residue: Any,
) -> None:
    run_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    folder_path = f"{package_root.rstrip('/')}/BlueprintWrite_{run_id}"
    blueprint_package = f"{folder_path}/BP_WriteMatrix"
    blueprint_path = object_path(blueprint_package)

    client.call("folder_create", {"folder_path": folder_path, "dry_run": False})
    client.call(
        "asset_create",
        {
            "asset_kind": "blueprint",
            "asset_path": blueprint_package,
            "parent_class_path": "/Script/Engine.Actor",
            "dry_run": False,
            "save": False,
        },
    )

    try:
        event_response = client.call(
            "node_create",
            {
                "asset_path": blueprint_path,
                "graph_kind": "blueprint",
                "graph_name": None,
                "node_class": "/Script/BlueprintGraph.K2Node_CustomEvent",
                "position": {"x": 0, "y": 0},
                "params": {},
                "dry_run": False,
            },
        )
        event_node_id = require_node_id(event_response, "CustomEvent")
        event_out = find_pin(event_response, "output", {"then", "execute"})

        branch_response = client.call(
            "node_create",
            {
                "asset_path": blueprint_path,
                "graph_kind": "blueprint",
                "graph_name": None,
                "node_class": "/Script/BlueprintGraph.K2Node_IfThenElse",
                "position": {"x": 320, "y": 0},
                "params": {},
                "dry_run": False,
            },
        )
        branch_node_id = require_node_id(branch_response, "Branch")
        branch_exec_in = find_pin(branch_response, "input", {"execute", "exec"})
        branch_condition = find_pin(branch_response, "input", {"Condition"})

        patch_response = client.call(
            "graph_patch_apply",
            {
                "asset_path": blueprint_path,
                "graph_kind": "blueprint",
                "graph_name": None,
                "dry_run": False,
                "compile_after": True,
                "operations": [
                    {
                        "op": "connect_pins",
                        "from_node_id": event_node_id,
                        "from_pin_id": event_out,
                        "to_node_id": branch_node_id,
                        "to_pin_id": branch_exec_in,
                    },
                    {
                        "op": "set_node_param",
                        "node_id": branch_node_id,
                        "pin_id": branch_condition,
                        "value": True,
                    },
                    {
                        "op": "set_node_position",
                        "node_id": branch_node_id,
                        "position": {"x": 480, "y": 96},
                    },
                ],
            },
        )
        assert_compile_ok(patch_response, "graph_patch_apply")

        client.call(
            "node_position_offset",
            {
                "asset_path": blueprint_path,
                "graph_kind": "blueprint",
                "graph_name": None,
                "node_id": event_node_id,
                "dx": -64,
                "dy": 32,
                "dry_run": False,
            },
        )
        info_response = client.call(
            "node_info_get",
            {
                "asset_path": blueprint_path,
                "graph_kind": "blueprint",
                "graph_name": None,
                "node_id": branch_node_id,
                "section": "all",
                "format": "compact_json",
                "index": None,
            },
        )
        if "True" not in str(info_response):
            raise RuntimeError("Branch condition default did not read back as True")

        snapshot = client.call(
            "graph_node_info_get",
            {
                "asset_path": blueprint_path,
                "graph_kind": "blueprint",
                "graph_name": None,
                "section": "all",
                "max_nodes": 50,
                "format": "indexed",
                "id_mode": "both",
            },
        )
        if graph_readback_node_count(snapshot) < 2:
            raise RuntimeError("Blueprint graph readback did not include created nodes")

        client.call("asset_compile", {"asset_path": blueprint_path})
        client.call("asset_save", {"asset_path": blueprint_path, "only_if_dirty": True, "fail_if_open_editor_conflict": True})
        client.call("editor_save_all", {"save_map_packages": True, "save_content_packages": True})
    finally:
        if not keep_assets:
            client.call("asset_delete", {"asset_path": blueprint_path, "dry_run": False, "allow_referenced": False})
            client.call("asset_redirectors_fixup", {"folder_path": package_root, "dry_run": False})
            client.call("editor_save_all", {"save_map_packages": True, "save_content_packages": True})
            remaining = collect_disk_residue(project_file, package_root)
            if remaining:
                raise RuntimeError(f"disk residue remained under {package_root}: {remaining}")
