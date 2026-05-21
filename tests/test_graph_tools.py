from __future__ import annotations

import inspect
from typing import Any, get_args, get_type_hints

from ue_node_nexus_mcp import runtime, server
from ue_node_nexus_mcp import tools_graphs

from tests.helpers import RecordingBridge


def test_graph_patch_apply_requests_compile_by_default(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

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
                "format": "compact",
            },
        )
    ]


def test_graph_patch_apply_accepts_material_function_kind(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    server.graph_patch_apply(
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

    server.graph_build_apply(
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

    server.graph_build_apply(
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

    server.graph_build_apply(
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


def test_node_params_get_forwards_graph_identity(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

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


def test_node_params_get_accepts_material_function_kind(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    server.node_params_get(
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

    server.node_info_get(
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

    server.node_info_get(
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

    server.node_position_get(
        asset_path="/Game/Materials/M_Example.M_Example",
        node_id="Clamp_00",
    )
    server.node_position_set(
        asset_path="/Game/Materials/M_Example.M_Example",
        node_id="Clamp_00",
        x=-100,
        y=200,
    )
    server.node_position_offset(
        asset_path="/Game/Materials/M_Example.M_Example",
        node_id="Clamp_00",
        dx=10,
        dy=-20,
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
        (
            "node_position_offset",
            {
                "asset_path": "/Game/Materials/M_Example.M_Example",
                "node_id": "Clamp_00",
                "dx": 10,
                "dy": -20,
                "graph_name": None,
                "graph_kind": "auto",
                "dry_run": True,
            },
        ),
    ]


def test_node_position_tools_accept_material_function_kind(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    server.node_position_set(
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

    server.node_create(
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

    server.node_create(
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


def test_graph_snapshot_get_forwards_include_flags(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    server.graph_snapshot_get(
        asset_path="/Game/Materials/M_Example.M_Example",
        graph_kind="material",
        format="full",
        include_node_params=False,
        include_links=False,
    )

    assert recording_bridge.calls == [
        (
            "graph_snapshot_get",
            {
                "asset_path": "/Game/Materials/M_Example.M_Example",
                "graph_name": None,
                "graph_kind": "material",
                "format": "full",
                "include_node_params": False,
                "include_links": False,
            },
        )
    ]


def test_graph_snapshot_get_defaults_to_wires_tiny_without_params(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    server.graph_snapshot_get(asset_path="/Game/Materials/M_Example.M_Example")

    assert recording_bridge.calls == [
        (
            "graph_snapshot_get",
            {
                "asset_path": "/Game/Materials/M_Example.M_Example",
                "graph_name": None,
                "graph_kind": "auto",
                "format": "wires_tiny",
                "include_node_params": False,
                "include_links": True,
            },
        )
    ]


def test_graph_node_info_get_forwards_whole_graph_node_view(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    server.graph_node_info_get(
        asset_path="/Game/Materials/M_Example.M_Example",
        graph_kind="material",
        section="links",
        max_nodes=25,
    )

    assert recording_bridge.calls == [
        (
            "graph_node_info_get",
            {
                "asset_path": "/Game/Materials/M_Example.M_Example",
                "graph_name": None,
                "graph_kind": "material",
                "section": "links",
                "max_nodes": 25,
                "format": "indexed",
                "id_mode": "alias",
            },
        )
    ]


def test_graph_node_info_get_w_pos_forwards_position_variant(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    server.graph_node_info_get_w_pos(
        asset_path="/Game/Materials/M_Example.M_Example",
        graph_kind="material",
        id_mode="both",
    )

    assert recording_bridge.calls == [
        (
            "graph_node_info_get_w_pos",
            {
                "asset_path": "/Game/Materials/M_Example.M_Example",
                "graph_name": None,
                "graph_kind": "material",
                "section": "all",
                "max_nodes": None,
                "format": "indexed",
                "id_mode": "both",
            },
        )
    ]


def test_graph_node_info_get_forwards_grouped_format(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    server.graph_node_info_get(
        asset_path="/Game/Materials/M_Example.M_Example",
        graph_kind="material",
        format="grouped",
    )

    assert recording_bridge.calls == [
        (
            "graph_node_info_get",
            {
                "asset_path": "/Game/Materials/M_Example.M_Example",
                "graph_name": None,
                "graph_kind": "material",
                "section": "all",
                "max_nodes": None,
                "format": "grouped",
                "id_mode": "alias",
            },
        )
    ]


def test_graph_node_info_get_accepts_material_function_kind(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    server.graph_node_info_get(
        asset_path="/Game/Materials/Functions/MF_Test.MF_Test",
        graph_kind="material_function",
        format="indexed",
    )

    assert recording_bridge.calls == [
        (
            "graph_node_info_get",
            {
                "asset_path": "/Game/Materials/Functions/MF_Test.MF_Test",
                "graph_name": None,
                "graph_kind": "material_function",
                "section": "all",
                "max_nodes": None,
                "format": "indexed",
                "id_mode": "alias",
            },
        )
    ]


def test_node_class_params_get_forwards_class_template(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    server.node_class_params_get(graph_kind="material", node_class="ScalarParameter")

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

    server.node_class_params_get(graph_kind="material_function", node_class="FunctionInput")

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

    server.node_params_set(
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


def test_graph_info_format_signature_exposes_dense_formats() -> None:
    signature = inspect.signature(tools_graphs.graph_node_info_get)
    type_hints = get_type_hints(tools_graphs.graph_node_info_get)
    format_args = get_args(type_hints["format"])

    assert signature.parameters["format"].default == "indexed"
    assert format_args == ("indexed", "grouped", "text")


def test_graph_read_signatures_expose_material_function_kind() -> None:
    node_info_hints = get_type_hints(tools_graphs.graph_node_info_get)
    node_info_w_pos_hints = get_type_hints(tools_graphs.graph_node_info_get_w_pos)
    snapshot_hints = get_type_hints(tools_graphs.graph_snapshot_get)
    patch_hints = get_type_hints(tools_graphs.graph_patch_apply)

    expected = ("material", "material_function", "blueprint", "auto")
    assert get_args(node_info_hints["graph_kind"]) == expected
    assert get_args(node_info_w_pos_hints["graph_kind"]) == expected
    assert get_args(snapshot_hints["graph_kind"]) == expected
    assert get_args(patch_hints["graph_kind"]) == expected


def test_node_signatures_expose_material_function_kind() -> None:
    expected_with_auto = ("material", "material_function", "blueprint", "auto")
    expected_without_auto = ("material", "material_function", "blueprint")

    assert get_args(get_type_hints(tools_graphs.node_class_params_get)["graph_kind"]) == expected_without_auto
    assert get_args(get_type_hints(tools_graphs.node_params_get)["graph_kind"]) == expected_with_auto
    assert get_args(get_type_hints(tools_graphs.node_params_set)["graph_kind"]) == expected_with_auto
    assert get_args(get_type_hints(tools_graphs.node_info_get)["graph_kind"]) == expected_with_auto
    assert get_args(get_type_hints(tools_graphs.node_create)["graph_kind"]) == expected_with_auto
    assert get_args(get_type_hints(tools_graphs.node_position_get)["graph_kind"]) == expected_with_auto
    assert get_args(get_type_hints(tools_graphs.node_position_set)["graph_kind"]) == expected_with_auto
    assert get_args(get_type_hints(tools_graphs.node_position_offset)["graph_kind"]) == expected_with_auto


def test_tools_graphs_imports_without_server_cycle() -> None:
    assert tools_graphs.graph_node_info_get.__name__ == "graph_node_info_get"
    assert tools_graphs.graph_build_apply.__name__ == "graph_build_apply"
