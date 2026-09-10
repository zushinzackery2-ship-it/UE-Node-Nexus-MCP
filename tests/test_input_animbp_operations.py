"""Facade and wrapper tests for Enhanced Input and AnimBP state machine writes."""

from __future__ import annotations

from typing import Callable

import pytest

from ue_node_nexus_mcp import tools_blueprints, tools_project
from ue_node_nexus_mcp.operation_registry import get_operation_spec
from ue_node_nexus_mcp.tools_facade import ue_execute, ue_read

from tests.support.bridges import RecordingBridge

WRITE_OK = {"ok": True, "data": {"applied": False, "dry_run": True}, "diagnostics": [], "warnings": []}


def test_new_operations_carry_expected_metadata() -> None:
    assert get_operation_spec("input_action_create").kind == "write"
    assert get_operation_spec("input_action_create").group == "project_input"
    assert get_operation_spec("input_mapping_context_entry_add").default_response == "delta"
    assert get_operation_spec("input_mapping_context_get").kind == "read"
    assert get_operation_spec("anim_state_machine_state_add").group == "blueprint"
    assert get_operation_spec("anim_state_machine_transition_add").risk == "medium"


def test_input_writes_pass_through_ue_execute(all_features: None, use_bridge: Callable) -> None:
    bridge = use_bridge(RecordingBridge(WRITE_OK))
    result = ue_execute(
        "input_mapping_context_entry_add",
        {"asset_path": "/Game/Input/IMC_Default", "action_path": "/Game/Input/IA_Jump", "key": "SpaceBar"},
    )
    assert result["ok"] is True
    operation, payload = bridge.calls[0]
    assert operation == "input_mapping_context_entry_add"
    assert payload["action_path"] == "/Game/Input/IA_Jump"
    assert payload["key"] == "SpaceBar"


def test_input_action_create_wrapper_defaults_to_safe_dry_run(all_features: None, use_bridge: Callable) -> None:
    bridge = use_bridge(RecordingBridge(WRITE_OK))
    result = tools_project.input_action_create("/Game/Input/IA_Jump")
    assert result["ok"] is True
    operation, payload = bridge.calls[0]
    assert operation == "input_action_create"
    assert payload == {
        "asset_path": "/Game/Input/IA_Jump",
        "value_type": "bool",
        "dry_run": True,
        "save": False,
    }


def test_mapping_context_reads_through_ue_read(all_features: None, use_bridge: Callable) -> None:
    bridge = use_bridge(
        RecordingBridge(
            {
                "ok": True,
                "data": {"mappings": [["/Game/Input/IA_Jump", "SpaceBar", 0, 0]], "count": 1},
                "diagnostics": [],
                "warnings": [],
            }
        )
    )
    result = ue_read(target="input_mapping_context", asset_path="/Game/Input/IMC_Default")
    assert result["ok"] is True
    assert result["data"]["operation"] == "input_mapping_context_get"
    assert bridge.calls[0][0] == "input_mapping_context_get"
    assert bridge.calls[0][1]["asset_path"] == "/Game/Input/IMC_Default"


def test_state_add_wrapper_omits_machine_name_when_not_given(all_features: None, use_bridge: Callable) -> None:
    bridge = use_bridge(RecordingBridge(WRITE_OK))
    result = tools_blueprints.anim_state_machine_state_add(
        "/Game/Anim/ABP_Hero", "Run", set_as_entry=True
    )
    assert result["ok"] is True
    operation, payload = bridge.calls[0]
    assert operation == "anim_state_machine_state_add"
    assert payload["state_name"] == "Run"
    assert payload["set_as_entry"] is True
    assert payload["dry_run"] is True
    assert "machine_name" not in payload

    tools_blueprints.anim_state_machine_state_add("/Game/Anim/ABP_Hero", "Idle", machine_name="Locomotion")
    assert bridge.calls[1][1]["machine_name"] == "Locomotion"


def test_transition_add_wrapper_sends_both_states(all_features: None, use_bridge: Callable) -> None:
    bridge = use_bridge(RecordingBridge(WRITE_OK))
    result = tools_blueprints.anim_state_machine_transition_add("/Game/Anim/ABP_Hero", "Idle", "Run")
    assert result["ok"] is True
    payload = bridge.calls[0][1]
    assert payload["from_state"] == "Idle"
    assert payload["to_state"] == "Run"
    assert payload["dry_run"] is True


@pytest.mark.parametrize(
    "invoke",
    [
        lambda: tools_project.input_action_create("   "),
        lambda: tools_project.input_mapping_context_entry_add("/Game/I", "/Game/A", ""),
        lambda: tools_blueprints.anim_state_machine_state_add("/Game/A", ""),
        lambda: tools_blueprints.anim_state_machine_transition_add("/Game/A", "Idle", " "),
    ],
)
def test_wrappers_reject_bad_arguments_before_the_bridge(
    all_features: None, use_bridge: Callable, invoke: Callable
) -> None:
    bridge = use_bridge(RecordingBridge())
    with pytest.raises(ValueError):
        invoke()
    assert bridge.calls == []
