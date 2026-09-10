"""Background task queue tests on the live ue_execute facade path."""

from __future__ import annotations

import threading
from typing import Any, Callable

import pytest

from ue_node_nexus_mcp import task_queue
from ue_node_nexus_mcp.tools_facade import ue_execute

from tests.support.bridges import RecordingBridge, RoutingBridge


@pytest.fixture(autouse=True)
def clean_task_state() -> None:
    task_queue.reset_for_tests()
    yield
    task_queue.reset_for_tests()


def _submit(operation: str, payload: dict[str, Any] | None = None) -> dict[str, Any]:
    request: dict[str, Any] = {"operation": operation}
    if payload is not None:
        request["payload"] = payload
    return ue_execute("task_submit", request)


def _wait(task_id: str) -> None:
    assert task_queue.wait_for_task(task_id, 5.0), f"task did not finish in time: {task_id}"


def test_submit_runs_in_background_and_stores_result(all_features: None, use_bridge: Callable) -> None:
    bridge = use_bridge(RecordingBridge({"ok": True, "data": {"compiled": True}, "diagnostics": [], "warnings": []}))
    submitted = _submit("asset_compile", {"asset_path": "/Game/M.M"})
    assert submitted["ok"] is True
    task_id = submitted["data"]["task_id"]
    assert submitted["data"]["status"] == "queued"

    _wait(task_id)
    status = ue_execute("task_status", {"task_id": task_id})
    assert status["data"]["status"] == "succeeded"
    assert "summary" in status["data"]

    result = ue_execute("task_result", {"task_id": task_id})
    assert result["ok"] is True
    assert result["data"]["result"]["ok"] is True
    assert result["data"]["result"]["data"]["compiled"] is True
    assert bridge.calls[0] == ("asset_compile", {"asset_path": "/Game/M.M"})


def test_failed_operation_is_reported_as_failed(all_features: None, use_bridge: Callable) -> None:
    use_bridge(RecordingBridge({"ok": False, "error": {"code": "asset_not_found", "message": "missing"}, "diagnostics": [], "warnings": []}))
    task_id = _submit("asset_compile", {"asset_path": "/Game/Nope.Nope"})["data"]["task_id"]
    _wait(task_id)

    status = ue_execute("task_status", {"task_id": task_id})
    assert status["data"]["status"] == "failed"
    assert status["data"]["error"]["code"] == "asset_not_found"

    result = ue_execute("task_result", {"task_id": task_id})
    assert result["ok"] is True
    assert result["data"]["status"] == "failed"
    assert result["data"]["result"]["ok"] is False


def test_submit_validates_upfront_and_rejects_task_nesting(all_features: None, use_bridge: Callable) -> None:
    bridge = use_bridge(RecordingBridge())
    unknown = _submit("no_such_operation")
    assert unknown["ok"] is False and unknown["error"]["code"] == "unknown_operation"

    missing = _submit("asset_compile", {})
    assert missing["ok"] is False and missing["error"]["code"] == "missing_required_field"

    nested = _submit("task_submit", {"operation": "asset_get"})
    assert nested["ok"] is False and nested["error"]["code"] == "nested_task"

    assert bridge.calls == []


def test_cancel_only_hits_queued_tasks(all_features: None, use_bridge: Callable) -> None:
    release = threading.Event()

    def slow_compile(payload: dict[str, Any]) -> dict[str, Any]:
        release.wait(5.0)
        return {"ok": True, "data": {}}

    bridge = use_bridge(RoutingBridge({"asset_compile": slow_compile, "asset_get": {"ok": True, "data": {}}}))
    try:
        first = _submit("asset_compile", {"asset_path": "/Game/A.A"})["data"]["task_id"]
        second = _submit("asset_get", {"asset_path": "/Game/B.B"})["data"]["task_id"]

        cancelled = ue_execute("task_cancel", {"task_id": second})
        assert cancelled["ok"] is True
        assert ue_execute("task_status", {"task_id": second})["data"]["status"] == "cancelled"

        not_done = ue_execute("task_result", {"task_id": first})
        assert not_done["ok"] is False and not_done["error"]["code"] == "task_not_done"
    finally:
        release.set()
    _wait(first)

    # The cancelled task must never reach the bridge.
    assert [call[0] for call in bridge.calls] == ["asset_compile"]
    recancel = ue_execute("task_cancel", {"task_id": first})
    assert recancel["ok"] is False and recancel["error"]["code"] == "task_not_cancellable"


def test_status_lists_tasks_newest_first(all_features: None, use_bridge: Callable) -> None:
    use_bridge(RecordingBridge({"ok": True, "data": {}, "diagnostics": [], "warnings": []}))
    first = _submit("asset_get", {"asset_path": "/Game/A.A"})["data"]["task_id"]
    second = _submit("asset_get", {"asset_path": "/Game/B.B"})["data"]["task_id"]
    _wait(first)
    _wait(second)

    listing = ue_execute("task_status", {})
    assert listing["data"]["count"] == 2
    assert [row["task_id"] for row in listing["data"]["tasks"]] == [second, first]


def test_unknown_task_ids_are_reported(all_features: None, use_bridge: Callable) -> None:
    use_bridge(RecordingBridge())
    for operation in ("task_status", "task_result", "task_cancel"):
        result = ue_execute(operation, {"task_id": "task-999"})
        assert result["ok"] is False and result["error"]["code"] == "task_not_found"


def test_batch_execute_can_run_inside_a_task(all_features: None, use_bridge: Callable) -> None:
    bridge = use_bridge(RecordingBridge({"ok": True, "data": {}, "diagnostics": [], "warnings": []}))
    submitted = _submit(
        "batch_execute",
        {
            "operations": [
                {"operation": "asset_compile", "payload": {"asset_path": "/Game/M.M"}},
                {"operation": "asset_save", "payload": {"asset_path": "/Game/M.M"}},
            ]
        },
    )
    task_id = submitted["data"]["task_id"]
    _wait(task_id)

    result = ue_execute("task_result", {"task_id": task_id})
    assert result["data"]["status"] == "succeeded"
    assert result["data"]["result"]["data"]["executed"] == 2
    assert [call[0] for call in bridge.calls] == ["asset_compile", "asset_save"]


def test_queue_rejects_submissions_beyond_active_limit(all_features: None, use_bridge: Callable) -> None:
    release = threading.Event()

    def slow(payload: dict[str, Any]) -> dict[str, Any]:
        release.wait(10.0)
        return {"ok": True, "data": {}}

    use_bridge(RoutingBridge({"asset_get": slow}))
    try:
        ids = [
            _submit("asset_get", {"asset_path": f"/Game/A{index}.A{index}"})["data"]["task_id"]
            for index in range(task_queue.MAX_ACTIVE_TASKS)
        ]
        overflow = _submit("asset_get", {"asset_path": "/Game/Over.Over"})
        assert overflow["ok"] is False
        assert overflow["error"]["code"] == "task_queue_full"
    finally:
        release.set()
    for task_id in ids:
        _wait(task_id)
