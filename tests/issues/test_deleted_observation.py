"""Deleted assets may remain in the registry while an export confirms absence."""

from pathlib import Path
from types import SimpleNamespace

import pytest

from ue_node_nexus_mcp.transcode.collaboration.apply.observe import capture
from ue_node_nexus_mcp.transcode.collaboration.apply.transactions import request
from ue_node_nexus_mcp.transcode.collaboration.history import History
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.sync_project import SyncError
from tests.collaboration.fake_bridge import ProtocolUe


@pytest.fixture
def deleted(tmp_path):
    ue = ProtocolUe()
    store = Store(tmp_path / "repository")
    context = SimpleNamespace(root=tmp_path, schema=None, schema_key=ue.schema_key, bridge_available=True)
    asset = next(path for path, raw in ue.assets.items() if raw["kind"] == "material")
    capture(ue, context, store, [asset])
    raw = ue.assets.pop(asset)
    return ue, context, store, asset, raw


def stale_registry(ue, asset, raw, kind):
    original = ue.op_transcode_status

    def status(payload):
        data = original(payload)
        data["assets"].append([asset, raw["class"], kind, raw.get("saved_hash", ""), False])
        return data

    ue.op_transcode_status = status


@pytest.mark.parametrize("kind,operation", [("material", "transcode_export"), ("niagara_system", "vfx_transcode_export")])
def test_exported_absence_removes_old_snapshot_and_guards_retry(deleted, kind, operation):
    ue, context, store, asset, raw = deleted
    stale_registry(ue, asset, raw, kind)
    setattr(ue, "op_" + operation, lambda payload: dict(assets=[], skipped=[dict(asset_path=asset, reason="asset_not_found")]))
    observation = capture(ue, context, store, [asset], fresh=[asset])
    history = History(store)
    assert asset in observation["measured"]
    assert asset not in observation["raw"]
    assert asset not in observation["revisions"]
    assert asset not in history.entries(observation["commit"])
    assert asset not in store.record("memory", "observed")["entries"]
    workspace = SimpleNamespace(store=store, history=history, state=dict(id="retry"))
    unit = dict(asset=asset, kind=kind, dependencies=[], payload=dict(asset_path=asset, kind=kind, create=True))
    commit = observation["commit"]
    record = request(workspace, unit, commit, commit, commit, observation, dict())
    assert record["request"]["expected_absent"] is True
    assert "expected_revision" not in record["request"]
    assert Path(store.root / "transactions" / record["id"] / "apply.json").exists()


@pytest.mark.parametrize("reason", ["unsupported_kind", "not_niagara", "use_vfx_transcode_export"])
def test_other_export_failures_cannot_authorize_recreation(deleted, reason):
    ue, context, store, asset, raw = deleted
    stale_registry(ue, asset, raw, "material")
    ue.op_transcode_export = lambda payload: dict(assets=[], skipped=[dict(asset_path=asset, reason=reason)])
    previous = store.ref("refs/ue/observed")
    with pytest.raises(SyncError) as error:
        capture(ue, context, store, [asset], fresh=[asset])
    assert error.value.code == "observation_failed"
    assert store.ref("refs/ue/observed") == previous
