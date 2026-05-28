from __future__ import annotations

from typing import Any

from ue_node_nexus_mcp import runtime

from tests import internal_tools
from tests.helpers import RecordingBridge


def test_node_class_params_get_forwards_class_template(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.node_class_params_get(graph_kind="material", node_class="ScalarParameter")

    assert recording_bridge.calls == [
        (
            "node_class_params_get",
            {
                "graph_kind": "material",
                "node_class": "ScalarParameter",
            },
        )
    ]


def test_node_class_params_get_accepts_material_function_kind(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.node_class_params_get(graph_kind="material_function", node_class="FunctionInput")

    assert recording_bridge.calls == [
        (
            "node_class_params_get",
            {
                "graph_kind": "material_function",
                "node_class": "FunctionInput",
            },
        )
    ]


def test_node_params_set_accepts_material_function_kind(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.node_params_set(
        asset_path="/Game/Materials/Functions/MF_Test.MF_Test",
        node_id="Color",
        graph_kind="material_function",
        params={"OutputName": "Result"},
        dry_run=False,
        compile_after=True,
    )

    assert recording_bridge.calls == [
        (
            "node_params_set",
            {
                "asset_path": "/Game/Materials/Functions/MF_Test.MF_Test",
                "node_id": "Color",
                "params": {"OutputName": "Result"},
                "graph_name": None,
                "graph_kind": "material_function",
                "dry_run": False,
                "compile_after": True,
            },
        )
    ]
