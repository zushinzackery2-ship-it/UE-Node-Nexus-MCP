"""graph_patch_apply behavior exercised through the live ue_execute path.

These tests go through the registered MCP facade tool on the native bridge path.
"""

from __future__ import annotations

from typing import Any

from tests.support.bridges import RecordingBridge

from ue_node_nexus_mcp.server import ue_execute


def _patch(payload: dict[str, Any]) -> dict[str, Any]:
    return ue_execute("graph_patch_apply", payload, response={"mode": "full"})


def test_material_graph_patch_forwards_ordered_client_ids(all_features, use_bridge) -> None:
    bridge = use_bridge(RecordingBridge())

    response = _patch(
        {
            "asset_path": "/Game/Materials/M_Test.M_Test",
            "graph_kind": "material",
            "dry_run": False,
            "compile_after": False,
            "operations": [
                {
                    "op": "create_node",
                    "client_id": "density",
                    "node_class": "MaterialExpressionScalarParameter",
                    "params": {"ParameterName": "Density"},
                },
                {
                    "op": "create_node",
                    "client_id": "mul",
                    "node_class": "MaterialExpressionMultiply",
                },
                {
                    "op": "connect_pins",
                    "from_node_id": "density",
                    "from_pin": "0",
                    "to_node_id": "mul",
                    "to_pin": "B",
                },
            ],
        }
    )

    assert response["ok"] is True
    assert bridge.calls[0][0] == "graph_patch_apply"
    assert bridge.calls[0][1]["operations"][0]["client_id"] == "density"


def test_material_graph_patch_does_not_replace_pin_names_that_match_client_ids(all_features, use_bridge) -> None:
    bridge = use_bridge(RecordingBridge())

    response = _patch(
        {
            "asset_path": "/Game/Materials/M_Test.M_Test",
            "graph_kind": "material",
            "dry_run": False,
            "compile_after": False,
            "operations": [
                {
                    "op": "create_node",
                    "client_id": "source",
                    "node_class": "MaterialExpressionScalarParameter",
                },
                {
                    "op": "create_node",
                    "client_id": "target",
                    "node_class": "MaterialExpressionMultiply",
                },
                {
                    "op": "connect_pins",
                    "from_node_id": "source",
                    "from_pin": "target",
                    "to_node_id": "target",
                    "to_pin": "source",
                },
            ],
        }
    )

    assert response["ok"] is True
    assert bridge.calls[0][1]["operations"][2]["from_pin"] == "target"


def test_blueprint_graph_patch_forwards_payload_unchanged(all_features, use_bridge) -> None:
    bridge = use_bridge(RecordingBridge())
    payload = {
        "asset_path": "/Game/BP/BP_Test.BP_Test",
        "graph_kind": "blueprint",
        "dry_run": False,
        "operations": [
            {"op": "create_node", "client_id": "branch", "node_class": "Branch"},
            {"op": "connect_pins", "from_node_id": "branch", "from_pin": "Then", "to_node_id": "x", "to_pin": "execute"},
        ],
        "bridge_only_future_field": 123,
    }

    response = _patch(payload)

    assert response["ok"] is True
    assert bridge.calls == [("graph_patch_apply", payload)]


def test_graph_patch_nonfatal_material_attribute_pin_integrity_does_not_fail_root(all_features, use_bridge) -> None:
    bridge = use_bridge(RecordingBridge())
    bridge.response = {
        "ok": False,
        "operation": "graph_patch_apply",
        "data": {
            "applied": True,
            "changed": True,
            "post_checks": {
                "pin_integrity": {
                    "ok": False,
                    "broken_links": [
                        {
                            "node_id": "MaterialOutput",
                            "pin_id": "MaterialAttributes",
                            "source_node_id": "Source",
                            "reason": "bUseMaterialAttributes_false",
                        }
                    ],
                    "missing_pins": [],
                },
                "compile": {
                    "requested": False,
                    "ran": False,
                    "ok": True,
                    "error_count": 0,
                },
            },
        },
        "diagnostics": [],
        "warnings": [],
    }

    response = _patch(
        {
            "asset_path": "/Game/Materials/M_Test.M_Test",
            "operations": [],
            "dry_run": False,
            "compile_after": False,
        }
    )

    assert response["ok"] is True
    assert response["remaining_errors"] == 0
    assert response["warnings"][0]["code"] == "nonfatal_material_attribute_pin_integrity"
