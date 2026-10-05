"""Exercise the public runtime gate against one identified PIE observation."""

import pytest

from ue_node_nexus_mcp.facade_execute import execute_operation
from ue_node_nexus_mcp.safety.runtime import tools
from tests.diagnostics.contracts.test_runtime_verdict import runtime_data


@pytest.mark.parametrize("changes,passed,code", (
    (dict(), True, "runtime_diagnostics_clean"),
    (dict(error_count=303), False, "runtime_diagnostics_failed"),
    (dict(active=True), False, "runtime_observation_incomplete"),
    (dict(dropped_count=1), False, "runtime_observation_incomplete"),
    (dict(sources_complete=False), False, "runtime_observation_incomplete"),
    (dict(session_id="different"), False, "runtime_session_mismatch"),
    (dict(instance_id="different"), False, "runtime_session_mismatch")))
def test_public_gate_uses_exact_session(monkeypatch, changes, passed, code):
    observed = runtime_data(**changes)
    calls = []

    def bridge(operation, payload):
        calls.append((operation, payload))
        return dict(ok=True, data=dict(runtime=observed))

    monkeypatch.setattr(tools, "call_bridge", bridge)
    expected = runtime_data()
    result = execute_operation("runtime_verification_get", dict(session_id=expected["session_id"],
        instance_id=expected["instance_id"], functional_passed=True))
    assert result["ok"] is passed
    assert result["data"]["runtime_verification"]["code"] == code
    assert calls == [("diagnostics_get", dict(session_id=expected["session_id"],
        include_assets=False, include_history=False, limit=3, severity="error"))]
    assert result["runtime_diagnostics"]["session_id"] == observed["session_id"]


def test_functional_failure_is_retained(monkeypatch):
    observed = runtime_data()
    monkeypatch.setattr(tools, "call_bridge", lambda *_: dict(ok=True, data=dict(runtime=observed)))
    result = execute_operation("runtime_verification_get", dict(session_id=observed["session_id"],
        instance_id=observed["instance_id"], functional_passed=False))
    assert result["ok"] is False
    assert result["error"]["code"] == "functional_checks_failed"
