from __future__ import annotations

from typing import Any

from ue_node_nexus_mcp import runtime
from ue_node_nexus_mcp.tools_graph_writes import graph_patch_apply


class RecordingBridge:
    def __init__(self) -> None:
        self.calls: list[tuple[str, dict[str, Any]]] = []
        self.response: dict[str, Any] = {
            "ok": True,
            "operation": "",
            "data": {},
            "diagnostics": [],
            "warnings": [],
        }

    def call(self, operation: str, payload: dict[str, Any], **_: Any) -> dict[str, Any]:
        self.calls.append((operation, payload))
        response = dict(self.response)
        response["operation"] = response.get("operation") or operation
        return response


class SequencedBridge:
    def __init__(self, responses: list[dict[str, Any]]) -> None:
        self.calls: list[tuple[str, dict[str, Any]]] = []
        self._responses = responses

    def call(self, operation: str, payload: dict[str, Any], **_: Any) -> dict[str, Any]:
        self.calls.append((operation, payload))
        response = dict(self._responses.pop(0))
        response["operation"] = response.get("operation") or operation
        response.setdefault("diagnostics", [])
        response.setdefault("warnings", [])
        return response


def test_material_graph_patch_expands_create_node_client_ids(monkeypatch: Any) -> None:
    bridge = SequencedBridge(
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
    monkeypatch.setattr(runtime, "bridge", bridge)

    response = graph_patch_apply(
        asset_path="/Game/Materials/M_Test.M_Test",
        graph_kind="material",
        dry_run=False,
        compile_after=False,
        operations=[
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
    assert response["data"]["client_side_expansion"]["created_nodes"][0]["client_id"] == "density"


def test_material_graph_patch_does_not_replace_pin_names_that_match_client_ids(monkeypatch: Any) -> None:
    bridge = SequencedBridge(
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
    monkeypatch.setattr(runtime, "bridge", bridge)

    response = graph_patch_apply(
        asset_path="/Game/Materials/M_Test.M_Test",
        graph_kind="material",
        dry_run=False,
        compile_after=False,
        operations=[
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


def test_material_graph_patch_client_id_dry_run_returns_clear_error(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    response = graph_patch_apply(
        asset_path="/Game/Materials/M_Test.M_Test",
        graph_kind="material",
        operations=[
            {"op": "create_node", "client_id": "mul", "node_class": "MaterialExpressionMultiply"},
            {"op": "connect_pins", "from_node_id": "mul", "from_pin": "0", "to_node_id": "Output", "to_pin": "A"},
        ],
    )

    assert response["ok"] is False
    assert response["error"]["code"] == "material_client_id_dry_run_unsupported"
    assert recording_bridge.calls == []


def test_graph_patch_nonfatal_material_attribute_pin_integrity_does_not_fail_root(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    recording_bridge.response = {
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
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    response = graph_patch_apply(
        asset_path="/Game/Materials/M_Test.M_Test",
        operations=[],
        dry_run=False,
        compile_after=False,
    )

    assert response["ok"] is True
    assert response["remaining_errors"] == 0
    assert response["warnings"][0]["code"] == "nonfatal_material_attribute_pin_integrity"
