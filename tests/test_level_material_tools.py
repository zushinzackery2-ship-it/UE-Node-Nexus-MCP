from __future__ import annotations

from typing import Any

from ue_node_nexus_mcp import runtime

from tests import internal_tools
from tests.helpers import RecordingBridge


def test_level_material_read_tools_forward_compact_payloads(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.level_actor_get("/Temp/UEDPIE_0.World:PersistentLevel.Actor")
    internal_tools.level_actor_transform_get("/Temp/UEDPIE_0.World:PersistentLevel.Actor")
    internal_tools.object_properties_get(
        "/Temp/UEDPIE_0.World:PersistentLevel.Actor.StaticMeshComponent0",
        property_names=["Mobility"],
    )
    internal_tools.component_materials_get("/Temp/UEDPIE_0.World:PersistentLevel.Actor.StaticMeshComponent0")
    internal_tools.material_interface_resolve(
        component_path="/Temp/UEDPIE_0.World:PersistentLevel.Actor.StaticMeshComponent0",
        slot_index=0,
        include_params=True,
    )
    internal_tools.material_usage_find(material_path="/Game/Materials/M_Master.M_Master")
    internal_tools.component_material_instance_params_get(
        "/Temp/UEDPIE_0.World:PersistentLevel.Actor.StaticMeshComponent0",
        0,
    )

    assert recording_bridge.calls == [
        (
            "level_actor_get",
            {
                "actor_path": "/Temp/UEDPIE_0.World:PersistentLevel.Actor",
                "include_components": False,
            },
        ),
        (
            "level_actor_transform_get",
            {
                "actor_path": "/Temp/UEDPIE_0.World:PersistentLevel.Actor",
            },
        ),
        (
            "object_properties_get",
            {
                "object_path": "/Temp/UEDPIE_0.World:PersistentLevel.Actor.StaticMeshComponent0",
                "property_names": ["Mobility"],
                "include_non_editable": False,
                "format": "compact",
            },
        ),
        (
            "component_materials_get",
            {
                "component_path": "/Temp/UEDPIE_0.World:PersistentLevel.Actor.StaticMeshComponent0",
            },
        ),
        (
            "material_interface_resolve",
            {
                "asset_path": None,
                "material_path": None,
                "component_path": "/Temp/UEDPIE_0.World:PersistentLevel.Actor.StaticMeshComponent0",
                "slot_index": 0,
                "include_params": True,
            },
        ),
        (
            "material_usage_find",
            {
                "asset_path": None,
                "material_path": "/Game/Materials/M_Master.M_Master",
                "limit": 100,
                "cursor": None,
                "format": "indexed",
            },
        ),
        (
            "component_material_instance_params_get",
            {
                "component_path": "/Temp/UEDPIE_0.World:PersistentLevel.Actor.StaticMeshComponent0",
                "slot_index": 0,
            },
        ),
    ]


def test_level_material_write_tools_default_to_dry_run(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.component_materials_set(
        "/Temp/UEDPIE_0.World:PersistentLevel.Actor.StaticMeshComponent0",
        0,
        "/Game/Materials/M_Master.M_Master",
    )
    internal_tools.component_material_instance_params_set(
        "/Temp/UEDPIE_0.World:PersistentLevel.Actor.StaticMeshComponent0",
        0,
        [{"type": "scalar", "name": "Roughness", "value": 0.5}],
        create_dynamic=True,
    )

    assert recording_bridge.calls == [
        (
            "component_materials_set",
            {
                "component_path": "/Temp/UEDPIE_0.World:PersistentLevel.Actor.StaticMeshComponent0",
                "slot_index": 0,
                "material_path": "/Game/Materials/M_Master.M_Master",
                "dry_run": True,
            },
        ),
        (
            "component_material_instance_params_set",
            {
                "component_path": "/Temp/UEDPIE_0.World:PersistentLevel.Actor.StaticMeshComponent0",
                "slot_index": 0,
                "params": [{"type": "scalar", "name": "Roughness", "value": 0.5}],
                "dry_run": True,
                "create_dynamic": True,
            },
        ),
    ]
