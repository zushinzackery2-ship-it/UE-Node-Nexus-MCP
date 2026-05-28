from __future__ import annotations

import asyncio
from pathlib import Path
from typing import Any

from ue_node_nexus_mcp import runtime, server
from ue_node_nexus_mcp.contracts import BRIDGE_OPERATIONS, DEFAULT_HIDDEN_OPERATIONS, OPERATION_FEATURES, THIN_EXPOSED_OPERATIONS

from tests import internal_tools
from tests.helpers import RecordingBridge


def _enabled_bridge_operations() -> set[str]:
    return {
        operation
        for operation, feature in OPERATION_FEATURES.items()
        if operation in BRIDGE_OPERATIONS and feature in runtime.enabled_features()
    }


def _default_exposed_bridge_operations() -> set[str]:
    return _enabled_bridge_operations() - DEFAULT_HIDDEN_OPERATIONS




def test_write_parameter_tools_request_compile_by_default(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.node_params_set(
        asset_path="/Game/Materials/M_Example.M_Example",
        node_id="node-a",
        params={"default_value": 0.5},
        dry_run=False,
    )
    internal_tools.material_instance_params_set(
        asset_path="/Game/Materials/MI_Example.MI_Example",
        params=[{"type": "scalar", "name": "Roughness", "value": 0.8}],
        dry_run=False,
    )

    assert recording_bridge.calls[0][1]["compile_after"] is True
    assert recording_bridge.calls[1][1]["compile_after"] is True


def test_read_list_tools_default_to_indexed_where_available(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    response = internal_tools.asset_list()
    internal_tools.level_actors_list()
    internal_tools.level_mesh_instances_list()
    internal_tools.material_expression_classes_list()
    internal_tools.material_instance_params_get("/Game/Materials/MI_Example.MI_Example")

    assert "remaining_errors" in response
    assert isinstance(response["remaining_errors"], int)
    assert recording_bridge.calls[0][1]["format"] == "indexed"
    assert recording_bridge.calls[1][1]["format"] == "indexed"
    assert recording_bridge.calls[2][1]["format"] == "indexed"
    assert recording_bridge.calls[3][1]["format"] == "indexed"
    assert recording_bridge.calls[3][1]["include_params"] is False
    assert recording_bridge.calls[4][1]["format"] == "compact"


def test_remaining_errors_counts_current_response_errors(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    recording_bridge.response = {
        "ok": False,
        "operation": "asset_list",
        "error": {"code": "asset_failed", "message": "failed", "details": {}},
        "diagnostics": [
            {"severity": "warning", "code": "warn"},
            {"severity": "error", "code": "err"},
            {"severity": "fatal", "code": "fatal"},
        ],
        "warnings": [],
        "data": {"remaining_errors": 99, "post_checks": {"compile": {"error_count": 3}}},
    }
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    response = internal_tools.asset_list()

    assert response["remaining_errors"] == 6
    assert "remaining_errors" not in response["data"]

    recording_bridge.response = {
        "ok": True,
        "operation": "asset_list",
        "data": {},
        "diagnostics": [{"severity": "warning", "code": "warn"}],
        "warnings": [],
    }
    assert internal_tools.asset_list()["remaining_errors"] == 0

    recording_bridge.response = {
        "ok": False,
        "operation": "asset_list",
        "data": {},
        "diagnostics": [],
        "warnings": [],
    }
    assert internal_tools.asset_list()["remaining_errors"] == 1
    assert list(internal_tools.asset_list())[-1] == "remaining_errors"


def test_default_mcp_tool_surface_excludes_hidden_tools() -> None:
    registered = set(runtime.mcp._tool_manager._tools)

    assert registered == THIN_EXPOSED_OPERATIONS
    assert registered.isdisjoint(DEFAULT_HIDDEN_OPERATIONS)
    assert "niagara_system_duplicate" not in registered
    assert "niagara_template_duplicate" not in registered


def test_public_mcp_list_tools_excludes_hidden_tools() -> None:
    tools = asyncio.run(runtime.mcp.list_tools())
    registered = {tool.name for tool in tools}

    assert registered == THIN_EXPOSED_OPERATIONS
    assert registered.isdisjoint(DEFAULT_HIDDEN_OPERATIONS)


def test_hidden_tools_are_hidden_from_mcp_but_remain_local_functions(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    assert "auto_index_clear" not in runtime.mcp._tool_manager._tools
    assert "editor_save_all" not in runtime.mcp._tool_manager._tools
    assert "editor_request_exit" not in runtime.mcp._tool_manager._tools
    response = internal_tools.auto_index_clear(delete_file=True)
    internal_tools.editor_save_all(save_map_packages=True, save_content_packages=True)
    internal_tools.editor_request_exit(save_before_exit=True, force=False)

    assert response["ok"] is True
    assert recording_bridge.calls == [
        ("auto_index_clear", {"delete_file": True}),
        ("editor_save_all", {"save_map_packages": True, "save_content_packages": True}),
        ("editor_request_exit", {"save_before_exit": True, "force": False}),
    ]


def test_auto_index_tools_forward_indexed_payloads(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.auto_index_enable(root_path="/Game", rebuild=True)
    internal_tools.auto_index_overview()
    internal_tools.auto_index_tree_get(root_path="/Game", depth=2)
    internal_tools.auto_index_query(text="hero", class_names=["Blueprint"], package_paths=["/Game/Characters"])
    internal_tools.auto_index_resolve_path("BP_Hero")

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
    ]


def test_project_context_get_forwards_empty_payload(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.project_context_get()

    assert recording_bridge.calls == [("project_context_get", {})]


def test_bridge_capabilities_get_forwards_empty_payload(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.bridge_capabilities_get()

    assert recording_bridge.calls == [("bridge_capabilities_get", {})]


def test_bridge_capabilities_reports_official_niagara_plugin_state() -> None:
    source = Path(
        "Plugins/UeNodeNexusBridge/Source/UeNodeNexusBridge/Private/Core/"
        "UeNodeNexusBridgeCapabilitiesOps.cpp"
    ).read_text(encoding="utf-8")

    assert 'FindPlugin(TEXT("Niagara"))' in source
    assert 'SetBoolField(TEXT("niagara_plugin_enabled")' in source
    assert 'SetBoolField(TEXT("niagara_bridge_plugin_enabled")' in source
    assert 'SetBoolField(TEXT("niagara_available")' in source


def test_bridge_contract_check_forwards_expected_operations(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.bridge_contract_check()
    internal_tools.bridge_contract_check(mode="exposed")
    internal_tools.bridge_contract_check(mode="all")

    enabled_contract_operations = _enabled_bridge_operations()
    contract_calls = [
        call
        for call in recording_bridge.calls
        if "expected_operations" in call[1]
    ]

    assert contract_calls[0] == (
        "bridge_capabilities_get",
        {
            "expected_operations": sorted(enabled_contract_operations),
            "allowed_extra_operations": sorted(set(BRIDGE_OPERATIONS) - enabled_contract_operations),
            "mode": "enabled",
        },
    )
    assert contract_calls[1] == (
        "bridge_capabilities_get",
        {
            "expected_operations": sorted(_default_exposed_bridge_operations()),
            "allowed_extra_operations": sorted(set(BRIDGE_OPERATIONS) - _default_exposed_bridge_operations()),
            "mode": "exposed",
        },
    )
    assert contract_calls[2] == (
        "bridge_capabilities_get",
        {
            "expected_operations": sorted(BRIDGE_OPERATIONS),
            "allowed_extra_operations": [],
            "mode": "all",
        },
    )


def test_hidden_auto_index_functions_remain_callable(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    internal_tools.auto_index_disable()
    internal_tools.auto_index_flush()
    internal_tools.auto_index_clear(delete_file=True)
    internal_tools.auto_index_diff_registry()

    assert recording_bridge.calls == [
        ("auto_index_disable", {}),
        ("auto_index_flush", {}),
        ("auto_index_clear", {"delete_file": True}),
        ("auto_index_diff_registry", {"format": "indexed"}),
    ]
