"""A validation worker that only finished late shader work still exits normally."""

from pathlib import Path
from types import SimpleNamespace

import pytest

from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.safety.isolation import process as isolation_process
from ue_node_nexus_mcp.safety.isolation import session as session_module
from ue_node_nexus_mcp.safety.isolation.session import ValidationSession, normal_exit, transient_close_blocker


def test_transient_blockers_are_the_compiler_ones():
    assert transient_close_blocker(dict(blockers=["editor_busy"], compiling=True))
    assert transient_close_blocker(dict(saving=True))
    assert not transient_close_blocker(dict())
    assert not transient_close_blocker(dict(blockers=[]))
    assert not transient_close_blocker(dict(blockers=["state_blocked"]))
    assert not transient_close_blocker(dict(blockers=["editor_busy"], dirty_packages=["/Game/X"]))
    assert not transient_close_blocker(dict(blockers=["editor_busy"], pie=True))
    assert not transient_close_blocker(dict(blockers=["editor_busy"], users=[dict(client_session_id="other")]))


def test_normal_exit_retries_a_late_shader_compile(monkeypatch):
    attempts = []

    def close(manager, payload):
        attempts.append(payload)
        if len(attempts) == 1:
            raise InstanceError("close_blocked", "busy", dict(instance=dict(blockers=["editor_busy"], compiling=True)))
        return dict(exit_confirmed=True, exit_code=0)

    monkeypatch.setattr(session_module, "close_instance", close)
    monkeypatch.setattr(session_module, "time", SimpleNamespace(monotonic=lambda: 0.0, sleep=lambda seconds: None))
    assert normal_exit(None, "worker", float("inf")) == dict(exit_confirmed=True, exit_code=0)
    assert len(attempts) == 2 and all(attempt["dry_run"] is False for attempt in attempts)


def test_unknown_blocker_is_not_retried(monkeypatch):
    def close(manager, payload):
        raise InstanceError("close_blocked", "busy", dict(instance=dict(blockers=["instance_in_use"])))

    monkeypatch.setattr(session_module, "close_instance", close)
    with pytest.raises(InstanceError) as error:
        normal_exit(None, "worker", float("inf"))
    assert error.value.code == "close_blocked"


def test_close_force_stops_when_the_blocker_is_not_transient(monkeypatch):
    state = dict(instance_id="worker", state="READY", dirty_packages=[], users=[], pid=42)
    session = ValidationSession.__new__(ValidationSession)
    session.manager = SimpleNamespace(current=lambda: state, status=lambda *args, **kwargs: state,
                                      release=lambda: None, close=lambda: None,
                                      broker=SimpleNamespace(session_id="self"))
    session.project, session.timeout = Path("."), 1
    session.requests = Path("requests.jsonl")
    forced = []

    def close(manager, payload):
        if payload.get("dry_run", True):
            return dict(dry_run=True, instance=state, blockers=[])
        raise InstanceError("close_blocked", "busy", dict(instance=dict(blockers=["editor_busy"], pie=True)))

    def stop(current, project):
        forced.append(current)
        return dict(exit_confirmed=True, forced=True, state="EXITED", exit_code=88, pid=42)

    monkeypatch.setattr(session_module, "close_instance", close)
    monkeypatch.setattr(isolation_process, "stop_owned", stop)
    result = session.close()
    assert forced and result["forced"] and result["exit_code"] == 88
