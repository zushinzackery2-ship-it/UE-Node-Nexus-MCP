"""A completed operation cannot make a crashed validation worker pass."""

import json
from pathlib import Path
from types import SimpleNamespace

import pytest

from tests.safety.test_isolation import project
from ue_node_nexus_mcp.safety.isolation import service
from ue_node_nexus_mcp.safety.isolation.session import ValidationSession


@pytest.mark.parametrize("exit_code", [0, 3, None])
def test_validation_requires_normal_worker_exit(all_features, tmp_path, monkeypatch, exit_code):
    source = project(tmp_path)

    class Worker:
        def __init__(self, *args):
            pass

        def start(self):
            return dict(instance=dict(instance_id="isolated"))

        def execute(self, item):
            return dict(ok=True)

        def close(self):
            return dict(exit_confirmed=True, instance=dict(state="EXITED", exit_code=exit_code,
                        crash_evidence=dict(exit_kind="fatal_crash") if exit_code == 3 else None))

    monkeypatch.setattr(service, "runtime_root", lambda: tmp_path / "runtime")
    monkeypatch.setattr("ue_node_nexus_mcp.instances.lifecycle.engine.resolve_engine", lambda *args: tmp_path)
    monkeypatch.setattr("ue_node_nexus_mcp.safety.isolation.session.ValidationSession", Worker)
    item = dict(operation="asset_create", payload=dict(asset_path="/Game/Safety/Probe", asset_kind="material"))
    result = service.validate(str(source), [item], None, "d3d12", 30, False)
    assert result["ok"] == (exit_code == 0)
    record = json.loads(Path(result["data"]["receipt_path"]).read_text())
    assert record["source_unchanged"] and record["cleanup"]["instance"]["exit_code"] == exit_code
    if exit_code != 0:
        assert record["error"]["code"] == "isolation_worker_exit_failed"


def test_already_exited_worker_keeps_exit_code_and_crash_evidence():
    state = dict(instance_id="isolated", state="EXITED", exit_code=3,
                 crash_evidence=dict(exit_kind="fatal_crash"))
    closed = []
    session = ValidationSession.__new__(ValidationSession)
    session.manager = SimpleNamespace(current=lambda: state, status=lambda *args: state,
                                      release=lambda: None, close=lambda: closed.append(True))
    result = session.close()
    assert result["exit_code"] == 3 and result["instance"]["crash_evidence"] == state["crash_evidence"]
    assert closed == [True]
