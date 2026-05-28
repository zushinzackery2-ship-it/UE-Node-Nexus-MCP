from __future__ import annotations

import inspect
from typing import Any, get_args, get_type_hints

from ue_node_nexus_mcp import runtime, server
from ue_node_nexus_mcp import tools_graphs

from tests import internal_tools
from tests.helpers import RecordingBridge


def test_graph_snapshot_get_forwards_include_flags(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.graph_snapshot_get(
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

    internal_tools.graph_snapshot_get(asset_path="/Game/Materials/M_Example.M_Example")

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

    internal_tools.graph_node_info_get(
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
                "include_position": False,
            },
        )
    ]

def test_graph_node_info_get_can_include_position_via_default_tool(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.graph_node_info_get(
        asset_path="/Game/Materials/M_Example.M_Example",
        graph_kind="material",
        include_position=True,
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
                "format": "indexed",
                "id_mode": "alias",
                "include_position": True,
            },
        )
    ]

def test_graph_node_info_get_has_no_position_variant_wrapper() -> None:
    assert not hasattr(server, "graph_node_info_get_w_pos")
    assert not hasattr(tools_graphs, "graph_node_info_get_w_pos")


def test_graph_node_info_get_forwards_position_id_mode(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.graph_node_info_get(
        asset_path="/Game/Materials/M_Example.M_Example",
        graph_kind="material",
        id_mode="both",
        include_position=True,
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
                "format": "indexed",
                "id_mode": "both",
                "include_position": True,
            },
        )
    ]

def test_graph_node_info_get_forwards_grouped_format(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.graph_node_info_get(
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
                "include_position": False,
            },
        )
    ]

def test_graph_node_info_get_accepts_material_function_kind(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.graph_node_info_get(
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
                "include_position": False,
            },
        )
    ]

def test_graph_info_format_signature_exposes_dense_formats() -> None:
    signature = inspect.signature(tools_graphs.graph_node_info_get)
    type_hints = get_type_hints(tools_graphs.graph_node_info_get)
    format_args = get_args(type_hints["format"])

    assert signature.parameters["format"].default == "indexed"
    assert signature.parameters["include_position"].default is False
    assert format_args == ("indexed", "grouped", "text")

def test_graph_read_signatures_expose_material_function_kind() -> None:
    node_info_hints = get_type_hints(tools_graphs.graph_node_info_get)
    snapshot_hints = get_type_hints(tools_graphs.graph_snapshot_get)
    patch_hints = get_type_hints(tools_graphs.graph_patch_apply)

    expected = ("material", "material_function", "blueprint", "auto")
    assert get_args(node_info_hints["graph_kind"]) == expected
    assert get_args(snapshot_hints["graph_kind"]) == expected
    assert get_args(patch_hints["graph_kind"]) == expected

def test_tools_graphs_imports_without_server_cycle() -> None:
    assert tools_graphs.graph_node_info_get.__name__ == "graph_node_info_get"
    assert tools_graphs.graph_build_apply.__name__ == "graph_build_apply"

