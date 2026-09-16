import json
import os
import time

import pytest

from ue_node_nexus_mcp.diagnostics import sessions
from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.instances.identity.processes import inspect_process


def group(directory, name, identity, size=0):
    record = directory / (name + ".json")
    record.write_text(json.dumps(dict(identity=identity, reserved_bytes=sessions.RESERVED_BYTES)), encoding="utf-8")
    log = directory / (name + ".log")
    log.write_bytes(b"x" * size)
    return record, log


def test_retention_removes_expired_inactive_logs_and_keeps_live_logs(tmp_path):
    current = inspect_process(os.getpid())
    live = group(tmp_path, f"session-{os.getpid()}-abc", current)
    dead = group(tmp_path, f"session-{os.getpid()}-def", dict(current, process_created="1"))
    old = time.time() - sessions.RETENTION_SECONDS - 60
    for file in (*live, *dead):
        os.utime(file, (old, old))
    new = sessions.allocate(tmp_path)
    assert new.with_suffix(".json").is_file()
    assert all(file.is_file() for file in live)
    assert all(not file.exists() for file in dead)


def test_malformed_metadata_with_live_pid_is_protected(tmp_path):
    name = f"session-{os.getpid()}-abc"
    record = tmp_path / (name + ".json")
    record.write_text("{partial", encoding="utf-8")
    log = tmp_path / (name + ".log")
    log.touch()
    sessions.allocate(tmp_path)
    assert record.is_file() and log.is_file()
    assert json.loads((tmp_path / "retention-status.json").read_text())["unverified"] == 1


def test_global_budget_prunes_inactive_groups_before_reserving(tmp_path, monkeypatch):
    monkeypatch.setattr(sessions, "TOTAL_BYTES", sessions.RESERVED_BYTES + 128)
    identity = dict(inspect_process(os.getpid()), process_created="1")
    files = group(tmp_path, f"session-{os.getpid()}-abc", identity, size=256)
    sessions.allocate(tmp_path)
    assert all(not file.exists() for file in files)


def test_active_reservations_have_an_explicit_capacity_limit(tmp_path, monkeypatch):
    monkeypatch.setattr(sessions, "TOTAL_BYTES", sessions.RESERVED_BYTES)
    sessions.allocate(tmp_path)
    with pytest.raises(InstanceError) as failure:
        sessions.allocate(tmp_path)
    assert failure.value.code == "log_capacity_exceeded"
