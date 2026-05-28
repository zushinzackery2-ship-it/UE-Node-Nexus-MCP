from __future__ import annotations

import inspect
from typing import Any, get_args, get_type_hints

from ue_node_nexus_mcp import runtime
from ue_node_nexus_mcp import tools_graphs

from tests import internal_tools
from tests.helpers import RecordingBridge


def test_graph_patch_apply_requests_compile_by_default(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    result = internal_tools.graph_patch_apply(
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
                "format": "compact",
            },
        )
    ]

def test_graph_patch_apply_accepts_material_function_kind(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.graph_patch_apply(
        asset_path="/Game/Materials/Functions/MF_Test.MF_Test",
        graph_kind="material_function",
        operations=[],
        dry_run=False,
    )

    assert recording_bridge.calls == [
        (
            "graph_patch_apply",
            {
                "asset_path": "/Game/Materials/Functions/MF_Test.MF_Test",
                "operations": [],
                "graph_name": None,
                "graph_kind": "material_function",
                "dry_run": False,
                "compile_after": True,
                "format": "compact",
            },
        )
    ]

def test_graph_build_apply_forwards_compact_material_spec(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    nodes = [
        {
            "id": "custom",
            "node_class": "Custom",
            "position": {"x": 0, "y": 0},
            "params": {
                "Code": "return InColor;",
                "OutputType": "CMOT_Float3",
                "Inputs": ["InColor"],
            },
        },
        {
            "id": "color",
            "node_class": "VectorParameter",
            "position": {"x": -260, "y": 0},
            "params": {"ParameterName": "InColor"},
        },
    ]
    links = [
        {
            "from": {"node": "color", "pin": "0"},
            "to": {"node": "custom", "pin": "InColor"},
        }
    ]
    outputs = [
        {
            "property": "BaseColor",
            "from": {"node": "custom", "pin": "0"},
        }
    ]

    internal_tools.graph_build_apply(
        asset_path="/Game/Materials/M_Custom.M_Custom",
        nodes=nodes,
        links=links,
        material_outputs=outputs,
        dry_run=False,
    )

    assert recording_bridge.calls == [
        (
            "graph_build_apply",
            {
                "asset_path": "/Game/Materials/M_Custom.M_Custom",
                "nodes": nodes,
                "links": links,
                "material_outputs": outputs,
                "graph_name": None,
                "graph_kind": "material",
                "dry_run": False,
                "compile_after": True,
                "format": "compact",
            },
        )
    ]

def test_graph_build_apply_forwards_endpoint_shorthand_and_structured_color(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    nodes = [
        {
            "id": "base_color",
            "node_class": "VectorParameter",
            "position": {"x": -300, "y": 0},
            "params": {
                "ParameterName": "BaseColor",
                "DefaultValue": {"r": 0.1, "g": 0.6, "b": 0.9, "a": 1.0},
            },
        },
        {
            "id": "roughness",
            "node_class": "ScalarParameter",
            "position": {"x": -300, "y": 180},
            "params": {"ParameterName": "Roughness", "DefaultValue": 0.35},
        },
    ]
    outputs = [
        {"from": "base_color.RGB", "to": "MaterialOutput.BaseColor"},
        {"from": "roughness.Value", "to": "MaterialOutput.Roughness"},
    ]

    internal_tools.graph_build_apply(
        asset_path="/Game/Materials/M_Shorthand.M_Shorthand",
        nodes=nodes,
        material_outputs=outputs,
        dry_run=False,
    )

    assert recording_bridge.calls == [
        (
            "graph_build_apply",
            {
                "asset_path": "/Game/Materials/M_Shorthand.M_Shorthand",
                "nodes": nodes,
                "links": [],
                "material_outputs": outputs,
                "graph_name": None,
                "graph_kind": "material",
                "dry_run": False,
                "compile_after": True,
                "format": "compact",
            },
        )
    ]

def test_graph_build_apply_forwards_material_function_spec(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    nodes = [
        {
            "id": "input",
            "node_class": "FunctionInput",
            "position": {"x": -300, "y": 0},
            "params": {"InputName": "InColor", "InputType": "FunctionInput_Vector3"},
        },
        {
            "id": "output",
            "node_class": "FunctionOutput",
            "position": {"x": 200, "y": 0},
            "params": {"OutputName": "Color"},
        },
    ]
    links = [
        {
            "from": {"node": "input", "pin": "0"},
            "to": {"node": "output", "pin": "A"},
        }
    ]

    internal_tools.graph_build_apply(
        asset_path="/Game/Materials/Functions/MF_Test.MF_Test",
        graph_kind="material_function",
        nodes=nodes,
        links=links,
        dry_run=False,
    )

    assert recording_bridge.calls == [
        (
            "graph_build_apply",
            {
                "asset_path": "/Game/Materials/Functions/MF_Test.MF_Test",
                "nodes": nodes,
                "links": links,
                "material_outputs": [],
                "graph_name": None,
                "graph_kind": "material_function",
                "dry_run": False,
                "compile_after": True,
                "format": "compact",
            },
        )
    ]

