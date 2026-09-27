"""Editor cleanup retains the primary failure and writes exit evidence."""

from types import SimpleNamespace

import pytest

from tests.live.editor.session import EditorSession
from ue_node_nexus_mcp.instances.errors import InstanceError


def broken_cleanup(monkeypatch):
    def close(*args):
        raise InstanceError("close_blocked", "dirty package", dict(instance=dict(state="BLOCKED")))

    monkeypatch.setattr("ue_node_nexus_mcp.instances.session.shutdown.close_instance", close)
    records, closed = [], []
    session = EditorSession.__new__(EditorSession)
    session.session = SimpleNamespace(current=lambda: dict(instance_id="test"), release=lambda: None,
                                      status=lambda: dict(state="READY"), close=lambda: closed.append(True))
    session.previous_bridge = None
    session.report = lambda stage, record: records.append((stage, record))
    return session, records, closed


def test_cleanup_failure_keeps_the_workflow_exception(monkeypatch):
    session, records, closed = broken_cleanup(monkeypatch)
    assert session.__exit__(AssertionError, AssertionError("readback failed"), None) is False
    assert closed == [True]
    assert records[0][0] == "exit"
    assert records[0][1]["normal"] is False
    assert records[0][1]["instance"]["state"] == "BLOCKED"
    assert records[0][1]["error"]["code"] == "close_blocked"


def test_cleanup_failure_alone_still_fails_acceptance(monkeypatch):
    session, records, closed = broken_cleanup(monkeypatch)
    with pytest.raises(InstanceError, match="dirty package"):
        session.__exit__(None, None, None)
    assert closed == [True] and records[0][1]["normal"] is False
