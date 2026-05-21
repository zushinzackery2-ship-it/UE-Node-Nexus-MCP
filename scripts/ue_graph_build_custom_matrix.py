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


def require_ok(response: dict[str, Any], context: str) -> dict[str, Any]:
    if response.get("ok") is not True:
        raise RuntimeError(f"{context} expected ok=true: {response}")
    return response


def assert_readback_contains(response: dict[str, Any], fragments: list[str]) -> None:
    text = str(response.get("data", {}).get("text", ""))
    missing = [fragment for fragment in fragments if fragment not in text]
    if missing:
        raise RuntimeError(f"readback missing {missing}: {text}")


def run_graph_build_custom_matrix(
    client: BridgeLike,
    project_file: Path,
    package_root: str,
    keep_assets: bool,
    collect_disk_residue: Any,
) -> None:
    run_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    run_root = f"{package_root.rstrip('/')}/GraphBuildCustom_{run_id}"
    package_path = f"{run_root}/M_GraphBuildCustom"
    asset_path = object_path(package_path)

    client.call("folder_create", {"folder_path": run_root, "dry_run": False})
    client.call("asset_create", {"asset_kind": "material", "asset_path": package_path, "dry_run": False, "save": True})

    try:
        build = require_ok(
            client.call(
                "graph_build_apply",
                {
                    "asset_path": asset_path,
                    "graph_kind": "material",
                    "graph_name": None,
                    "dry_run": False,
                    "compile_after": True,
                    "format": "full",
                    "nodes": [
                        {
                            "id": "color",
                            "node_class": "VectorParameter",
                            "position": {"x": -620, "y": -120},
                            "params": {
                                "ParameterName": "CustomColor",
                                "DefaultValue": "(R=0.180000,G=0.520000,B=0.930000,A=1.000000)",
                            },
                        },
                        {
                            "id": "scalar",
                            "node_class": "ScalarParameter",
                            "position": {"x": -620, "y": 140},
                            "params": {
                                "ParameterName": "RoughnessInput",
                                "DefaultValue": 0.45,
                            },
                        },
                        {
                            "id": "custom",
                            "node_class": "Custom",
                            "position": {"x": -180, "y": 0},
                            "params": {
                                "Description": "MCP_CustomGraphBuild",
                                "Code": "return float3(InColor.r, InScalar, InColor.b);",
                                "OutputType": "CMOT_Float3",
                                "Inputs": [
                                    {"name": "InColor"},
                                    {"name": "InScalar"},
                                ],
                            },
                        },
                    ],
                    "links": [
                        {
                            "from": {"node": "color", "pin": "0"},
                            "to": {"node": "custom", "pin": "InColor"},
                        },
                        {
                            "from": {"node": "scalar", "pin": "0"},
                            "to": {"node": "custom", "pin": "InScalar"},
                        },
                    ],
                    "material_outputs": [
                        {
                            "property": "BaseColor",
                            "from": {"node": "custom", "pin": "0"},
                        },
                        {
                            "property": "Roughness",
                            "from": {"node": "scalar", "pin": "0"},
                        },
                    ],
                },
            ),
            "graph_build_apply custom material",
        )
        if build.get("data", {}).get("post_checks", {}).get("compile", {}).get("ok") is not True:
            raise RuntimeError(f"graph_build_apply compile failed: {build}")

        changed = build.get("data", {}).get("diff", {})
        if len(changed.get("nodes_created", [])) != 3:
            raise RuntimeError(f"expected 3 created nodes: {build}")
        if len(changed.get("links_added", [])) != 4:
            raise RuntimeError(f"expected 4 added links: {build}")

        readback = require_ok(
            client.call(
                "graph_node_info_get_w_pos",
                {
                    "asset_path": asset_path,
                    "graph_kind": "material",
                    "graph_name": None,
                    "format": "text",
                    "id_mode": "alias",
                    "section": "all",
                    "max_nodes": 20,
                },
            ),
            "graph build readback",
        )
        assert_readback_contains(
            readback,
            [
                "Custom_00",
                "CustomColor",
                "RoughnessInput",
                "InColor",
                "InScalar",
                "MaterialOutput.inpin_01.BaseColor",
                "MaterialOutput.inpin_04.Roughness",
            ],
        )

        client.call("asset_save", {"asset_path": asset_path, "only_if_dirty": True, "fail_if_open_editor_conflict": True})
        client.call("editor_save_all", {"save_map_packages": True, "save_content_packages": True})
    finally:
        if not keep_assets:
            client.call(
                "asset_delete",
                {
                    "asset_path": asset_path,
                    "dry_run": False,
                    "allow_referenced": True,
                    "cleanup_after_delete": True,
                },
            )
            client.call("asset_redirectors_fixup", {"folder_path": package_root, "dry_run": False})
            client.call("folder_delete", {"folder_path": run_root, "dry_run": False, "recursive": True})
            client.call("folder_delete", {"folder_path": package_root, "dry_run": False, "recursive": True})
            client.call("editor_save_all", {"save_map_packages": True, "save_content_packages": True})
            remaining = collect_disk_residue(project_file, package_root)
            if remaining:
                raise RuntimeError(f"disk residue remained under {package_root}: {remaining}")
