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


def require_text_contains(response: dict[str, Any], fragments: list[str]) -> None:
    text = str(response.get("data", {}).get("text", ""))
    missing = [fragment for fragment in fragments if fragment not in text]
    if missing:
        raise RuntimeError(f"readback missing {missing}: {text}")


def run_material_function_graph_matrix(
    client: BridgeLike,
    project_file: Path,
    package_root: str,
    keep_assets: bool,
    collect_disk_residue: Any,
) -> None:
    run_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    run_root = f"{package_root.rstrip('/')}/MaterialFunctionGraph_{run_id}"
    function_package = f"{run_root}/MF_GraphBuildFunction"
    material_package = f"{run_root}/M_UsesGraphBuildFunction"
    function_asset = object_path(function_package)
    material_asset = object_path(material_package)

    client.call("folder_create", {"folder_path": run_root, "dry_run": False})
    client.call("asset_create", {"asset_kind": "material_function", "asset_path": function_package, "dry_run": False, "save": True})
    client.call("asset_create", {"asset_kind": "material", "asset_path": material_package, "dry_run": False, "save": True})

    try:
        function_build = require_ok(
            client.call(
                "graph_build_apply",
                {
                    "asset_path": function_asset,
                    "graph_kind": "material_function",
                    "graph_name": None,
                    "dry_run": False,
                    "compile_after": True,
                    "nodes": [
                        {
                            "id": "normal",
                            "node_class": "FunctionInput",
                            "position": {"x": -760, "y": -160},
                            "params": {
                                "InputName": "NormalWS",
                                "InputType": "FunctionInput_Vector3",
                                "PreviewValue": "(X=0.000000,Y=0.000000,Z=1.000000,W=0.000000)",
                                "bUsePreviewValueAsDefault": True,
                            },
                        },
                        {
                            "id": "posz",
                            "node_class": "FunctionInput",
                            "position": {"x": -760, "y": 80},
                            "params": {
                                "InputName": "PosZColor",
                                "InputType": "FunctionInput_Vector3",
                                "PreviewValue": "(X=0.340000,Y=0.360000,Z=0.330000,W=1.000000)",
                                "bUsePreviewValueAsDefault": True,
                            },
                        },
                        {
                            "id": "custom",
                            "node_class": "Custom",
                            "position": {"x": -260, "y": -20},
                            "params": {
                                "Description": "MCP_MaterialFunctionGraph",
                                "Code": "float3 n = normalize(NormalWS); return PosZColor * saturate(n.z * 0.5 + 0.5);",
                                "OutputType": "CMOT_Float3",
                                "Inputs": [{"name": "NormalWS"}, {"name": "PosZColor"}],
                            },
                        },
                        {
                            "id": "out",
                            "node_class": "FunctionOutput",
                            "position": {"x": 220, "y": -20},
                            "params": {"OutputName": "Color"},
                        },
                    ],
                    "links": [
                        {"from": {"node": "normal", "pin": "0"}, "to": {"node": "custom", "pin": "NormalWS"}},
                        {"from": {"node": "posz", "pin": "0"}, "to": {"node": "custom", "pin": "PosZColor"}},
                        {"from": {"node": "custom", "pin": "0"}, "to": {"node": "out", "pin": "A"}},
                    ],
                },
            ),
            "material function graph build",
        )
        if function_build.get("data", {}).get("post_checks", {}).get("pin_integrity", {}).get("ok") is not True:
            raise RuntimeError(f"material function pin integrity failed: {function_build}")

        function_readback = require_ok(
            client.call(
                "graph_node_info_get_w_pos",
                {
                    "asset_path": function_asset,
                    "graph_kind": "material_function",
                    "graph_name": None,
                    "format": "text",
                    "id_mode": "alias",
                    "section": "all",
                    "max_nodes": 20,
                },
            ),
            "material function graph readback",
        )
        require_text_contains(function_readback, ["Graph.Kind = material_function", "NormalWS", "PosZColor", "Color", "MCP_MaterialFunctionGraph"])

        material_build = require_ok(
            client.call(
                "graph_build_apply",
                {
                    "asset_path": material_asset,
                    "graph_kind": "material",
                    "graph_name": None,
                    "dry_run": False,
                    "compile_after": True,
                    "nodes": [
                        {
                            "id": "fn",
                            "node_class": "MaterialFunctionCall",
                            "position": {"x": -240, "y": -80},
                            "params": {"MaterialFunction": function_asset},
                        },
                        {"id": "normal", "node_class": "PixelNormalWS", "position": {"x": -620, "y": -160}, "params": {}},
                        {
                            "id": "posz",
                            "node_class": "VectorParameter",
                            "position": {"x": -620, "y": 40},
                            "params": {
                                "ParameterName": "PosZColor",
                                "DefaultValue": "(R=0.340000,G=0.360000,B=0.330000,A=1.000000)",
                            },
                        },
                    ],
                    "links": [
                        {"from": {"node": "normal", "pin": "0"}, "to": {"node": "fn", "pin": "NormalWS"}},
                        {"from": {"node": "posz", "pin": "0"}, "to": {"node": "fn", "pin": "PosZColor"}},
                    ],
                    "material_outputs": [{"property": "BaseColor", "from": {"node": "fn", "pin": "Color"}}],
                },
            ),
            "material function call graph build",
        )
        if material_build.get("data", {}).get("post_checks", {}).get("compile", {}).get("ok") is not True:
            raise RuntimeError(f"material function call compile failed: {material_build}")

        material_readback = require_ok(
            client.call(
                "graph_node_info_get_w_pos",
                {
                    "asset_path": material_asset,
                    "graph_kind": "material",
                    "graph_name": None,
                    "format": "text",
                    "id_mode": "alias",
                    "section": "all",
                    "max_nodes": 20,
                },
            ),
            "material function call readback",
        )
        require_text_contains(material_readback, ["MaterialFunctionCall", "NormalWS", "PosZColor", "MaterialOutput.inpin_01.BaseColor"])

        client.call("asset_save", {"asset_path": function_asset, "only_if_dirty": True, "fail_if_open_editor_conflict": True})
        client.call("asset_save", {"asset_path": material_asset, "only_if_dirty": True, "fail_if_open_editor_conflict": True})
        client.call("editor_save_all", {"save_map_packages": True, "save_content_packages": True})
    finally:
        if not keep_assets:
            for asset_path in [material_asset, function_asset]:
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
