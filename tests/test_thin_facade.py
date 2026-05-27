from __future__ import annotations

from typing import Any

from ue_node_nexus_mcp import runtime, server
from ue_node_nexus_mcp.contracts import THIN_EXPOSED_OPERATIONS
from tests.helpers import RecordingBridge


def test_thin_context_reports_facade_tools() -> None:
    response = server.ue_context_get()

    assert response["ok"] is True
    assert set(response["data"]["facade_tools"]) == THIN_EXPOSED_OPERATIONS
    assert response["data"]["recommended_next"] == "ue_capability_get"


def test_thin_capability_index_lists_enabled_operations() -> None:
    response = server.ue_capability_get(group="graph")

    assert response["ok"] is True
    operations = {row[0] for row in response["data"]["operations"]}
    assert "graph_snapshot_get" in operations
    assert "node_params_set" in operations
    assert all(len(row) == 4 for row in response["data"]["operations"])


def test_thin_capability_schema_returns_one_operation() -> None:
    response = server.ue_capability_get(operation="node_params_set", detail="schema")

    assert response["ok"] is True
    assert response["data"]["operation"] == "node_params_set"
    assert response["data"]["group"] == "graph"
    assert response["data"]["default_response"] == "delta"


def test_thin_capability_rejects_unknown_operation() -> None:
    response = server.ue_capability_get(operation='python_exec')

    assert response['ok'] is False
    assert response['error']['code'] == 'invalid_operation'


def test_ue_execute_returns_delta_and_stores_diff(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    recording_bridge.response = {
        "ok": True,
        "operation": "node_params_set",
        "data": {"node_id": "n1"},
        "diagnostics": [],
        "warnings": [],
    }
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    response = server.ue_execute(
        operation="node_params_set",
        payload={"asset_path": "/Game/M.M", "node_id": "n1", "params": {"default_value": 1.0}, "dry_run": False},
    )

    assert response["ok"] is True
    assert recording_bridge.calls == [
        ("node_params_set", {"asset_path": "/Game/M.M", "node_id": "n1", "params": {"default_value": 1.0}, "dry_run": False})
    ]
    assert response["data"]["diff_token"].startswith("diff_")
    assert response["data"]["state_token"].startswith("state:/Game/M.M:")

    diff = server.ue_diff_get(since_token=response["data"]["diff_token"])
    assert diff["ok"] is True
    assert ["payload", "asset_path", "/Game/M.M"] in diff["data"]["changes"]


def test_ue_execute_rejects_unknown_operation() -> None:
    response = server.ue_execute(operation="python_exec", payload={})

    assert response["ok"] is False
    assert response["error"]["code"] == "invalid_operation"


def test_ue_read_stores_artifact(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    recording_bridge.response = {
        "ok": True,
        "operation": "graph_snapshot_get",
        "data": {"format": "wires_tiny", "asset_path": "/Game/M.M", "total_nodes": 3},
        "diagnostics": [],
        "warnings": [],
    }
    monkeypatch.setattr(runtime, "bridge", recording_bridge)

    response = server.ue_read(target="graph", asset_path="/Game/M.M", format="summary")

    assert response["ok"] is True
    assert recording_bridge.calls == [("graph_snapshot_get", {"asset_path": "/Game/M.M", "format": "wires_tiny"})]
    artifact_id = response["data"]["artifact"]["id"]
    artifact = server.ue_read(target="artifact", query={"artifact_id": artifact_id})
    assert artifact["ok"] is True
    assert artifact["data"]["data"]["format"] == "wires_tiny"


def test_ue_read_exposes_usable_diff_token(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    recording_bridge.response = {
        'ok': True,
        'operation': 'graph_snapshot_get',
        'data': {'format': 'wires_tiny', 'asset_path': '/Game/M.M', 'total_nodes': 3},
        'diagnostics': [],
        'warnings': [],
    }
    monkeypatch.setattr(runtime, 'bridge', recording_bridge)

    response = server.ue_read(target='graph', asset_path='/Game/M.M', format='summary')

    assert response['ok'] is True
    diff = server.ue_diff_get(since_token=response['data']['diff_token'])
    assert diff['ok'] is True
    assert ['operation', 'graph_snapshot_get'] in diff['data']['changes']


def test_ue_read_rejects_non_mapping_query() -> None:
    response = server.ue_read(target='graph', query='bad')  # type: ignore[arg-type]

    assert response['ok'] is False
    assert response['error']['code'] == 'invalid_query'


def test_ue_execute_rejects_invalid_response_mode(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    monkeypatch.setattr(runtime, 'bridge', recording_bridge)

    response = server.ue_execute(
        operation='node_params_set',
        payload={'asset_path': '/Game/M.M', 'node_id': 'n1'},
        response={'mode': 'not_a_mode'},
    )

    assert response['ok'] is False
    assert response['error']['code'] == 'invalid_response_mode'
    assert recording_bridge.calls == []


def test_ue_diff_get_rejects_invalid_limit(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    recording_bridge.response = {
        'ok': True,
        'operation': 'node_params_set',
        'data': {'node_id': 'n1'},
        'diagnostics': [],
        'warnings': [],
    }
    monkeypatch.setattr(runtime, 'bridge', recording_bridge)
    execute = server.ue_execute(operation='node_params_set', payload={'asset_path': '/Game/M.M', 'node_id': 'n1'})

    response = server.ue_diff_get(since_token=execute['data']['diff_token'], limit=0)

    assert response['ok'] is False
    assert response['error']['code'] == 'invalid_limit'


def test_ue_diff_get_rejects_non_integer_limit(monkeypatch: Any) -> None:
    recording_bridge = RecordingBridge()
    recording_bridge.response = {
        'ok': True,
        'operation': 'node_params_set',
        'data': {'node_id': 'n1'},
        'diagnostics': [],
        'warnings': [],
    }
    monkeypatch.setattr(runtime, 'bridge', recording_bridge)
    execute = server.ue_execute(operation='node_params_set', payload={'asset_path': '/Game/M.M', 'node_id': 'n1'})

    response = server.ue_diff_get(since_token=execute['data']['diff_token'], limit='x')  # type: ignore[arg-type]

    assert response['ok'] is False
    assert response['error']['code'] == 'invalid_limit'


def test_ue_plan_validate_rejects_unknown_operations() -> None:
    response = server.ue_plan_validate([
        {"operation": "node_create", "payload": {"asset_path": "/Game/M.M"}},
        {"operation": "python_exec", "payload": {}},
    ])

    assert response["ok"] is False
    assert response["data"]["valid"] is False
    assert response["remaining_errors"] == 1
