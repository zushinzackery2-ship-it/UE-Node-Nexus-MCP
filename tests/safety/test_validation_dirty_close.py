"""Dirty packages discovered by native close must reach worker save and exit."""

from pathlib import Path
from types import SimpleNamespace

import pytest

from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.safety.isolation import process as isolation_process
from ue_node_nexus_mcp.safety.isolation import session as session_module
from ue_node_nexus_mcp.safety.isolation.session import ValidationSession


def worker(monkeypatch, boundary, packages, save_ok=True):
    pending, writes, forced = list(packages), [], []
    state = dict(instance_id="worker", state="READY", dirty_packages=[], users=[], pid=42)
    session = ValidationSession.__new__(ValidationSession)
    session.project, session.timeout = Path("."), 1

    def status(*args, **kwargs):
        return dict(state, dirty_packages=list(pending) if boundary == "initial" else [])

    session.manager = SimpleNamespace(current=lambda: state, status=status,
                                      release=lambda: None, close=lambda: None,
                                      broker=SimpleNamespace(session_id="self"))

    def close(manager, payload):
        blocked = dict(state, state="BLOCKED", blockers=["instance_dirty"], dirty_packages=list(pending))
        if payload.get("dry_run", True):
            if boundary == "stale_preview":
                return dict(instance=dict(blocked, dirty_packages=list(packages)), blockers=["instance_dirty"])
            observed = blocked if boundary == "preview" and pending else state
            return dict(instance=observed, blockers=observed.get("blockers", []))
        if pending:
            raise InstanceError("close_blocked", "native close observed unsaved packages", dict(instance=blocked))
        return dict(exit_confirmed=True, exit_code=0)

    def call(operation, payload):
        assert operation == "asset_save"
        writes.append(payload["asset_path"])
        if save_ok:
            pending.remove(payload["asset_path"])
        return dict(ok=save_ok)

    def stop(current, project):
        forced.append(current)
        return dict(exit_confirmed=True, forced=True, state="EXITED", exit_code=88, pid=42)

    session.call = call
    monkeypatch.setattr(session_module, "close_instance", close)
    monkeypatch.setattr(session_module, "time", SimpleNamespace(monotonic=lambda: 0.0, sleep=lambda seconds: None))
    monkeypatch.setattr(isolation_process, "stop_owned", stop)
    return session, writes, forced


@pytest.mark.parametrize("boundary", ["initial", "preview", "commit", "stale_preview"])
def test_dirty_state_from_every_close_boundary_is_saved(monkeypatch, boundary):
    packages = ["/Game/Probe/First", "/Game/Probe/Second"]
    session, writes, forced = worker(monkeypatch, boundary, packages)
    assert session.close() == dict(exit_confirmed=True, exit_code=0)
    assert writes == packages
    assert forced == []


def test_late_shared_package_is_refused_before_save(monkeypatch):
    session, writes, forced = worker(monkeypatch, "commit", ["/Engine/Shared"])
    result = session.close()
    assert result["error"]["code"] == "isolation_shared_package_dirty"
    assert writes == []
    assert forced and result["exit_code"] == 88


def test_late_save_failure_preserves_the_cleanup_error(monkeypatch):
    session, writes, forced = worker(monkeypatch, "commit", ["/Game/Probe/First"], save_ok=False)
    result = session.close()
    assert result["error"]["code"] == "isolation_cleanup_failed"
    assert writes == ["/Game/Probe/First"]
    assert forced and result["exit_code"] == 88
