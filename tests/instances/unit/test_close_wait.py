"""A committed normal exit has its own deadline, including STOPPING."""

from types import SimpleNamespace

import pytest

from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.instances.session import shutdown


def session(monkeypatch, states):
    clock = [0.0]
    monkeypatch.setattr(shutdown.time, "monotonic", lambda: clock[0])
    monkeypatch.setattr(shutdown.time, "sleep", lambda seconds: clock.__setitem__(0, clock[0] + seconds))
    remaining = iter(states)
    current = [next(remaining)]

    def status(payload):
        if clock[0] >= current[0][0]:
            current[0] = next(remaining, current[0])
        return dict(instance_id="worker", **current[0][1])

    return SimpleNamespace(call=lambda *args: dict(instance_id="worker"), current=lambda: dict(), status=status,
                           broker=SimpleNamespace(policy=dict(shutdown_seconds=1, exit_seconds=4)))


@pytest.mark.parametrize("state", ["STOPPING", "UNRESPONSIVE"])
def test_committed_exit_survives_drain_deadline(monkeypatch, state):
    editor = session(monkeypatch, [(2.0, dict(state=state, exit_committed=True)),
                                  (4.0, dict(state="EXITED", exit_code=0))])
    result = shutdown.close_instance(editor, dict(dry_run=False))
    assert result["exit_confirmed"] and result["exit_code"] == 0


def test_uncommitted_close_keeps_drain_deadline(monkeypatch):
    editor = session(monkeypatch, [(10.0, dict(state="DRAINING"))])
    with pytest.raises(InstanceError) as error:
        shutdown.close_instance(editor, dict(dry_run=False))
    assert error.value.code == "shutdown_timeout"


def test_committed_exit_is_bounded(monkeypatch):
    editor = session(monkeypatch, [(10.0, dict(state="STOPPING", exit_committed=True))])
    with pytest.raises(InstanceError) as error:
        shutdown.close_instance(editor, dict(dry_run=False))
    assert error.value.code == "shutdown_timeout"
