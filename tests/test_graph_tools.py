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


def test_graph_info_format_signature_exposes_dense_formats() -> None:
    signature = inspect.signature(tools_graphs.graph_node_info_get)
    type_hints = get_type_hints(tools_graphs.graph_node_info_get)
    format_args = get_args(type_hints["format"])

    assert signature.parameters["format"].default == "indexed"
    assert format_args == ("indexed", "grouped", "text")


def test_tools_graphs_imports_without_server_cycle() -> None:
    assert tools_graphs.graph_node_info_get.__name__ == "graph_node_info_get"
