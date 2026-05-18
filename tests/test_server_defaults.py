from __future__ import annotations

from typing import Any

from ue_node_nexus_mcp import server


class RecordingBridge:
    def __init__(self) -> None:
        self.calls: list[tuple[str, dict[str, Any]]] = []

    def call(self, operation: str, payload: dict[str, Any]) -> dict[str, Any]:
        self.calls.append((operation, payload))
        return {
            "ok": True,
            "operation": operation,
            "data": {},
            "diagnostics": [],
            "warnings": [],
        }


def test_graph_patch_apply_requests_compile_by_default(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(server, "bridge", recording_bridge)

    result = server.graph_patch_apply(
        asset_path="/Game/BP/BP_Example.BP_Example",
        operations=[],
        dry_run=False,
    )

    assert result["ok"] is True
    assert recording_bridge.calls == [
        (
            "graph_patch_apply",
            {
                "asset_path": "/Game/BP/BP_Example.BP_Example",
                "operations": [],
                "graph_name": None,
                "graph_kind": "auto",
                "dry_run": False,
                "compile_after": True,
            },
        )
    ]


def test_write_parameter_tools_request_compile_by_default(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(server, "bridge", recording_bridge)

    server.node_params_set(
        asset_path="/Game/Materials/M_Example.M_Example",
        node_id="node-a",
        params={"default_value": 0.5},
        dry_run=False,
    )
    server.material_instance_params_set(
        asset_path="/Game/Materials/MI_Example.MI_Example",
        params=[{"type": "scalar", "name": "Roughness", "value": 0.8}],
        dry_run=False,
    )

    assert recording_bridge.calls[0][1]["compile_after"] is True
    assert recording_bridge.calls[1][1]["compile_after"] is True


def test_node_params_get_forwards_graph_identity(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(server, "bridge", recording_bridge)

    server.node_params_get(
        asset_path="/Game/BP/BP_Example.BP_Example",
        node_id="node-a",
        graph_name="EventGraph",
        graph_kind="blueprint",
    )

    assert recording_bridge.calls == [
        (
            "node_params_get",
            {
                "asset_path": "/Game/BP/BP_Example.BP_Example",
                "node_id": "node-a",
                "graph_name": "EventGraph",
                "graph_kind": "blueprint",
            },
        )
    ]
