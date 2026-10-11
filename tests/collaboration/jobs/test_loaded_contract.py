"""A mixed installed/loaded scheduler cannot accept work without ownership handoff."""

from unittest.mock import Mock

import pytest

from ue_node_nexus_mcp import task_queue
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.errors import SyncError
from ue_node_nexus_mcp.transcode.sync import background


def test_old_loaded_scheduler_is_rejected_before_job_creation(tmp_path, monkeypatch):
    calls = []

    def old_submit(operation, callback, scope=None, on_cancel=None, on_finish=None):
        calls.append(operation)
        return "untracked-task"

    monkeypatch.setattr(task_queue, "submit_callable", old_submit)
    scope, runner = Mock(), Mock()
    with pytest.raises(SyncError) as failed:
        background.run("pull", None, dict(project="Demo", background=True), runner, tmp_path, scope)
    assert failed.value.code == "mcp_restart_required"
    details = failed.value.details
    assert details["loaded"]["supports_on_submit"] is False
    assert details["disk"]["supports_on_submit"] is True
    assert details["loaded"]["version"] and details["disk"]["version"]
    assert details["required_action"] == "restart_mcp_process"
    assert not calls
    runner.assert_not_called()
    scope.release.assert_called_once_with()
    store = Store(tmp_path / "Demo/.nexus/collaboration")
    assert store.records("sync_job") == []
    store.db.discard()
