from __future__ import annotations

from typing import Any

from ue_node_nexus_mcp import runtime

from tests import internal_tools
from tests.helpers import RecordingBridge


def test_node_params_get_forwards_graph_identity(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.node_params_get(
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

def test_node_params_get_accepts_material_function_kind(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.node_params_get(
        asset_path="/Game/Materials/Functions/MF_Test.MF_Test",
        node_id="Custom_00",
        graph_kind="material_function",
    )

    assert recording_bridge.calls == [
        (
            "node_params_get",
            {
                "asset_path": "/Game/Materials/Functions/MF_Test.MF_Test",
                "node_id": "Custom_00",
                "graph_name": None,
                "graph_kind": "material_function",
            },
        )
    ]

def test_node_info_get_forwards_compact_selection(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.node_info_get(
        asset_path="/Game/Materials/M_Example.M_Example",
        node_id="Clamp_00",
        graph_kind="material",
        section="input",
        index=0,
    )

    assert recording_bridge.calls == [
        (
            "node_info_get",
            {
                "asset_path": "/Game/Materials/M_Example.M_Example",
                "node_id": "Clamp_00",
                "graph_name": None,
                "graph_kind": "material",
                "section": "input",
                "index": 0,
                "format": "text",
            },
        )
    ]

def test_node_info_get_accepts_material_function_kind(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.node_info_get(
        asset_path="/Game/Materials/Functions/MF_Test.MF_Test",
        node_id="Custom_00",
        graph_kind="material_function",
        section="param",
    )

    assert recording_bridge.calls == [
        (
            "node_info_get",
            {
                "asset_path": "/Game/Materials/Functions/MF_Test.MF_Test",
                "node_id": "Custom_00",
                "graph_name": None,
                "graph_kind": "material_function",
                "section": "param",
                "index": None,
                "format": "text",
            },
        )
    ]

def test_node_position_tools_default_to_dry_run(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.node_position_get(
        asset_path="/Game/Materials/M_Example.M_Example",
        node_id="Clamp_00",
    )
    internal_tools.node_position_set(
        asset_path="/Game/Materials/M_Example.M_Example",
        node_id="Clamp_00",
        x=-100,
        y=200,
    )

    assert recording_bridge.calls == [
        (
            "node_position_get",
            {
                "asset_path": "/Game/Materials/M_Example.M_Example",
                "node_id": "Clamp_00",
                "graph_name": None,
                "graph_kind": "auto",
            },
        ),
        (
            "node_position_set",
            {
                "asset_path": "/Game/Materials/M_Example.M_Example",
                "node_id": "Clamp_00",
                "x": -100,
                "y": 200,
                "graph_name": None,
                "graph_kind": "auto",
                "dry_run": True,
            },
        ),
    ]

def test_node_position_tools_accept_material_function_kind(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.node_position_set(
        asset_path="/Game/Materials/Functions/MF_Test.MF_Test",
        node_id="Custom_00",
        graph_kind="material_function",
        x=10,
        y=20,
        dry_run=False,
    )

    assert recording_bridge.calls == [
        (
            "node_position_set",
            {
                "asset_path": "/Game/Materials/Functions/MF_Test.MF_Test",
                "node_id": "Custom_00",
                "x": 10,
                "y": 20,
                "graph_name": None,
                "graph_kind": "material_function",
                "dry_run": False,
            },
        )
    ]

def test_node_create_defaults_to_dry_run(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.node_create(
        asset_path="/Game/Materials/M_Example.M_Example",
        node_class="Clamp",
        graph_kind="material",
        position={"x": -100, "y": 200},
        params={"MinDefault": 0.0},
    )

    assert recording_bridge.calls == [
        (
            "node_create",
            {
                "asset_path": "/Game/Materials/M_Example.M_Example",
                "node_class": "Clamp",
                "graph_name": None,
                "graph_kind": "material",
                "name": None,
                "position": {"x": -100, "y": 200},
                "params": {"MinDefault": 0.0},
                "dry_run": True,
            },
        )
    ]

def test_node_create_accepts_material_function_kind(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.node_create(
        asset_path="/Game/Materials/Functions/MF_Test.MF_Test",
        graph_kind="material_function",
        node_class="FunctionOutput",
        position={"x": 200, "y": 0},
        params={"OutputName": "Color"},
        dry_run=False,
    )

    assert recording_bridge.calls == [
        (
            "node_create",
            {
                "asset_path": "/Game/Materials/Functions/MF_Test.MF_Test",
                "node_class": "FunctionOutput",
                "graph_name": None,
                "graph_kind": "material_function",
                "name": None,
                "position": {"x": 200, "y": 0},
                "params": {"OutputName": "Color"},
                "dry_run": False,
            },
        )
    ]


