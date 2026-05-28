from __future__ import annotations

from typing import Any

from ue_node_nexus_mcp import runtime

from tests import internal_tools
from tests.helpers import RecordingBridge


def test_asset_create_defaults_to_dry_run(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.asset_create(
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

    internal_tools.asset_create("/Game/Test/MF_New.MF_New", "material_function")
    internal_tools.asset_create("/Game/Test/DA_New.DA_New", "data_asset", parent_class_path="/Script/Engine.DataAsset")
    internal_tools.asset_create("/Game/Test/RT_New.RT_New", "texture_render_target_2d")

    assert [call[1]["asset_kind"] for call in recording_bridge.calls] == [
        "material_function",
        "data_asset",
        "texture_render_target_2d",
    ]
    assert recording_bridge.calls[1][1]["parent_class_path"] == "/Script/Engine.DataAsset"


def test_editor_maintenance_tools_forward_payload(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.editor_save_all()
    internal_tools.editor_request_exit(save_before_exit=True, force=False)

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


def test_asset_management_write_tools_default_to_dry_run(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.asset_delete("/Game/Test/M_Delete.M_Delete")
    internal_tools.asset_move("/Game/Test/M_A.M_A", "/Game/Test/M_B.M_B")
    internal_tools.asset_rename("/Game/Test/M_B.M_B", "/Game/Test/M_C.M_C")
    internal_tools.asset_move_batch(
        [
            {
                "source_asset_path": "/Game/Test/M_C.M_C",
                "destination_asset_path": "/Game/Test/M_D.M_D",
            }
        ],
        dry_run=False,
        continue_on_error=True,
    )
    internal_tools.asset_rename_batch(
        [
            {
                "source_asset_path": "/Game/Test/M_D.M_D",
                "destination_asset_path": "/Game/Test/M_E.M_E",
            }
        ]
    )
    internal_tools.asset_duplicate("/Game/Test/M_C.M_C", "/Game/Test/M_D.M_D")
    internal_tools.folder_create("/Game/Test/NewFolder")
    internal_tools.folder_delete("/Game/Test/NewFolder", dry_run=False)
    internal_tools.asset_redirectors_fixup("/Game/Test")

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
