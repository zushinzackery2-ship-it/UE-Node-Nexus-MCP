"""Replanned units preserve the caller's isolation policy before the main write."""

from types import SimpleNamespace
from unittest.mock import Mock

import pytest

from ue_node_nexus_mcp.safety.publication import gate
from ue_node_nexus_mcp.transcode.collaboration.apply import units


@pytest.mark.parametrize("mode,valid,expected", [("always", False, 1), ("always", True, 0), ("auto", False, 0)])
def test_replanned_plain_unit_revalidates_when_always_requested(monkeypatch, mode, valid, expected):
    asset = "/Game/Plain.Plain"
    item = dict(asset=asset, kind="blueprint", empty=False, dependencies=[],
                payload=dict(plan=[dict(op="bp_default_set", name="Enabled", value="true")]))
    batch = dict(units={asset: item}, candidate="replanned")
    workspace = SimpleNamespace(history=Mock())
    proof = dict(validation_id="new-proof")
    admit = Mock(return_value={asset: proof})
    monkeypatch.setattr(gate, "admit_batch", admit)
    monkeypatch.setattr(gate, "receipt_valid", lambda *a, **k: valid)
    monkeypatch.setattr(units.refresh, "settle", lambda *a: None)
    record = dict(id="apply")
    monkeypatch.setattr(units.transactions, "request", lambda *a: record)
    monkeypatch.setattr(units.transactions, "save", lambda *a: None)
    executed = Mock()
    monkeypatch.setattr(units.transactions, "execute", executed)
    monkeypatch.setattr(units, "publish_verified", lambda *a: "published")
    monkeypatch.setattr(units.transactions, "adopt", lambda *a: None)
    units.run_unit(None, None, workspace, asset, "source", "old", {}, dict(commit="observed"), batch,
                   dict(isolation=mode), set(), set(), {asset: dict(validation_id="old-proof")}, set())
    assert admit.call_count == expected
    executed.assert_called_once()
    if expected:
        assert record["safety_validation"] == proof
