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


def require_param(response: dict[str, Any], name: str, value_fragment: str | None = None) -> None:
    params = response.get("data", {}).get("params", [])
    if not isinstance(params, list):
        raise RuntimeError(f"params payload was not a list: {response}")
    for param in params:
        if not isinstance(param, dict) or param.get("name") != name:
            continue
        if value_fragment is not None and value_fragment not in str(param.get("value", "")):
            raise RuntimeError(f"param {name} did not contain {value_fragment}: {param}")
        return
    raise RuntimeError(f"param {name} not found: {response}")


def run_material_function_node_matrix(
    client: BridgeLike,
    project_file: Path,
    package_root: str,
    keep_assets: bool,
    collect_disk_residue: Any,
) -> None:
    run_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    run_root = f"{package_root.rstrip('/')}/MaterialFunctionNode_{run_id}"
    function_package = f"{run_root}/MF_NodeInterface"
    function_asset = object_path(function_package)

    client.call("folder_create", {"folder_path": run_root, "dry_run": False})
    client.call("asset_create", {"asset_kind": "material_function", "asset_path": function_package, "dry_run": False, "save": True})

    try:
        class_schema = require_ok(
            client.call(
                "node_class_params_get",
                {"graph_kind": "material_function", "node_class": "FunctionInput"},
            ),
            "material function class params",
        )
        require_param(class_schema, "InputName")
        require_param(class_schema, "InputType")

        build = require_ok(
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
                            "position": {"x": -720, "y": -140},
                            "params": {
                                "InputName": "NormalWS",
                                "InputType": "FunctionInput_Vector3",
                                "PreviewValue": "(X=0.000000,Y=0.000000,Z=1.000000,W=0.000000)",
                                "bUsePreviewValueAsDefault": True,
                            },
                        },
                        {
                            "id": "custom",
                            "node_class": "Custom",
                            "position": {"x": -220, "y": -80},
                            "params": {
                                "Description": "MCP_NodeInterface_Custom",
                                "Code": "return normalize(NormalWS) * 0.5 + 0.5;",
                                "OutputType": "CMOT_Float3",
                                "Inputs": [{"name": "NormalWS"}],
                            },
                        },
                        {
                            "id": "out",
                            "node_class": "FunctionOutput",
                            "position": {"x": 260, "y": -80},
                            "params": {"OutputName": "Color"},
                        },
                    ],
                    "links": [
                        {"from": {"node": "normal", "pin": "0"}, "to": {"node": "custom", "pin": "NormalWS"}},
                        {"from": {"node": "custom", "pin": "0"}, "to": {"node": "out", "pin": "A"}},
                    ],
                },
            ),
            "material function graph build",
        )
        if build.get("data", {}).get("post_checks", {}).get("pin_integrity", {}).get("ok") is not True:
            raise RuntimeError(f"pin integrity failed: {build}")

        graph = require_ok(
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
        require_text_contains(graph, ["Graph.Kind = material_function", "NormalWS", "Custom_00", "Color", "MCP_NodeInterface_Custom"])

        custom_info = require_ok(
            client.call(
                "node_info_get",
                {
                    "asset_path": function_asset,
                    "graph_kind": "material_function",
                    "graph_name": None,
                    "node_id": "Custom_00",
                    "section": "all",
                    "format": "text",
                },
            ),
            "material function node info",
        )
        require_text_contains(custom_info, ["Node.Name = Custom_00", "Node.Class = Custom", "NormalWS", "MCP_NodeInterface_Custom"])

        custom_params = require_ok(
            client.call(
                "node_params_get",
                {
                    "asset_path": function_asset,
                    "graph_kind": "material_function",
                    "graph_name": None,
                    "node_id": "Custom_00",
                },
            ),
            "material function node params get",
        )
        require_param(custom_params, "Description", "MCP_NodeInterface_Custom")
        require_param(custom_params, "OutputType", "CMOT_Float3")

        before_pos = require_ok(
            client.call(
                "node_position_get",
                {
                    "asset_path": function_asset,
                    "graph_kind": "material_function",
                    "graph_name": None,
                    "node_id": "Custom_00",
                },
            ),
            "material function position get",
        )
        require_text_contains(before_pos, ["Node.Name = Custom_00", "Node.Pos = -220,-80"])

        dry_move = require_ok(
            client.call(
                "node_position_offset",
                {
                    "asset_path": function_asset,
                    "graph_kind": "material_function",
                    "graph_name": None,
                    "node_id": "Custom_00",
                    "dx": 25,
                    "dy": 35,
                    "dry_run": True,
                },
            ),
            "material function position dry-run",
        )
        require_text_contains(dry_move, ["moved = dry_run", "Node.Pos.Before = -220,-80", "Node.Pos.After = -195,-45"])

        move = require_ok(
            client.call(
                "node_position_offset",
                {
                    "asset_path": function_asset,
                    "graph_kind": "material_function",
                    "graph_name": None,
                    "node_id": "Custom_00",
                    "dx": 25,
                    "dy": 35,
                    "dry_run": False,
                },
            ),
            "material function position offset",
        )
        require_text_contains(move, ["moved = true", "Node.Pos.After = -195,-45"])

        output_set = require_ok(
            client.call(
                "node_params_set",
                {
                    "asset_path": function_asset,
                    "graph_kind": "material_function",
                    "graph_name": None,
                    "node_id": "Color",
                    "params": {"OutputName": "NodeColor"},
                    "dry_run": False,
                    "compile_after": True,
                },
            ),
            "material function node params set",
        )
        if output_set.get("data", {}).get("post_checks", {}).get("pin_integrity", {}).get("ok") is not True:
            raise RuntimeError(f"params set pin integrity failed: {output_set}")

        output_params = require_ok(
            client.call(
                "node_params_get",
                {
                    "asset_path": function_asset,
                    "graph_kind": "material_function",
                    "graph_name": None,
                    "node_id": "NodeColor",
                },
            ),
            "material function output params readback",
        )
        require_param(output_params, "OutputName", "NodeColor")

        create = require_ok(
            client.call(
                "node_create",
                {
                    "asset_path": function_asset,
                    "graph_kind": "material_function",
                    "graph_name": None,
                    "node_class": "FunctionOutput",
                    "position": {"x": 260, "y": 110},
                    "params": {"OutputName": "DebugColor"},
                    "dry_run": False,
                },
            ),
            "material function node create",
        )
        if create.get("data", {}).get("created") is not True:
            raise RuntimeError(f"node_create did not report created=true: {create}")

        final_graph = require_ok(
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
            "material function final graph readback",
        )
        require_text_contains(final_graph, ["NodeColor", "DebugColor", "Node.Pos = -195,-45"])

        client.call("asset_compile", {"asset_path": function_asset})
        client.call("asset_save", {"asset_path": function_asset, "only_if_dirty": True, "fail_if_open_editor_conflict": True})
        client.call("editor_save_all", {"save_map_packages": True, "save_content_packages": True})
    finally:
        if not keep_assets:
            client.call("asset_delete", {"asset_path": function_asset, "dry_run": False, "allow_referenced": True, "cleanup_after_delete": True})
            client.call("asset_redirectors_fixup", {"folder_path": package_root, "dry_run": False})
            client.call("folder_delete", {"folder_path": run_root, "dry_run": False, "recursive": True})
            client.call("folder_delete", {"folder_path": package_root, "dry_run": False, "recursive": True})
            client.call("editor_save_all", {"save_map_packages": True, "save_content_packages": True})
            remaining = collect_disk_residue(project_file, package_root)
            if remaining:
                raise RuntimeError(f"disk residue remained under {package_root}: {remaining}")
