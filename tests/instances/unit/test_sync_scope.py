"""Rejected sync submissions leave the manager free to close an idle editor."""

from pathlib import Path
import time

import pytest

from ue_node_nexus_mcp.instances.session.binding import EditorSession
from ue_node_nexus_mcp.transcode.errors import SyncError
from ue_node_nexus_mcp.transcode.sync import background, lifecycle as sync_lifecycle
from tests.instances.support.sdk import LocalBroker
from .conftest import session_ready


@pytest.fixture
def session(lifecycle, monkeypatch):
    service, _, _, project = lifecycle
    session = EditorSession(LocalBroker(service, project.parent), str(project), startup_wait=0)
    session.ensure(dict(mode="reuse_or_start", dry_run=False))
    session_ready(session, service)
    monkeypatch.setattr(sync_lifecycle, "instance_manager", session)
    yield session
    session.close()


@pytest.mark.parametrize("action", ["checkout", "fetch", "pull", "push", "recover", "schema", "status"])
def test_invalid_options_leave_no_scope_and_allow_close(session, lifecycle, action):
    with pytest.raises(SyncError, match="unsupported options"):
        sync_lifecycle.run_managed_sync(None, action, None, dict(unknown_option=True), None)
    assert session.status()["scope_count"] == 0
    assert session.scopes == dict()
    response = session.call("close", dict(instance_id=session.current()["instance_id"], dry_run=False))
    assert response["state"] == "DRAINING"
    deadline = time.monotonic() + 3
    while session.status()["state"] != "EXITED" and time.monotonic() < deadline:
        lifecycle[0].reconcile()
        time.sleep(0.005)
    assert session.status()["state"] == "EXITED"


@pytest.mark.parametrize("boundary", ["project_dir", "Store", "submit"])
def test_submission_initialization_failure_releases_reserved_scope(session, monkeypatch, boundary):
    def reject(*args, **kwargs):
        raise SyncError("store_busy", "submission initialization failed")

    monkeypatch.setattr(background, boundary, reject)
    scope = session.reserve("ue_sync_pull", wait=False)
    with pytest.raises(SyncError, match="submission initialization failed"):
        background.run("pull", None, dict(project="Demo"), None, Path(session.repository()["mirror_root"]), scope)
    assert session.status()["scope_count"] == 0
    assert session.scopes == dict()
