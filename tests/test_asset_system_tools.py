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


def test_read_list_tools_default_to_indexed_where_available(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    response = server.asset_list()
    server.level_actors_list()
    server.level_mesh_instances_list()
    server.material_expression_classes_list()
    server.material_instance_params_get("/Game/Materials/MI_Example.MI_Example")

    assert "remaining_errors" in response
    assert isinstance(response["remaining_errors"], str)
    assert recording_bridge.calls[0][1]["format"] == "indexed"
    assert recording_bridge.calls[1][1]["format"] == "indexed"
    assert recording_bridge.calls[2][1]["format"] == "indexed"
    assert recording_bridge.calls[3][1]["format"] == "indexed"
    assert recording_bridge.calls[3][1]["include_params"] is False
    assert recording_bridge.calls[4][1]["format"] == "compact"


def test_auto_index_tools_forward_indexed_payloads(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    server.auto_index_enable(root_path="/Game", rebuild=True)
    server.auto_index_overview()
    server.auto_index_tree_get(root_path="/Game", depth=2)
    server.auto_index_query(text="hero", class_names=["Blueprint"], package_paths=["/Game/Characters"])
    server.auto_index_resolve_path("BP_Hero")
    server.auto_index_diff_registry()

    assert recording_bridge.calls == [
        ("auto_index_enable", {"root_path": "/Game", "rebuild": True}),
        ("auto_index_overview", {"limit": 30, "format": "indexed"}),
        ("auto_index_tree_get", {"root_path": "/Game", "depth": 2, "limit": 120, "format": "indexed"}),
        (
            "auto_index_query",
            {
                "text": "hero",
                "class_names": ["Blueprint"],
                "package_paths": ["/Game/Characters"],
                "recursive": True,
                "include_redirectors": False,
                "limit": 80,
                "cursor": None,
                "format": "indexed",
            },
        ),
        ("auto_index_resolve_path", {"path": "BP_Hero", "limit": 20, "format": "indexed"}),
        ("auto_index_diff_registry", {"format": "indexed"}),
    ]


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


def test_asset_create_extended_kinds_forward_payload(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    server.asset_create("/Game/Test/MF_New.MF_New", "material_function")
    server.asset_create("/Game/Test/DA_New.DA_New", "data_asset", parent_class_path="/Script/Engine.DataAsset")
    server.asset_create("/Game/Test/RT_New.RT_New", "texture_render_target_2d")

    assert [call[1]["asset_kind"] for call in recording_bridge.calls] == [
        "material_function",
        "data_asset",
        "texture_render_target_2d",
    ]
    assert recording_bridge.calls[1][1]["parent_class_path"] == "/Script/Engine.DataAsset"


def test_editor_maintenance_tools_forward_payload(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    server.editor_save_all()
    server.editor_request_exit(save_before_exit=True, force=False)

    assert recording_bridge.calls == [
        (
            "editor_save_all",
            {
                "save_map_packages": True,
                "save_content_packages": True,
            },
        ),
        (
            "editor_request_exit",
            {
                "save_before_exit": True,
                "force": False,
            },
        ),
    ]


def test_level_material_read_tools_forward_compact_payloads(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    server.level_actor_get("/Temp/UEDPIE_0.World:PersistentLevel.Actor")
    server.level_actor_transform_get("/Temp/UEDPIE_0.World:PersistentLevel.Actor")
    server.object_properties_get(
        "/Temp/UEDPIE_0.World:PersistentLevel.Actor.StaticMeshComponent0",
        property_names=["Mobility"],
    )
    server.component_materials_get("/Temp/UEDPIE_0.World:PersistentLevel.Actor.StaticMeshComponent0")
    server.material_interface_resolve(
        component_path="/Temp/UEDPIE_0.World:PersistentLevel.Actor.StaticMeshComponent0",
        slot_index=0,
        include_params=True,
    )
    server.material_usage_find(material_path="/Game/Materials/M_Master.M_Master")
    server.component_material_instance_params_get(
        "/Temp/UEDPIE_0.World:PersistentLevel.Actor.StaticMeshComponent0",
        0,
    )

    assert recording_bridge.calls == [
        (
            "level_actor_get",
            {
                "actor_path": "/Temp/UEDPIE_0.World:PersistentLevel.Actor",
                "include_components": True,
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

    server.object_properties_set(
        "/Temp/UEDPIE_0.World:PersistentLevel.Actor",
        [{"name": "Tags", "value_text": "(Example)"}],
    )
    server.level_actor_transform_set(
        "/Temp/UEDPIE_0.World:PersistentLevel.Actor",
        location={"x": 1.0, "y": 2.0, "z": 3.0},
        rotation={"pitch": 0.0, "yaw": 90.0, "roll": 0.0},
    )
    server.component_materials_set(
        "/Temp/UEDPIE_0.World:PersistentLevel.Actor.StaticMeshComponent0",
        0,
        "/Game/Materials/M_Master.M_Master",
    )
    server.component_material_instance_params_set(
        "/Temp/UEDPIE_0.World:PersistentLevel.Actor.StaticMeshComponent0",
        0,
        [{"type": "scalar", "name": "Roughness", "value": 0.5}],
        create_dynamic=True,
    )

    assert recording_bridge.calls == [
        (
            "object_properties_set",
            {
                "object_path": "/Temp/UEDPIE_0.World:PersistentLevel.Actor",
                "params": [{"name": "Tags", "value_text": "(Example)"}],
                "dry_run": True,
                "allow_non_editable": False,
            },
        ),
        (
            "level_actor_transform_set",
            {
                "actor_path": "/Temp/UEDPIE_0.World:PersistentLevel.Actor",
                "location": {"x": 1.0, "y": 2.0, "z": 3.0},
                "rotation": {"pitch": 0.0, "yaw": 90.0, "roll": 0.0},
                "scale": None,
                "dry_run": True,
                "mark_dirty": True,
            },
        ),
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


def test_asset_management_write_tools_default_to_dry_run(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    server.asset_delete("/Game/Test/M_Delete.M_Delete")
    server.asset_move("/Game/Test/M_A.M_A", "/Game/Test/M_B.M_B")
    server.asset_rename("/Game/Test/M_B.M_B", "/Game/Test/M_C.M_C")
    server.asset_move_batch(
        [
            {
                "source_asset_path": "/Game/Test/M_C.M_C",
                "destination_asset_path": "/Game/Test/M_D.M_D",
            }
        ],
        dry_run=False,
        continue_on_error=True,
    )
    server.asset_rename_batch(
        [
            {
                "source_asset_path": "/Game/Test/M_D.M_D",
                "destination_asset_path": "/Game/Test/M_E.M_E",
            }
        ]
    )
    server.asset_duplicate("/Game/Test/M_C.M_C", "/Game/Test/M_D.M_D")
    server.folder_create("/Game/Test/NewFolder")
    server.folder_delete("/Game/Test/NewFolder", dry_run=False)
    server.asset_redirectors_fixup("/Game/Test")

    assert recording_bridge.calls == [
        (
            "asset_delete",
            {
                "asset_path": "/Game/Test/M_Delete.M_Delete",
                "dry_run": True,
                "allow_referenced": False,
                "cleanup_after_delete": True,
            },
        ),
        (
            "asset_move",
            {
                "source_asset_path": "/Game/Test/M_A.M_A",
                "destination_asset_path": "/Game/Test/M_B.M_B",
                "dry_run": True,
                "save": False,
                "fix_redirectors": True,
            },
        ),
        (
            "asset_rename",
            {
                "source_asset_path": "/Game/Test/M_B.M_B",
                "destination_asset_path": "/Game/Test/M_C.M_C",
                "dry_run": True,
                "save": False,
                "fix_redirectors": True,
            },
        ),
        (
            "asset_move_batch",
            {
                "items": [
                    {
                        "source_asset_path": "/Game/Test/M_C.M_C",
                        "destination_asset_path": "/Game/Test/M_D.M_D",
                    }
                ],
                "dry_run": False,
                "save": False,
                "fix_redirectors": True,
                "continue_on_error": True,
            },
        ),
        (
            "asset_rename_batch",
            {
                "items": [
                    {
                        "source_asset_path": "/Game/Test/M_D.M_D",
                        "destination_asset_path": "/Game/Test/M_E.M_E",
                    }
                ],
                "dry_run": True,
                "save": False,
                "fix_redirectors": True,
                "continue_on_error": False,
            },
        ),
        (
            "asset_duplicate",
            {
                "source_asset_path": "/Game/Test/M_C.M_C",
                "destination_asset_path": "/Game/Test/M_D.M_D",
                "dry_run": True,
                "save": False,
            },
        ),
        (
            "folder_create",
            {
                "folder_path": "/Game/Test/NewFolder",
                "dry_run": True,
            },
        ),
        (
            "folder_delete",
            {
                "folder_path": "/Game/Test/NewFolder",
                "dry_run": False,
                "recursive": True,
            },
        ),
        (
            "asset_redirectors_fixup",
            {
                "folder_path": "/Game/Test",
                "dry_run": True,
            },
        ),
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
