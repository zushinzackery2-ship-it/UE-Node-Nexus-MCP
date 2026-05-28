from __future__ import annotations

from typing import Any, get_args, get_type_hints

from ue_node_nexus_mcp import runtime, tools_niagara, tools_niagara_modules
from ue_node_nexus_mcp import tools_niagara_properties

from tests import internal_tools
from tests.helpers import RecordingBridge


def test_niagara_dense_list_tools_keep_indexed_defaults(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.niagara_modules_list("/Game/FX/NS_Test.NS_Test")
    internal_tools.niagara_user_params_get("/Game/FX/NS_Test.NS_Test")
    internal_tools.niagara_system_properties_get("/Game/FX/NS_Test.NS_Test")
    internal_tools.niagara_renderer_properties_get("/Game/FX/NS_Test.NS_Test", 0, 0)
    internal_tools.niagara_renderers_list("/Game/FX/NS_Test.NS_Test")
    internal_tools.niagara_materials_get("/Game/FX/NS_Test.NS_Test")
    internal_tools.niagara_emitters_list("/Game/FX/NS_Test.NS_Test")
    internal_tools.niagara_system_summary_get("/Game/FX/NS_Test.NS_Test")
    internal_tools.niagara_asset_lint("/Game/FX/NS_Test.NS_Test")

    assert recording_bridge.calls == [
        (
            "niagara_modules_list",
            {
                "asset_path": "/Game/FX/NS_Test.NS_Test",
                "emitter_index": None,
                "usage": None,
                "format": "indexed",
                "include_script_paths": False,
            },
        ),
        ("niagara_user_params_get", {"asset_path": "/Game/FX/NS_Test.NS_Test", "format": "indexed"}),
        (
            "niagara_system_properties_get",
            {
                "asset_path": "/Game/FX/NS_Test.NS_Test",
                "property_names": None,
                "include_non_editable": False,
                "format": "indexed",
                "include_types": False,
            },
        ),
        (
            "niagara_renderer_properties_get",
            {
                "asset_path": "/Game/FX/NS_Test.NS_Test",
                "emitter_index": 0,
                "renderer_index": 0,
                "property_names": None,
                "include_non_editable": False,
                "format": "indexed",
                "include_types": False,
            },
        ),
        ("niagara_renderers_list", {"asset_path": "/Game/FX/NS_Test.NS_Test", "format": "indexed"}),
        ("niagara_materials_get", {"asset_path": "/Game/FX/NS_Test.NS_Test", "format": "indexed"}),
        ("niagara_emitters_list", {"asset_path": "/Game/FX/NS_Test.NS_Test", "format": "indexed"}),
        ("niagara_system_summary_get", {"asset_path": "/Game/FX/NS_Test.NS_Test", "format": "indexed"}),
        ("niagara_asset_lint", {"asset_path": "/Game/FX/NS_Test.NS_Test", "format": "indexed"}),
    ]


def test_niagara_dense_list_tools_forward_new_formats(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.niagara_modules_list("/Game/FX/NS_Test.NS_Test", format="indexed")
    internal_tools.niagara_modules_list("/Game/FX/NS_Test.NS_Test", format="tiny")
    internal_tools.niagara_modules_list("/Game/FX/NS_Test.NS_Test", include_script_paths=True)
    internal_tools.niagara_user_params_get("/Game/FX/NS_Test.NS_Test", format="indexed")
    internal_tools.niagara_user_params_get("/Game/FX/NS_Test.NS_Test", format="tiny")
    internal_tools.niagara_system_properties_get("/Game/FX/NS_Test.NS_Test", format="indexed")
    internal_tools.niagara_system_properties_get("/Game/FX/NS_Test.NS_Test", include_types=True)
    internal_tools.niagara_system_properties_get("/Game/FX/NS_Test.NS_Test", format="tiny")
    internal_tools.niagara_renderer_properties_get("/Game/FX/NS_Test.NS_Test", 0, 0, format="indexed")
    internal_tools.niagara_renderer_properties_get("/Game/FX/NS_Test.NS_Test", 0, 0, include_types=True)
    internal_tools.niagara_renderer_properties_get("/Game/FX/NS_Test.NS_Test", 0, 0, format="tiny")
    internal_tools.niagara_renderers_list("/Game/FX/NS_Test.NS_Test", format="indexed")
    internal_tools.niagara_renderers_list("/Game/FX/NS_Test.NS_Test", format="tiny")
    internal_tools.niagara_materials_get("/Game/FX/NS_Test.NS_Test", format="indexed")
    internal_tools.niagara_materials_get("/Game/FX/NS_Test.NS_Test", format="tiny")
    internal_tools.niagara_emitters_list("/Game/FX/NS_Test.NS_Test", format="indexed")
    internal_tools.niagara_emitters_list("/Game/FX/NS_Test.NS_Test", format="tiny")
    internal_tools.niagara_system_summary_get("/Game/FX/NS_Test.NS_Test", format="indexed")
    internal_tools.niagara_system_summary_get("/Game/FX/NS_Test.NS_Test", format="tiny")

    assert [payload["format"] for _, payload in recording_bridge.calls] == [
        "indexed",
        "tiny",
        "indexed",
        "indexed",
        "tiny",
        "indexed",
        "indexed",
        "tiny",
        "indexed",
        "indexed",
        "tiny",
        "indexed",
        "tiny",
        "indexed",
        "tiny",
        "indexed",
        "tiny",
        "indexed",
        "tiny",
    ]
    assert recording_bridge.calls[2][1]["include_script_paths"] is True
    assert recording_bridge.calls[6][1]["include_types"] is True
    assert recording_bridge.calls[9][1]["include_types"] is True


def test_niagara_dense_format_signatures_expose_new_formats() -> None:
    module_format = get_type_hints(tools_niagara_modules.niagara_modules_list)["format"]
    user_param_format = get_type_hints(tools_niagara.niagara_user_params_get)["format"]
    system_property_format = get_type_hints(tools_niagara.niagara_system_properties_get)["format"]
    renderer_property_format = get_type_hints(tools_niagara.niagara_renderer_properties_get)["format"]
    renderer_format = get_type_hints(tools_niagara.niagara_renderers_list)["format"]
    material_format = get_type_hints(tools_niagara.niagara_materials_get)["format"]
    emitter_format = get_type_hints(tools_niagara.niagara_emitters_list)["format"]
    summary_format = get_type_hints(tools_niagara.niagara_system_summary_get)["format"]

    expected = ("compact", "full", "indexed", "tiny")
    assert get_args(module_format) == expected
    assert get_args(user_param_format) == expected
    assert get_args(system_property_format) == expected
    assert get_args(renderer_property_format) == expected
    assert get_args(renderer_format) == expected
    assert get_args(material_format) == expected
    assert get_args(emitter_format) == expected
    assert get_args(summary_format) == expected


def test_niagara_property_set_tools_keep_summary_defaults(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    params = [{"name": "WarmupTickCount", "value_text": "4"}]
    internal_tools.niagara_system_properties_set("/Game/FX/NS_Test.NS_Test", params)
    internal_tools.niagara_renderer_properties_set("/Game/FX/NS_Test.NS_Test", 0, 0, params)

    assert recording_bridge.calls == [
        (
            "niagara_system_properties_set",
            {
                "asset_path": "/Game/FX/NS_Test.NS_Test",
                "params": params,
                "dry_run": True,
                "allow_non_editable": False,
                "save": False,
                "format": "summary",
            },
        ),
        (
            "niagara_renderer_properties_set",
            {
                "asset_path": "/Game/FX/NS_Test.NS_Test",
                "emitter_index": 0,
                "renderer_index": 0,
                "params": params,
                "dry_run": True,
                "allow_non_editable": False,
                "save": False,
                "format": "summary",
            },
        ),
    ]


def test_niagara_module_input_tools_forward_payloads(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    params = [{"name": "SpriteSize", "value_text": "X=34.000 Y=86.000"}]
    internal_tools.niagara_module_inputs_get("/Game/FX/NS_Test.NS_Test", 0, "ParticleSpawnScript", 2)
    internal_tools.niagara_module_inputs_set("/Game/FX/NS_Test.NS_Test", 0, "ParticleSpawnScript", 2, params, dry_run=False, save=True)

    assert recording_bridge.calls == [
        (
            "niagara_module_inputs_get",
            {
                "asset_path": "/Game/FX/NS_Test.NS_Test",
                "emitter_index": 0,
                "usage": "ParticleSpawnScript",
                "module_index": 2,
            },
        ),
        (
            "niagara_module_inputs_set",
            {
                "asset_path": "/Game/FX/NS_Test.NS_Test",
                "emitter_index": 0,
                "usage": "ParticleSpawnScript",
                "module_index": 2,
                "params": params,
                "dry_run": False,
                "save": True,
            },
        ),
    ]


def test_niagara_asset_lint_forwards_payload(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.niagara_asset_lint("/Game/FX/NS_Test.NS_Test")

    assert recording_bridge.calls == [
        ("niagara_asset_lint", {"asset_path": "/Game/FX/NS_Test.NS_Test", "format": "indexed"}),
    ]


def test_niagara_property_set_tools_forward_full_format(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    params = [{"name": "WarmupTickCount", "value_text": "4"}]
    internal_tools.niagara_system_properties_set("/Game/FX/NS_Test.NS_Test", params, format="full")
    internal_tools.niagara_renderer_properties_set("/Game/FX/NS_Test.NS_Test", 0, 0, params, format="full")

    assert [payload["format"] for _, payload in recording_bridge.calls] == ["full", "full"]


def test_niagara_create_tools_forward_summary_and_full_formats(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.niagara_system_create("/Game/FX/NS_New.NS_New")
    internal_tools.niagara_system_create("/Game/FX/NS_NewFull.NS_NewFull", format="full")
    internal_tools.niagara_system_duplicate("/Game/FX/NS_Source.NS_Source", "/Game/FX/NS_Copy.NS_Copy")
    internal_tools.niagara_system_duplicate("/Game/FX/NS_Source.NS_Source", "/Game/FX/NS_CopyFull.NS_CopyFull", format="full")

    assert [payload["format"] for _, payload in recording_bridge.calls] == ["summary", "full", "summary", "full"]


def test_niagara_property_set_signatures_expose_summary_and_full() -> None:
    system_format = get_type_hints(tools_niagara_properties.niagara_system_properties_set)["format"]
    renderer_format = get_type_hints(tools_niagara_properties.niagara_renderer_properties_set)["format"]

    assert get_args(system_format) == ("summary", "full")
    assert get_args(renderer_format) == ("summary", "full")


def test_niagara_create_signatures_expose_summary_and_full() -> None:
    create_format = get_type_hints(tools_niagara.niagara_system_create)["format"]
    duplicate_format = get_type_hints(tools_niagara.niagara_system_duplicate)["format"]

    assert get_args(create_format) == ("summary", "full")
    assert get_args(duplicate_format) == ("summary", "full")


def test_niagara_wrappers_strip_verbose_boundary_fields(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    verbose_data = {
        "asset_path": "/Game/FX/NS_Test.NS_Test",
        "capabilities": {"create_empty_system": True},
        "limitations": ["old verbose field"],
        "recommended_generic_workflow": "old verbose field",
        "emitter_count": 1,
    }
    recording_bridge.response = {
        "ok": True,
        "operation": "",
        "data": dict(verbose_data),
        "diagnostics": [],
        "warnings": [],
    }
    summary = internal_tools.niagara_system_summary_get("/Game/FX/NS_Test.NS_Test")

    recording_bridge.response = {
        "ok": True,
        "operation": "",
        "data": dict(verbose_data),
        "diagnostics": [],
        "warnings": [],
    }
    modules = internal_tools.niagara_modules_list("/Game/FX/NS_Test.NS_Test")

    for response in (summary, modules):
        assert "capabilities" not in response["data"]
        assert "limitations" not in response["data"]
        assert "recommended_generic_workflow" not in response["data"]
        assert response["data"]["emitter_count"] == 1
