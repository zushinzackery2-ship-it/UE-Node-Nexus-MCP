"""graph_patch_apply behavior exercised through the live ue_execute path.

These tests go through the registered MCP facade tool (not the legacy wrapper)
so the material client_id expansion, the dry-run guard, and the response
normalization are verified on the code path real clients hit.
"""

from __future__ import annotations

from typing import Any

from conftest import RecordingBridge, SequencedBridge

from ue_node_nexus_mcp.server import ue_execute


def _patch(payload: dict[str, Any]) -> dict[str, Any]:
    return ue_execute("graph_patch_apply", payload, response={"mode": "full"})


def test_material_graph_patch_expands_create_node_client_ids(all_features, use_bridge) -> None:
    bridge = use_bridge(
        SequencedBridge(
            [
                {"ok": True, "data": {"node_id": "node-param", "node_alias": "Density"}},
                {"ok": True, "data": {"node_id": "node-mul", "node_alias": "Multiply_01"}},
                {
                    "ok": True,
                    "data": {
                        "applied": True,
                        "diff": {"nodes_created": [], "links_added": []},
                        "post_checks": {"compile": {"error_count": 0}},
                    },
                },
            ]
        )
    )

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
    assert [call[0] for call in bridge.calls] == ["node_create", "node_create", "graph_patch_apply"]
    assert bridge.calls[2][1]["operations"] == [
        {
            "op": "connect_pins",
            "from_node_id": "node-param",
            "from_pin": "0",
            "to_node_id": "node-mul",
            "to_pin": "B",
        }
    ]
    assert bridge.calls[2][1]["dry_run"] is False
    assert response["data"]["client_side_expansion"]["created_nodes"][0]["client_id"] == "density"


def test_material_graph_patch_does_not_replace_pin_names_that_match_client_ids(all_features, use_bridge) -> None:
    bridge = use_bridge(
        SequencedBridge(
            [
                {"ok": True, "data": {"node_id": "node-source", "node_alias": "Source"}},
                {"ok": True, "data": {"node_id": "node-target", "node_alias": "Target"}},
                {
                    "ok": True,
                    "data": {
                        "applied": True,
                        "diff": {"nodes_created": [], "links_added": []},
                        "post_checks": {"compile": {"error_count": 0}},
                    },
                },
            ]
        )
    )

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
    assert bridge.calls[2][1]["operations"] == [
        {
            "op": "connect_pins",
            "from_node_id": "node-source",
            "from_pin": "target",
            "to_node_id": "node-target",
            "to_pin": "source",
        }
    ]


def test_material_graph_patch_client_id_dry_run_returns_clear_error(all_features, use_bridge) -> None:
    bridge = use_bridge(RecordingBridge())

    response = _patch(
        {
            "asset_path": "/Game/Materials/M_Test.M_Test",
            "graph_kind": "material",
            "operations": [
                {"op": "create_node", "client_id": "mul", "node_class": "MaterialExpressionMultiply"},
                {"op": "connect_pins", "from_node_id": "mul", "from_pin": "0", "to_node_id": "Output", "to_pin": "A"},
            ],
        }
    )

    assert response["ok"] is False
    assert response["error"]["code"] == "material_client_id_dry_run_unsupported"
    assert bridge.calls == []


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
