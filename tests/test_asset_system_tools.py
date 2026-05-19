from __future__ import annotations

from typing import Any

from ue_node_nexus_mcp import runtime, server

from tests.helpers import RecordingBridge


def test_write_parameter_tools_request_compile_by_default(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

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


def test_read_list_tools_default_to_compact(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    server.asset_list()
    server.level_actors_list()
    server.material_instance_params_get("/Game/Materials/MI_Example.MI_Example")

    assert recording_bridge.calls[0][1]["format"] == "compact"
    assert recording_bridge.calls[1][1]["format"] == "compact"
    assert recording_bridge.calls[2][1]["format"] == "compact"


def test_asset_create_defaults_to_dry_run(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    server.asset_create(
        asset_path="/Game/Materials/M_New.M_New",
        asset_kind="material",
    )

    assert recording_bridge.calls == [
        (
            "asset_create",
            {
                "asset_path": "/Game/Materials/M_New.M_New",
                "asset_kind": "material",
                "parent_asset_path": None,
                "parent_class_path": None,
                "dry_run": True,
                "save": False,
            },
        )
    ]


def test_blueprint_read_tools_default_to_compact(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    server.blueprint_details_get(asset_path="/Game/BP/BP_Example.BP_Example")
    server.anim_blueprint_summary_get(asset_path="/Game/ABP/ABP_Example.ABP_Example")

    assert recording_bridge.calls == [
        (
            "blueprint_details_get",
            {
                "asset_path": "/Game/BP/BP_Example.BP_Example",
                "include_defaults": True,
                "include_components": True,
                "property_names": [],
                "format": "compact",
            },
        ),
        (
            "anim_blueprint_summary_get",
            {
                "asset_path": "/Game/ABP/ABP_Example.ABP_Example",
                "format": "compact",
                "max_nodes": 200,
            },
        ),
    ]
