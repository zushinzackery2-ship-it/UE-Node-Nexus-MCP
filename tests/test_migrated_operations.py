"""Tests for the UnrealClaude-parity operations on the live facade path."""

from __future__ import annotations

from pathlib import Path
from typing import Callable

import pytest

from ue_node_nexus_mcp.operation_registry import get_operation_spec
from ue_node_nexus_mcp.tools_facade import ue_execute, ue_read

from tests.support.bridges import RecordingBridge


def test_new_operations_carry_expected_metadata() -> None:
    assert get_operation_spec("asset_dependencies_get").kind == "read"
    assert get_operation_spec("asset_referencers_get").group == "asset"
    assert get_operation_spec("level_open").risk == "high"
    assert get_operation_spec("level_actor_delete").risk == "high"
    assert get_operation_spec("level_actor_spawn").kind == "write"
    assert get_operation_spec("level_actor_transform_set").default_response == "delta"
    assert get_operation_spec("log_tail_get").local_mcp is True
    assert get_operation_spec("log_tail_get").bridge_operation is None


def test_actor_spawn_goes_to_bridge_with_typed_payload(all_features: None, use_bridge: Callable) -> None:
    bridge = use_bridge(RecordingBridge({"ok": True, "data": {"applied": False, "dry_run": True}, "diagnostics": [], "warnings": []}))
    result = ue_execute(
        "level_actor_spawn",
        {"class_path": "PointLight", "location": {"x": 0, "y": 0, "z": 500}, "dry_run": True},
    )
    assert result["ok"] is True
    operation, payload = bridge.calls[0]
    assert operation == "level_actor_spawn"
    assert payload["class_path"] == "PointLight"
    assert payload["location"] == {"x": 0, "y": 0, "z": 500}


def test_asset_dependencies_reads_through_ue_read(all_features: None, use_bridge: Callable) -> None:
    bridge = use_bridge(
        RecordingBridge(
            {
                "ok": True,
                "data": {"items": [["/Game/Textures/T_A", "hard"]], "count": 1, "has_more": False},
                "diagnostics": [],
                "warnings": [],
            }
        )
    )
    result = ue_read(target="asset_dependencies", asset_path="/Game/M.M")
    assert result["ok"] is True
    assert result["data"]["operation"] == "asset_dependencies_get"
    assert bridge.calls[0][0] == "asset_dependencies_get"
    assert bridge.calls[0][1]["asset_path"] == "/Game/M.M"

    ue_read(target="asset_referencers", asset_path="/Game/M.M")
    assert bridge.calls[1][0] == "asset_referencers_get"


def _project_logs(tmp_path: Path, monkeypatch) -> Path:
    from ue_node_nexus_mcp import tools_system
    from ue_node_nexus_mcp.instances.broker.client import BrokerClient
    from ue_node_nexus_mcp.instances.session.binding import EditorSession

    project = tmp_path / "Demo.uproject"
    project.touch()
    session = EditorSession(BrokerClient(tmp_path / "Runtime", str(tmp_path)), str(project))
    monkeypatch.setattr(tools_system, "instance_manager", session)
    return tmp_path / "Saved/Logs"


def test_log_tail_reads_newest_project_log(all_features: None, use_bridge: Callable, tmp_path: Path, monkeypatch) -> None:
    logs_dir = _project_logs(tmp_path, monkeypatch)
    logs_dir.mkdir(parents=True)
    (logs_dir / "Demo.log").write_text(
        "LogInit: startup\nLogTemp: Warning: sampler mismatch\nLogTemp: done\n", encoding="utf-8"
    )
    bridge = use_bridge(RecordingBridge())

    result = ue_execute("log_tail_get", {})
    assert result["ok"] is True
    data = result["data"]
    assert data["scanned_lines"] == 3
    assert data["text"].endswith("LogTemp: done")

    filtered = ue_execute("log_tail_get", {"match": "warning"})
    assert filtered["data"]["matched_lines"] == 1
    assert "sampler mismatch" in filtered["data"]["text"]
    assert not bridge.calls


def test_log_tail_via_ue_read_and_missing_log(all_features: None, use_bridge: Callable, tmp_path: Path, monkeypatch) -> None:
    logs_dir = _project_logs(tmp_path, monkeypatch)
    bridge = use_bridge(RecordingBridge())
    result = ue_read(target="log")
    assert result["ok"] is False  # no Logs dir yet -> log_not_found flows through ue_read

    logs_dir.mkdir(parents=True)
    (logs_dir / "Demo.log").write_text("only line\n", encoding="utf-8")
    ok_result = ue_read(target="log")
    assert ok_result["ok"] is True
    assert ok_result["data"]["operation"] == "log_tail_get"
    assert not bridge.calls


@pytest.mark.parametrize(
    "payload",
    [
        {"tail_kb": 0},
        {"tail_kb": "big"},
        {"max_lines": 0},
        {"match": "   "},
    ],
)
def test_log_tail_rejects_bad_arguments(all_features: None, use_bridge: Callable, payload: dict) -> None:
    use_bridge(RecordingBridge())
    result = ue_execute("log_tail_get", payload)
    assert result["ok"] is False
    assert result["error"]["code"] == "invalid_operation"


def test_level_open_defaults_to_dry_run(all_features: None, use_bridge: Callable) -> None:
    bridge = use_bridge(RecordingBridge({"ok": True, "data": {"dry_run": True, "applied": False}, "diagnostics": [], "warnings": []}))
    result = ue_execute("level_open", {"map_path": "/Game/Maps/Demo"})
    assert result["ok"] is True
    assert bridge.calls[0][1] == {"map_path": "/Game/Maps/Demo"}
