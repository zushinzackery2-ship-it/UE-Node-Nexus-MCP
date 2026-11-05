"""Completed batch proofs survive a later semantic rejection without new workers."""

from copy import deepcopy

import pytest

from ue_node_nexus_mcp.safety.publication import gate
from ue_node_nexus_mcp.transcode.collaboration.apply import units
from ue_node_nexus_mcp.transcode.errors import SyncError
from tests.safety.test_batch_admission import batch_fixture


@pytest.mark.parametrize("stop_on_error", [True, False])
def test_later_readback_failure_stops_or_reuses_valid_independent_proofs(monkeypatch, stop_on_error):
    bridge, context, workspace, entries, observation, _, _, events, invocations = batch_fixture(monkeypatch)
    first, second = [asset for asset, _ in entries]
    third = deepcopy(entries[1][1])
    third["asset"] = third["payload"]["asset_path"] = "/Game/Third.Third"
    third["dependencies"] = [second]
    fourth = deepcopy(third)
    fourth["asset"] = fourth["payload"]["asset_path"] = "/Game/Fourth.Fourth"
    fourth["dependencies"] = [third["asset"]]
    entries.extend([(third["asset"], third), (fourth["asset"], fourth)])
    batch = dict(order=[asset for asset, _ in entries], units=dict(entries), errors={})
    checked, applied = [], []

    def readback(_workspace, proof):
        checked.append(proof["asset"])
        if proof["asset"] == second:
            raise SyncError("apply_result_mismatch", "readback differs", dict(asset=second))

    def apply(*args):
        asset, receipts = args[3], args[-2]
        assert asset == first and gate.receipt_valid(receipts[asset], batch["units"][asset], "candidate")
        applied.append(asset)
        return [dict(asset=asset, action="pushed")]

    monkeypatch.setattr(gate, "actual_snapshot", readback)
    monkeypatch.setattr(units, "run_unit", apply)
    result = units.run_units(bridge, context, workspace, "source", "candidate", {}, observation, batch,
                             dict(isolation="always", stop_on_error=stop_on_error))
    assert len(invocations) == 1
    assert checked == [asset for asset, _ in entries]
    assert applied == ([] if stop_on_error else [first])
    assert batch["errors"][second]["code"] == "apply_result_mismatch"
    assert batch["errors"][fourth["asset"]]["code"] == "dependency_failed"
    assert sum(row["action"] == "pushed" for row in result) == len(applied)
    assert events
