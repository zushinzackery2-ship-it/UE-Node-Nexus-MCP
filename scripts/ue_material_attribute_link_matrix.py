from __future__ import annotations

import json
from datetime import datetime
from pathlib import Path
from typing import Any, Protocol


class BridgeLike(Protocol):
    last_http_response: str | None

    def call(self, operation: str, payload: dict[str, Any]) -> dict[str, Any]:
        ...


def object_path(package_path: str) -> str:
    name = package_path.rsplit("/", 1)[-1]
    return f"{package_path}.{name}"


def require_ok(response: dict[str, Any], context: str) -> dict[str, Any]:
    if response.get("ok") is not True:
        raise RuntimeError(f"{context} expected ok=true: {response}")
    return response


def call_expect_ok_false(
    client: BridgeLike,
    operation: str,
    payload: dict[str, Any],
) -> dict[str, Any]:
    try:
        response = client.call(operation, payload)
    except RuntimeError:
        raw = client.last_http_response
        if not raw:
            raise
        response = json.loads(raw)

    if response.get("ok") is not False:
        raise RuntimeError(f"{operation} expected ok=false, got: {response}")
    return response


def call_expect_bridge_failure(
    client: BridgeLike,
    operation: str,
    payload: dict[str, Any],
    expected_code: str,
) -> dict[str, Any]:
    response = call_expect_ok_false(client, operation, payload)
    diagnostics = response.get("diagnostics", [])
    if not any(isinstance(item, dict) and item.get("code") == expected_code for item in diagnostics):
        raise RuntimeError(f"{operation} missing diagnostic {expected_code}: {response}")
    return response


def assert_param_changed(response: dict[str, Any], name: str, before: str, after: str) -> None:
    changes = response.get("data", {}).get("diff", {}).get("params_changed", [])
    for change in changes:
        if (
            isinstance(change, dict)
            and change.get("name") == name
            and change.get("before") == before
            and change.get("after") == after
        ):
            return
    raise RuntimeError(f"missing param change {name}: {before}->{after}: {response}")


def assert_link_text(response: dict[str, Any], expected: str) -> None:
    text = str(response.get("data", {}).get("text", ""))
    if expected not in text:
        raise RuntimeError(f"graph readback missing {expected!r}: {text}")


def assert_integrity_reason(response: dict[str, Any], expected_reason: str) -> None:
    integrity = response.get("data", {}).get("post_checks", {}).get("pin_integrity", {})
    broken_links = integrity.get("broken_links", [])
    if integrity.get("ok") is not False:
        raise RuntimeError(f"pin integrity expected ok=false: {response}")
    if not any(isinstance(item, dict) and item.get("reason") == expected_reason for item in broken_links):
        raise RuntimeError(f"pin integrity missing reason {expected_reason}: {response}")


def create_material_node(
    client: BridgeLike,
    asset_path: str,
    node_class: str,
    name: str,
    params: dict[str, Any] | None = None,
) -> str:
    response = require_ok(
        client.call(
            "node_create",
            {
                "asset_path": asset_path,
                "graph_kind": "material",
                "graph_name": None,
                "node_class": node_class,
                "name": name,
                "position": {"x": -420, "y": 0},
                "params": params or {},
                "dry_run": False,
            },
        ),
        f"create {node_class}",
    )
    node_id = response.get("data", {}).get("node_id")
    if not isinstance(node_id, str) or not node_id:
        raise RuntimeError(f"{node_class} create did not return node_id: {response}")
    return node_id


def run_material_attribute_link_matrix(
    client: BridgeLike,
    project_file: Path,
    package_root: str,
    keep_assets: bool,
    collect_disk_residue: Any,
) -> None:
    run_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    run_root = f"{package_root.rstrip('/')}/MaterialAttributes_{run_id}"
    valid_package = f"{run_root}/M_ValidMA_AutoEnable"
    invalid_package = f"{run_root}/M_InvalidMA_TypeMismatch"
    valid_path = object_path(valid_package)
    invalid_path = object_path(invalid_package)

    client.call("folder_create", {"folder_path": run_root, "dry_run": False})
    client.call("asset_create", {"asset_kind": "material", "asset_path": valid_package, "dry_run": False, "save": True})
    client.call("asset_create", {"asset_kind": "material", "asset_path": invalid_package, "dry_run": False, "save": True})

    try:
        make_ma_id = create_material_node(client, valid_path, "MakeMaterialAttributes", "N_MakeMA")
        color_id = create_material_node(
            client,
            invalid_path,
            "Constant3Vector",
            "N_Color",
            {"Constant": "(R=0.2,G=0.5,B=0.9,A=1.0)"},
        )

        valid_patch = require_ok(
            client.call(
                "graph_patch_apply",
                {
                    "asset_path": valid_path,
                    "graph_kind": "material",
                    "graph_name": None,
                    "compile_after": True,
                    "dry_run": False,
                    "format": "full",
                    "operations": [
                        {
                            "op": "connect_pins",
                            "from_node_id": make_ma_id,
                            "from_pin_id": "0",
                            "to_node_id": "MaterialOutput",
                            "to_pin_id": "MaterialAttributes",
                        }
                    ],
                },
            ),
            "valid material attributes connect",
        )
        assert_param_changed(valid_patch, "bUseMaterialAttributes", "False", "True")

        valid_readback = require_ok(
            client.call(
                "graph_node_info_get_w_pos",
                {
                    "asset_path": valid_path,
                    "graph_kind": "material",
                    "graph_name": None,
                    "format": "text",
                    "id_mode": "alias",
                    "section": "links",
                    "max_nodes": 20,
                },
            ),
            "valid readback",
        )
        assert_link_text(valid_readback, "MaterialOutput.inpin_00.MaterialAttributes")

        invalid_patch = call_expect_bridge_failure(
            client,
            "graph_patch_apply",
            {
                "asset_path": invalid_path,
                "graph_kind": "material",
                "graph_name": None,
                "compile_after": True,
                "dry_run": False,
                "format": "full",
                "operations": [
                    {
                        "op": "connect_pins",
                        "from_node_id": color_id,
                        "from_pin_id": "0",
                        "to_node_id": "MaterialOutput",
                        "to_pin_id": "MaterialAttributes",
                    }
                ],
            },
            "material_pin_type_mismatch",
        )
        if invalid_patch.get("data", {}).get("changed") is not False:
            raise RuntimeError(f"invalid material attributes connect changed graph: {invalid_patch}")

        invalid_readback = require_ok(
            client.call(
                "graph_node_info_get_w_pos",
                {
                    "asset_path": invalid_path,
                    "graph_kind": "material",
                    "graph_name": None,
                    "format": "text",
                    "id_mode": "alias",
                    "section": "links",
                    "max_nodes": 20,
                },
            ),
            "invalid readback",
        )
        assert_link_text(invalid_readback, "outpin_00.0 > None")

        integrity_response = call_expect_ok_false(
            client,
            "node_params_set",
            {
                "asset_path": valid_path,
                "graph_kind": "material",
                "graph_name": None,
                "node_id": "MaterialOutput",
                "params": {"bUseMaterialAttributes": "False"},
                "dry_run": False,
                "compile_after": True,
            },
        )
        assert_integrity_reason(integrity_response, "bUseMaterialAttributes_false")

        client.call("editor_save_all", {"save_map_packages": True, "save_content_packages": True})
    finally:
        if not keep_assets:
            client.call("asset_delete", {"asset_path": valid_path, "dry_run": False, "allow_referenced": True})
            client.call("asset_delete", {"asset_path": invalid_path, "dry_run": False, "allow_referenced": True})
            client.call("asset_redirectors_fixup", {"folder_path": package_root, "dry_run": False})
            client.call("folder_delete", {"folder_path": run_root, "dry_run": False, "recursive": True})
            client.call("folder_delete", {"folder_path": package_root, "dry_run": False, "recursive": True})
            client.call("editor_save_all", {"save_map_packages": True, "save_content_packages": True})
            remaining = collect_disk_residue(project_file, package_root)
            if remaining:
                raise RuntimeError(f"disk residue remained under {package_root}: {remaining}")
