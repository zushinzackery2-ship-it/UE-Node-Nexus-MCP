"""Real UE receipts include operation and use a different digest from the store."""

from copy import deepcopy
from types import SimpleNamespace

import pytest

from ue_node_nexus_mcp.transcode.collaboration.apply.transactions import verify
from ue_node_nexus_mcp.transcode.collaboration.store.io import digest
from ue_node_nexus_mcp.transcode.sync_project import SyncError


def fixture():
    payload = dict(apply_id="apply-1", asset_path="/Game/M.M", save=True,
                   plan=[dict(op="set_node_param", name="R", value="0.5", line=0.0)])
    record = dict(id="apply-1", asset=payload["asset_path"], request=payload,
                  request_digest=digest(payload), operation="transcode_apply")
    request = dict(deepcopy(payload), operation=record["operation"])
    request["plan"][0]["line"] = 0
    receipt = dict(apply_id="apply-1", request=request, request_digest="25e10d90a24709c78693faa38a70d0f9",
                   phase="ue_committed", after=dict(asset_path=payload["asset_path"]))
    return SimpleNamespace(schema=None), record, receipt


def test_native_request_is_verified_without_comparing_unrelated_digests():
    workspace, record, receipt = fixture()
    assert len(receipt["request_digest"]) == 32 and len(record["request_digest"]) == 64
    assert verify(workspace, record, receipt) is receipt


@pytest.mark.parametrize("field,replacement", [
    ("operation", "vfx_transcode_apply"), ("save", 1), ("apply_id", "apply-2"),
    ("asset_path", "/Game/Other.Other"), ("plan", []), ("unexpected", True),
])
def test_changed_native_request_is_rejected(field, replacement):
    workspace, record, receipt = fixture()
    receipt["request"][field] = replacement
    with pytest.raises(SyncError, match="different request"):
        verify(workspace, record, receipt)


def test_missing_request_cannot_be_authenticated_by_an_unrelated_digest():
    workspace, record, receipt = fixture()
    receipt.pop("request")
    receipt["request_digest"] = record["request_digest"]
    with pytest.raises(SyncError, match="different request"):
        verify(workspace, record, receipt)


def test_corrupted_stored_request_cannot_accept_a_matching_corrupted_receipt():
    workspace, record, receipt = fixture()
    record["request"]["save"] = receipt["request"]["save"] = False
    with pytest.raises(SyncError, match="integrity check"):
        verify(workspace, record, receipt)
