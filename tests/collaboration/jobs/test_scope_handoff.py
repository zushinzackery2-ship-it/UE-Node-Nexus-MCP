"""Request cleanup preserves scopes already owned by a real queued worker."""

from contextlib import nullcontext
import threading
from unittest.mock import Mock

import pytest

from ue_node_nexus_mcp import task_queue
from ue_node_nexus_mcp.transcode.sync import background, lifecycle


@pytest.fixture
def queue():
    task_queue.reset_for_tests()
    yield
    for task in task_queue.task_status()["data"]["tasks"]:
        assert task_queue.wait_for_task(task["task_id"], 5)
    task_queue.reset_for_tests()


def scope():
    result = Mock()
    result.activate.side_effect = nullcontext
    return result


def test_invalid_managed_request_never_reserves_scope(tmp_path, monkeypatch):
    manager = lifecycle.instance_manager
    monkeypatch.setattr(manager, "repository", Mock(return_value=dict(
        mirror_root=str(tmp_path), mirror_project_name="Shadetest")))
    reserve = Mock()
    monkeypatch.setattr(manager, "reserve", reserve)
    with pytest.raises(Exception, match="unsupported options"):
        lifecycle.run_managed_sync(Mock(), "pull", None, dict(workspace_id="scope-test", force="ue"), Mock())
    reserve.assert_not_called()


def test_cleanup_failure_still_releases_unsubmitted_scope(tmp_path, monkeypatch):
    store = Mock()
    store.db.discard.side_effect = OSError("test discard failure")
    monkeypatch.setattr(background, "Store", Mock(return_value=store))
    monkeypatch.setattr(background, "submit", Mock(side_effect=RuntimeError("test submit failure")))
    work = scope()
    with pytest.raises(OSError, match="test discard failure"):
        background.run("pull", None, dict(project="Shadetest"), Mock(), tmp_path, work)
    work.release.assert_called_once_with()


def test_response_failure_preserves_running_worker_scope(tmp_path, monkeypatch, queue):
    entered, finish = threading.Event(), threading.Event()
    work = scope()

    def runner(*_args):
        entered.set()
        assert finish.wait(5)
        return dict(status="ok")

    monkeypatch.setattr(background, "observe", Mock(side_effect=OSError("test response failure")))
    try:
        with pytest.raises(OSError, match="test response failure"):
            background.run("pull", None, dict(project="Shadetest", background=True), runner, tmp_path, work)
        assert entered.wait(3)
        work.release.assert_not_called()
    finally:
        finish.set()
        for task in task_queue.task_status()["data"]["tasks"]:
            assert task_queue.wait_for_task(task["task_id"], 5)
    work.release.assert_called_once_with()


def test_duplicate_request_releases_only_its_unused_scope(tmp_path, queue):
    entered, finish = threading.Event(), threading.Event()
    original, duplicate = scope(), scope()
    options = dict(project="Shadetest", background=True, idempotency_key="scope-owner")

    def runner(*_args):
        entered.set()
        assert finish.wait(5)
        return dict(status="ok")

    try:
        first = background.run("pull", None, dict(options), runner, tmp_path, original)
        assert entered.wait(3)
        second = background.run("pull", None, dict(options), runner, tmp_path, duplicate)
        assert first["job_id"] == second["job_id"]
        original.release.assert_not_called()
        duplicate.release.assert_called_once_with()
    finally:
        finish.set()
        for task in task_queue.task_status()["data"]["tasks"]:
            assert task_queue.wait_for_task(task["task_id"], 5)
    original.release.assert_called_once_with()
