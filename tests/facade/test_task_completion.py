"""A task's public terminal state includes scope cleanup and durable callbacks."""

from contextlib import nullcontext
from threading import Event, Thread
from unittest.mock import Mock

import pytest

from ue_node_nexus_mcp.tasks import queue


@pytest.fixture(autouse=True)
def clean_queue():
    queue.shutdown()
    queue.reset_for_tests()
    yield
    queue.shutdown()
    queue.reset_for_tests()


@pytest.mark.parametrize("phase", ["scope", "finish"])
@pytest.mark.parametrize("fails", [False, True])
def test_completion_is_active_until_all_obligations_finish(monkeypatch, phase, fails):
    entered, release = Event(), Event()
    finished = []

    def pause():
        entered.set()
        assert release.wait(5)
        if fails:
            raise OSError("completion probe")

    class Scope:
        def activate(self):
            return nullcontext()

        def release(self):
            if phase == "scope":
                pause()

    def finish(record):
        if phase == "finish":
            pause()
        finished.append(record.status)

    identifier = queue.submit_callable("probe", lambda: dict(ok=True, data={}),
                                       Scope(), on_finish=finish)
    try:
        assert entered.wait(5)
        assert queue.task_status(identifier)["data"]["status"] == "running"
        assert queue.task_result(identifier)["error"]["code"] == "task_not_done"
        monkeypatch.setattr(queue, "MAX_FINISHED_TASKS", 0)
        queue._prune_finished()
        assert identifier in queue._tasks
    finally:
        monkeypatch.setattr(queue, "MAX_FINISHED_TASKS", 50)
        release.set()
        assert queue.wait_for_task(identifier, 5)
    result = queue.task_result(identifier)["data"]
    assert result["status"] == ("failed" if fails else "succeeded")
    if fails:
        assert result["error"]["code"] == "task_completion_failed"
        assert result["operation_diagnostics"]["error_count"] > 0
        assert result["result"]["ok"] is False
    else:
        assert finished == ["succeeded"]


def test_reserving_cancellation_waits_for_scope_ownership(monkeypatch):
    entered, release = Event(), Event()
    scope = Mock()
    result = []

    def reserve(*_args):
        entered.set()
        assert release.wait(5)
        return scope

    monkeypatch.setattr(queue, "reserve", reserve)
    thread = Thread(target=lambda: result.append(queue.task_submit("asset_get", dict(asset_path="/Game/A.A"))))
    thread.start()
    try:
        assert entered.wait(5)
        identifier = queue.task_status()["data"]["tasks"][0]["task_id"]
        assert queue.task_cancel(identifier)["data"]["status"] == "cancelling"
        assert queue.task_result(identifier)["error"]["code"] == "task_not_done"
        scope.release.assert_not_called()
    finally:
        release.set()
        thread.join(5)
    assert not thread.is_alive()
    scope.release.assert_called_once_with()
    assert result[0]["data"]["status"] == "cancelled"
    assert queue.wait_for_task(identifier, 1)


def test_cancel_callback_failure_returns_actual_terminal_state():
    occupied, unblock = Event(), Event()

    def first():
        occupied.set()
        assert unblock.wait(5)
        return dict(ok=True)

    running = queue.submit_callable("first", first)
    assert occupied.wait(5)
    cancelled = queue.submit_callable("second", Mock(), on_cancel=Mock(side_effect=OSError("cancel probe")))
    try:
        response = queue.task_cancel(cancelled)
        assert response["data"]["status"] == "failed"
        assert queue.task_result(cancelled)["data"]["operation_diagnostics"]["status"] == "failed"
    finally:
        unblock.set()
        assert queue.wait_for_task(running, 5)
