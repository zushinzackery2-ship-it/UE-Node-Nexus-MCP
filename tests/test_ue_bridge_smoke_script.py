from __future__ import annotations

from pathlib import Path
from typing import Any

from scripts import ue_auto_index_matrix
from scripts import ue_asset_recreate_matrix
from scripts import ue_bridge_smoke
from scripts import ue_blueprint_write_matrix
from scripts import ue_material_function_node_matrix
from scripts import ue_material_property_matrix
from scripts import ue_material_attribute_link_matrix
from scripts import update_plugin_and_smoke_ue55


def test_package_path_to_disk_path_maps_game_content(tmp_path: Path) -> None:
    project = tmp_path / "Sample.uproject"
    expected = tmp_path / "Content" / "MCPWriteSmoke" / "Run_001"

    assert ue_bridge_smoke.package_path_to_disk_path(project, "/Game/MCPWriteSmoke/Run_001") == expected


def test_package_path_to_disk_path_rejects_non_game_path(tmp_path: Path) -> None:
    project = tmp_path / "Sample.uproject"

    try:
        ue_bridge_smoke.package_path_to_disk_path(project, "/Engine/BasicShapes/Cube")
    except ValueError as exc:
        assert "Only /Game package paths" in str(exc)
    else:
        raise AssertionError("non-/Game package path should fail")


def test_build_asset_matrix_uses_unique_root_and_required_payloads() -> None:
    assets = ue_bridge_smoke.build_asset_matrix("/Game/SmokeRoot/", "abc")
    paths = [asset.asset_path for asset in assets]
    kinds = [asset.payload["asset_kind"] for asset in assets]

    assert paths == [
        "/Game/SmokeRoot/Run_abc/MF_Smoke",
        "/Game/SmokeRoot/Run_abc/DA_Smoke",
        "/Game/SmokeRoot/Run_abc/RT_Smoke",
        "/Game/SmokeRoot/Run_abc/BP_Smoke",
    ]
    assert kinds == ["material_function", "data_asset", "texture_render_target_2d", "blueprint"]
    assert all(asset.payload["dry_run"] is False for asset in assets)
    assert all(asset.payload["save"] is True for asset in assets)


def test_collect_disk_residue_returns_files_under_package_root(tmp_path: Path) -> None:
    project = tmp_path / "Sample.uproject"
    content_dir = tmp_path / "Content" / "SmokeRoot"
    nested_file = content_dir / "Nested" / "Asset.uasset"
    nested_file.parent.mkdir(parents=True)
    nested_file.write_text("x", encoding="utf-8")

    residue = ue_bridge_smoke.collect_disk_residue(project, "/Game/SmokeRoot")

    assert residue == [str(nested_file)]


def test_parse_args_defaults_to_read_only_mode() -> None:
    args = ue_bridge_smoke.parse_args([])

    assert args.bridge_url == ue_bridge_smoke.DEFAULT_BRIDGE_URL
    assert args.write is False
    assert args.material_property_matrix is False
    assert args.launch_editor is False


def test_parse_args_accepts_material_property_matrix() -> None:
    args = ue_bridge_smoke.parse_args(["--material-property-matrix"])

    assert args.material_property_matrix is True


def test_parse_args_accepts_material_attribute_link_matrix() -> None:
    args = ue_bridge_smoke.parse_args(["--material-attribute-link-matrix"])

    assert args.material_attribute_link_matrix is True


def test_parse_args_accepts_material_function_node_matrix() -> None:
    args = ue_bridge_smoke.parse_args(["--material-function-node-matrix"])

    assert args.material_function_node_matrix is True


def test_parse_args_accepts_level_material_matrix() -> None:
    args = ue_bridge_smoke.parse_args(["--level-material-matrix"])

    assert args.level_material_matrix is True


def test_parse_args_accepts_blueprint_write_matrix() -> None:
    args = ue_bridge_smoke.parse_args(["--blueprint-write-matrix"])

    assert args.blueprint_write_matrix is True


def test_parse_args_accepts_auto_index_matrix() -> None:
    args = ue_bridge_smoke.parse_args(["--auto-index-matrix"])

    assert args.auto_index_matrix is True


def test_parse_args_accepts_asset_recreate_matrix() -> None:
    args = ue_bridge_smoke.parse_args(["--asset-recreate-matrix"])

    assert args.asset_recreate_matrix is True


def test_auto_index_object_path_uses_asset_name() -> None:
    assert ue_auto_index_matrix.object_path("/Game/Test/M_Item") == "/Game/Test/M_Item.M_Item"


def test_asset_recreate_object_path_uses_asset_name() -> None:
    assert ue_asset_recreate_matrix.object_path("/Game/Test/M_Item") == "/Game/Test/M_Item.M_Item"


def test_build_auto_index_paths_are_run_scoped() -> None:
    paths = ue_auto_index_matrix.build_auto_index_paths("/Game/AutoIndex/", "abc")

    assert paths.root == "/Game/AutoIndex"
    assert paths.run_folder == "/Game/AutoIndex/Run_abc"
    assert paths.source_asset == "/Game/AutoIndex/Run_abc/M_AutoIndex"
    assert paths.renamed_asset == "/Game/AutoIndex/Run_abc/M_AutoIndexRenamed"
    assert paths.moved_asset == "/Game/AutoIndex/Run_abc/Moved/M_AutoIndexMoved"


class RecordingBridge:
    def __init__(self) -> None:
        self.calls: list[tuple[str, dict[str, Any]]] = []

    def call(self, operation: str, payload: dict[str, Any]) -> dict[str, Any]:
        self.calls.append((operation, payload))
        return {"ok": True, "data": {"text": "", "count": 0, "total": 0}}


def test_auto_index_query_uses_indexed_package_scoped_payload() -> None:
    client = RecordingBridge()

    ue_auto_index_matrix.query_asset(client, "/Game/Root", "Hero", limit=7)

    assert client.calls == [
        (
            "auto_index_query",
            {
                "text": "Hero",
                "class_names": [],
                "package_paths": ["/Game/Root"],
                "recursive": True,
                "include_redirectors": False,
                "limit": 7,
                "cursor": None,
                "format": "indexed",
            },
        )
    ]


def test_auto_index_diff_clean_rejects_drift() -> None:
    try:
        ue_auto_index_matrix.assert_diff_clean({"data": {"indexed_only": 1, "registry_only": 0}})
    except RuntimeError as exc:
        assert "reported drift" in str(exc)
    else:
        raise AssertionError("drift should fail")


def test_auto_index_not_resolved_ignores_input_echo() -> None:
    ue_auto_index_matrix.assert_not_resolved_to(
        {
            "data": {
                "resolved_asset_path": "",
                "text": "R:input=/Game/Test/M_Old.M_Old|exact=0|candidates=0|returned=0\nC:\nA:\n",
            }
        },
        "/Game/Test/M_Old.M_Old",
        "old path",
    )


def test_update_plugin_parse_args_splits_build_and_bridge_timeouts() -> None:
    args = update_plugin_and_smoke_ue55.parse_args(["--skip-install", "--build-timeout", "120"])

    assert args.timeout == 10.0
    assert args.build_timeout == 120.0
    assert args.skip_install is True


def test_blueprint_matrix_object_path_uses_asset_name() -> None:
    assert ue_blueprint_write_matrix.object_path("/Game/Test/BP_Item") == "/Game/Test/BP_Item.BP_Item"


def test_blueprint_matrix_find_pin_by_name_and_direction() -> None:
    response = {
        "data": {
            "pins": [
                {"direction": "input", "name": "execute", "pin_id": "in"},
                {"direction": "output", "name": "then", "pin_id": "out"},
            ]
        }
    }

    assert ue_blueprint_write_matrix.find_pin(response, "output", {"then"}) == "out"


def test_blueprint_matrix_readback_accepts_returned_node_count() -> None:
    response = {"data": {"returned_nodes": 2, "text": "N:0:0:Event;1:1:Branch\n"}}

    assert ue_blueprint_write_matrix.graph_readback_node_count(response) == 2


def test_material_property_schema_requires_kind_type_and_default() -> None:
    params = {
        "R": {"name": "R", "kind": "number", "type": "float", "default_value": "0.0", "editable": True},
        "Mode": {
            "name": "Mode",
            "kind": "enum",
            "type": "TEnumAsByte<EMode>",
            "default_value": "A",
            "editable": True,
            "enum_values": [{"name": "A", "value": 0}],
        },
    }

    ue_material_property_matrix.assert_schema_contains(params, {"number", "enum"}, "test")


def test_material_property_schema_rejects_missing_enum_values() -> None:
    params = {
        "Mode": {
            "name": "Mode",
            "kind": "enum",
            "type": "TEnumAsByte<EMode>",
            "default_value": "A",
            "editable": True,
        },
    }

    try:
        ue_material_property_matrix.assert_schema_contains(params, {"enum"}, "test")
    except RuntimeError as exc:
        assert "enum schema missing values" in str(exc)
    else:
        raise AssertionError("missing enum_values should fail")


def test_material_property_readback_accepts_numeric_bool_and_object_text() -> None:
    params = {
        "R": {"value": "0.750000"},
        "G": {"value": "True"},
        "Texture": {"value": "/Engine/EngineResources/DefaultTexture.DefaultTexture"},
    }

    ue_material_property_matrix.assert_writes_read_back(
        params,
        {"R": 0.75, "G": True, "Texture": "/Engine/EngineResources/DefaultTexture.DefaultTexture"},
        "test",
    )


def test_material_attribute_call_expect_bridge_failure_accepts_structured_error() -> None:
    class FailingBridge:
        last_http_response = (
            '{"ok":false,"diagnostics":[{"code":"material_pin_type_mismatch"}],"data":{"changed":false}}'
        )

        def call(self, operation: str, payload: dict[str, Any]) -> dict[str, Any]:
            raise RuntimeError("graph_patch_apply returned ok=false")

    response = ue_material_attribute_link_matrix.call_expect_bridge_failure(
        FailingBridge(),
        "graph_patch_apply",
        {},
        "material_pin_type_mismatch",
    )

    assert response["ok"] is False


def test_material_attribute_integrity_rejects_enabled_ok_state() -> None:
    try:
        ue_material_attribute_link_matrix.assert_integrity_reason(
            {"data": {"post_checks": {"pin_integrity": {"ok": True, "broken_links": []}}}},
            "bUseMaterialAttributes_false",
        )
    except RuntimeError as exc:
        assert "expected ok=false" in str(exc)
    else:
        raise AssertionError("pin integrity ok=true should fail")


def test_material_function_node_object_path_uses_asset_name() -> None:
    assert ue_material_function_node_matrix.object_path("/Game/Test/MF_Item") == "/Game/Test/MF_Item.MF_Item"
