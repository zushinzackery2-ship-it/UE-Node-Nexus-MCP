"""batch_execute tests on the live ue_execute facade path with fake bridges."""

from __future__ import annotations

from typing import Any, Callable

import pytest

from ue_node_nexus_mcp import runtime
from ue_node_nexus_mcp.tools_facade import ue_execute

from tests.support.bridges import RecordingBridge, SequencedBridge


def _batch(operations: list[dict[str, Any]], **extra: Any) -> dict[str, Any]:
    return ue_execute("batch_execute", {"operations": operations, **extra})


def test_happy_path_runs_all_items_in_order(all_features: None, use_bridge: Callable) -> None:
    bridge = use_bridge(RecordingBridge({"ok": True, "data": {"asset_path": "/Game/A.A"}, "diagnostics": [], "warnings": []}))
    result = _batch(
        [
            {"operation": "asset_get", "payload": {"asset_path": "/Game/A.A"}},
            {"operation": "asset_compile", "payload": {"asset_path": "/Game/A.A"}},
        ]
    )
    assert result["ok"] is True
    data = result["data"]
    assert (data["total"], data["executed"], data["failed"], data["skipped"]) == (2, 2, 0, 0)
    assert [call[0] for call in bridge.calls] == ["asset_get", "asset_compile"]
    assert all(item["ok"] for item in data["items"])
    assert data["items"][0]["asset_path"] == "/Game/A.A"
    assert "asset_path=/Game/A.A" in data["items"][0]["summary"]


def test_batch_stops_at_first_failure_by_default(all_features: None, use_bridge: Callable) -> None:
    bridge = use_bridge(
        SequencedBridge(
            [
                {"ok": True, "data": {}},
                {"ok": False, "error": {"code": "asset_not_found", "message": "missing"}},
                {"ok": True, "data": {}},
            ]
        )
    )
    result = _batch(
        [
            {"operation": "asset_get", "payload": {"asset_path": "/Game/A.A"}},
            {"operation": "asset_get", "payload": {"asset_path": "/Game/B.B"}},
            {"operation": "asset_get", "payload": {"asset_path": "/Game/C.C"}},
        ]
    )
    assert result["ok"] is False
    assert result["error"]["code"] == "batch_item_failed"
    assert result["error"]["details"]["first_failed_index"] == 1
    data = result["data"]
    assert (data["executed"], data["failed"], data["skipped"]) == (2, 1, 1)
    assert data["items"][1]["error"]["code"] == "asset_not_found"
    assert len(bridge.calls) == 2


def test_continue_on_error_keeps_going(all_features: None, use_bridge: Callable) -> None:
    bridge = use_bridge(
        SequencedBridge(
            [
                {"ok": False, "error": {"code": "asset_not_found", "message": "missing"}},
                {"ok": True, "data": {}},
            ]
        )
    )
    result = _batch(
        [
            {"operation": "asset_get", "payload": {"asset_path": "/Game/A.A"}},
            {"operation": "asset_get", "payload": {"asset_path": "/Game/B.B"}},
        ],
        continue_on_error=True,
    )
    assert result["ok"] is False
    assert (result["data"]["executed"], result["data"]["failed"], result["data"]["skipped"]) == (2, 1, 0)
    assert result["data"]["items"][1]["ok"] is True
    assert len(bridge.calls) == 2


def test_invalid_batch_executes_nothing(all_features: None, use_bridge: Callable) -> None:
    bridge = use_bridge(RecordingBridge())
    result = _batch(
        [
            {"operation": "asset_get", "payload": {"asset_path": "/Game/A.A"}},
            {"operation": "no_such_operation", "payload": {}},
            {"operation": "asset_get", "payload": {}},
        ]
    )
    assert result["ok"] is False
    assert result["error"]["code"] == "invalid_batch"
    codes = {entry["index"]: entry["code"] for entry in result["error"]["details"]["errors"]}
    assert codes == {1: "unknown_operation", 2: "missing_required_field"}
    assert bridge.calls == []


def test_nested_batch_is_rejected(all_features: None, use_bridge: Callable) -> None:
    bridge = use_bridge(RecordingBridge())
    result = _batch([{"operation": "batch_execute", "payload": {"operations": []}}])
    assert result["ok"] is False
    assert result["error"]["details"]["errors"][0]["code"] == "nested_batch"
    assert bridge.calls == []


def test_disabled_feature_group_fails_validation(
    all_features: None, use_bridge: Callable, monkeypatch: pytest.MonkeyPatch
) -> None:
    bridge = use_bridge(RecordingBridge())
    monkeypatch.setattr(runtime, "_enabled_features", {"core", "asset"})
    result = _batch([{"operation": "niagara_system_summary_get", "payload": {"asset_path": "/Game/FX.FX"}}])
    assert result["ok"] is False
    assert result["error"]["details"]["errors"][0]["code"] == "feature_disabled"
    assert bridge.calls == []


def test_batch_size_and_shape_limits(all_features: None, use_bridge: Callable) -> None:
    use_bridge(RecordingBridge())
    too_many = [{"operation": "asset_get", "payload": {"asset_path": "/Game/A.A"}}] * 21
    result = _batch(too_many)
    assert result["ok"] is False
    assert result["error"]["code"] == "invalid_operation"
    assert "batch limit" in result["error"]["message"]

    empty = _batch([])
    assert empty["ok"] is False
    assert empty["error"]["code"] == "invalid_operation"


def test_client_side_handlers_run_inside_batches(all_features: None, use_bridge: Callable) -> None:
    """Batch items must go through execute_operation, not the raw bridge call."""
    bridge = use_bridge(
        RecordingBridge(
            {
                "ok": True,
                "data": {"items": [], "error_count": 0, "warning_count": 0},
                "diagnostics": [],
                "warnings": [],
            }
        )
    )
    result = _batch([{"operation": "diagnostics_get", "payload": {}}])
    assert result["ok"] is True
    # diagnostics enrichment reads project context on the same path ue_execute uses
    assert [call[0] for call in bridge.calls] == ["diagnostics_get", "project_context_get"]
