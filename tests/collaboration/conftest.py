"""Merge and history tests simulate a successful external validation dependency.

Admission itself is covered by tests/safety and the independent live worker run.
These protocol fixtures model UE mutations, rather than starting an editor.
"""

import pytest

from ue_node_nexus_mcp.safety.publication.risk import reasons


@pytest.fixture(autouse=True)
def successful_isolation_dependency(monkeypatch):
    def admit(bridge, context, workspace, item, candidate, observation, options):
        if not reasons(item):
            return None
        return dict(validation_id="simulated-worker", candidate=candidate, asset=item["asset"])

    monkeypatch.setattr("ue_node_nexus_mcp.safety.publication.gate.admit", admit)
