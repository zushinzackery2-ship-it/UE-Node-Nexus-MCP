"""A stale recorded plugin build is rejected before baseline export or copying."""

import pytest

from ue_node_nexus_mcp.safety.isolation.identity import verify
from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.transcode.errors import SyncError
from ue_node_nexus_mcp.safety.publication import gate
from .test_publication_gate import fixture


def test_identity_error_names_the_actual_mismatched_values():
    with pytest.raises(InstanceError) as error:
        verify({"VFX": dict(source_fingerprint="loaded")}, {"VFX": dict(source_fingerprint="recorded")}, phase="source_preflight")
    assert error.value.details == dict(phase="source_preflight", plugin="VFX", field="source_fingerprint",
                                       expected="recorded", actual="loaded")


def test_mismatch_does_not_export_baseline_or_start_validation(monkeypatch):
    bridge, context, workspace, item, observation, _, calls, events = fixture(monkeypatch)

    def mismatch(*args):
        verify({"VFX": dict(source_fingerprint="loaded")}, {"VFX": dict(source_fingerprint="recorded")}, phase="source_preflight")

    monkeypatch.setattr(gate, "verify_source_identity", mismatch)
    with pytest.raises(SyncError) as error:
        gate.admit(bridge, context, workspace, item, "candidate", observation, dict(isolation="auto"))
    assert error.value.code == "isolation_identity_mismatch"
    assert [name for name, _ in calls] == ["bridge_capabilities_get"]
    assert not events
