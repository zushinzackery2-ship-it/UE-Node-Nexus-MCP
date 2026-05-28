from __future__ import annotations

from typing import Any

from ue_node_nexus_mcp import runtime

from tests import internal_tools
from tests.helpers import RecordingBridge


def test_blueprint_read_tools_default_to_compact(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.blueprint_details_get(asset_path="/Game/BP/BP_Example.BP_Example")
    internal_tools.anim_blueprint_summary_get(asset_path="/Game/ABP/ABP_Example.ABP_Example")

    assert recording_bridge.calls == [
        (
            "blueprint_details_get",
            {
                "asset_path": "/Game/BP/BP_Example.BP_Example",
                "include_defaults": False,
                "include_components": False,
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


def test_blueprint_components_patch_defaults_to_dry_run(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    operations = [
        {
            "op": "add_component",
            "name": "CameraBoom",
            "component_class": "/Script/Engine.SpringArmComponent",
            "parent": "RootComponent",
        }
    ]
    internal_tools.blueprint_components_patch(
        asset_path="/Game/BP/BP_Character.BP_Character",
        operations=operations,
    )

    assert recording_bridge.calls == [
        (
            "blueprint_components_patch",
            {
                "asset_path": "/Game/BP/BP_Character.BP_Character",
                "operations": operations,
                "dry_run": True,
                "compile_after": True,
                "format": "compact",
            },
        )
    ]


def test_project_input_mapping_tools_forward_payloads(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    operations = [
        {
            "op": "add_axis_mapping",
            "axis_name": "MoveForward",
            "key": "W",
            "scale": 1.0,
        }
    ]
    internal_tools.project_input_mappings_get()
    internal_tools.project_input_mappings_patch(operations=operations, dry_run=False)

    assert recording_bridge.calls == [
        ("project_input_mappings_get", {"format": "compact"}),
        (
            "project_input_mappings_patch",
            {
                "operations": operations,
                "dry_run": False,
                "save_config": True,
                "format": "compact",
            },
        ),
    ]


def test_niagara_duplicate_tools_use_generic_system_payloads(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.niagara_system_create(
        asset_path="/Game/FX/NS_Copy.NS_Copy",
        source_asset_path="/Game/FX/NS_Source.NS_Source",
        dry_run=False,
    )
    internal_tools.niagara_system_duplicate(
        source_asset_path="/Game/FX/NS_Source.NS_Source",
        destination_asset_path="/Game/FX/NS_Duplicate.NS_Duplicate",
    )

    assert recording_bridge.calls == [
        (
            "niagara_system_create",
            {
                "asset_path": "/Game/FX/NS_Copy.NS_Copy",
                "source_asset_path": "/Game/FX/NS_Source.NS_Source",
                "create_default_nodes": True,
                "dry_run": False,
                "save": False,
                "format": "summary",
            },
        ),
        (
            "niagara_system_duplicate",
            {
                "source_asset_path": "/Game/FX/NS_Source.NS_Source",
                "destination_asset_path": "/Game/FX/NS_Duplicate.NS_Duplicate",
                "dry_run": True,
                "save": False,
                "format": "summary",
            },
        ),
    ]
