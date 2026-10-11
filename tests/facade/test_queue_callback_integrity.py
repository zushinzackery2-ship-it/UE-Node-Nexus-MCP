"""Callback faults cannot publish unowned work or kill the queue consumer."""

from unittest.mock import Mock

import pytest

from ue_node_nexus_mcp import task_queue
from tests.support.scopes import Scope


@pytest.fixture
def queue():
    task_queue.shutdown()
    task_queue.reset_for_tests()
    yield
    task_queue.shutdown()
    task_queue.reset_for_tests()


def test_submission_persistence_failure_never_executes_or_retains_task(queue):
    execute = Mock(return_value=dict(ok=True))
    with pytest.raises(OSError, match="durable submit"):
        task_queue.submit_callable("test", execute, on_submit=Mock(side_effect=OSError("durable submit")))
    assert task_queue.task_status()["data"]["tasks"] == []
    execute.assert_not_called()
    control = task_queue.submit_callable("control", lambda: dict(ok=True))
    assert task_queue.wait_for_task(control, 3)


def test_public_submission_worker_start_fault_releases_scope_and_removes_task(queue, monkeypatch):
    scope = Mock()
    monkeypatch.setattr(task_queue, "reserve", lambda *_args: scope)
    monkeypatch.setattr(task_queue, "_ensure_worker", Mock(side_effect=OSError("consumer start")))
    with pytest.raises(OSError, match="consumer start"):
        task_queue.task_submit("asset_get", dict(asset_path="/Game/Probe.Probe"))
    assert task_queue.task_status()["data"]["tasks"] == []
    scope.release.assert_called_once_with()


@pytest.mark.parametrize("boundary", ["finish", "scope"])
def test_cleanup_fault_is_terminal_and_next_task_completes(queue, boundary):
    scope = Scope()
    finish = Mock()
    if boundary == "finish":
        finish.side_effect = OSError("durable completion")
    else:
        scope.release = Mock(side_effect=OSError("scope release"))
    first = task_queue.submit_callable("first", lambda: dict(ok=True), scope, on_finish=finish)
    second = task_queue.submit_callable("second", lambda: dict(ok=True))
    assert task_queue.wait_for_task(first, 3)
    assert task_queue.wait_for_task(second, 3)
    row = task_queue.task_result(first)["data"]
    assert row["status"] == "failed" and row["finished_at"]
    assert row["error"]["code"] == "task_completion_failed"
    assert row["error"]["details"]["failures"][0]["phase"] == boundary
    finish.assert_called_once()
    assert task_queue.task_result(second)["data"]["status"] == "succeeded"
