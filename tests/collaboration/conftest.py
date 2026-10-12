"""Merge and history tests simulate a successful external validation dependency.

Admission itself is covered by tests/safety and the independent live worker run.
These protocol fixtures model UE mutations, rather than starting an editor.
"""

import pytest

from ue_node_nexus_mcp.safety.publication.risk import reasons
from ue_node_nexus_mcp.transcode.storage.io import digest


@pytest.fixture(autouse=True)
def successful_isolation_dependency(monkeypatch):
    def admit(bridge, context, workspace, item, candidate, observation, options):
        if not reasons(item):
            return None
        return dict(validation_id="simulated-worker", candidate=candidate, asset=item["asset"])

    def admit_batch(bridge, context, workspace, entries, candidate, observation, options):
        return dict((asset, dict(validation_id="simulated-worker", candidate=candidate, asset=asset,
                                binding=dict(candidate=candidate, payload_digest=digest(item["payload"]))))
                    for asset, item in entries)

    monkeypatch.setattr("ue_node_nexus_mcp.safety.publication.gate.admit", admit)
    monkeypatch.setattr("ue_node_nexus_mcp.safety.publication.gate.admit_batch", admit_batch)
    monkeypatch.setattr("ue_node_nexus_mcp.safety.publication.gate.receipt_valid", lambda receipt, item, candidate, **kwargs:
                        receipt["binding"]["candidate"] == candidate and receipt["binding"]["payload_digest"] == digest(item["payload"]))
