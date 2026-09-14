"""Interrupted publication: saved receipts, rollbacks and restart reconciliation."""

from __future__ import annotations

from pathlib import Path

import pytest

from ue_node_nexus_mcp.transcode.collaboration.apply import transactions
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import text_of
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.collaboration.workspace import Workspace
from ue_node_nexus_mcp.transcode.collaboration.workspace.projection import prepare
from ue_node_nexus_mcp.transcode.sync import run_sync
from ue_node_nexus_mcp.transcode.sync_project import SyncError
from tests.transcode.fake_ue import SCHEMA_KEY
from tests.transcode.fixtures import material_raw

from .fake_bridge import ProtocolUe

MATERIAL = "/Game/Materials/M_Glass.M_Glass"
SECOND = "/Game/Materials/M_Rim.M_Rim"
CONSTANT = "G-EPS"


@pytest.fixture
def project(tmp_path):
    ue = ProtocolUe()
    second = material_raw()
    second["asset_path"] = SECOND
    ue.assets[SECOND] = second
    env = dict(UE_NEXUS_TRANSCODE_DIR=str(tmp_path / "decoded"))
    workspace = run_sync(ue, "checkout", options=dict(dry_run=False, agent_id="A"), env=env)
    return ue, env, workspace, Store(Path(workspace["files_root"]).parents[2])


def call(project, action, paths=None, bridge=None, **options):
    ue, env, workspace, _ = project
    return run_sync(bridge or ue, action, paths, dict(dict(dry_run=False, workspace_id=workspace["id"]), **options), env=env)


def edit(project, asset, old, new):
    path = Path(project[2]["file_paths"][asset])
    text = path.read_text(encoding="utf-8")
    assert old in text, (asset, text)
    path.write_text(text.replace(old, new), encoding="utf-8")


def node_value(ue, asset, guid, name):
    node = next(item for item in ue.assets[asset]["graph"]["nodes"] if item["guid"] == guid)
    return next(row["value"] for row in node["props"] if row["name"] == name)


def published_text(store, asset):
    from ue_node_nexus_mcp.transcode.collaboration.history import History

    entries = History(store).entries(store.ref("refs/ue/published"))
    return text_of(store.objects.data(entries[asset], "snapshot")) if asset in entries else None


def lose_publication(monkeypatch):
    """The process dies between UE's saved receipt and the durable publication."""
    original, lost = transactions.publish, []

    def crash(workspace, record, consume=True):
        if not lost:
            lost.append(record["id"])
            raise OSError("process lost before the receipt was published")
        return original(workspace, record, consume)

    monkeypatch.setattr(transactions, "publish", crash)
    return lost


def committed(project, monkeypatch):
    ue, env, workspace, store = project
    edit(project, MATERIAL, "Constant(R=0.000001)", "Constant(R=0.04)")
    call(project, "commit", all=True, message="A")
    lost = lose_publication(monkeypatch)
    interrupted = call(project, "push", [MATERIAL])
    assert interrupted["errors"][MATERIAL]["code"] == "publication_io_failed", interrupted
    record = store.record("apply", lost[0])
    assert record["phase"] == "ue_committed" and not record.get("published")
    assert store.ref("refs/ue/published") is None
    assert node_value(ue, MATERIAL, CONSTANT, "R") == "0.04"
    return lost[0]


def test_an_interrupted_publication_finishes_on_the_next_push(project, monkeypatch):
    ue, env, workspace, store = project
    apply_id = committed(project, monkeypatch)
    monkeypatch.undo()
    resumed = call(project, "push", [MATERIAL])
    assert resumed["status"] == "published", resumed
    assert [row["action"] for row in resumed["rows"]] == ["unchanged"]
    # The saved receipt is published, never re-executed against the editor.
    assert len(ue.applied) == 1
    assert store.record("apply", apply_id)["phase"] == "completed"
    assert "R=0.04" in published_text(store, MATERIAL)


def test_a_restarted_process_recovers_the_saved_receipt_explicitly(project, monkeypatch):
    ue, env, workspace, store = project
    apply_id = committed(project, monkeypatch)
    monkeypatch.undo()
    assert Store(store.root).record("apply", apply_id)["phase"] == "ue_committed"
    preview = call(project, "recover", apply_id=apply_id, dry_run=True)
    assert preview["phase"] == "ue_committed" and preview["asset"] == MATERIAL
    assert store.ref("refs/ue/published") is None

    recovered = call(project, "recover", apply_id=apply_id)
    assert recovered["phase"] == "completed"
    assert recovered["published"] == store.ref("refs/ue/published")
    assert len(ue.applied) == 1
    assert call(project, "recover", apply_id=apply_id)["published"] == recovered["published"]


def test_an_editor_change_after_the_receipt_becomes_a_recovery_conflict(project, monkeypatch):
    ue, env, workspace, store = project
    apply_id = committed(project, monkeypatch)
    monkeypatch.undo()
    node = next(item for item in ue.assets[MATERIAL]["graph"]["nodes"] if item["guid"] == CONSTANT)
    node["props"] = [dict(row, value="0.5") if row["name"] == "R" else row for row in node["props"]]
    with pytest.raises(SyncError) as failure:
        call(project, "recover", apply_id=apply_id)
    assert failure.value.code == "recovery_conflict"
    observation = failure.value.details["observation"]
    assert (store.root / "observations" / observation / "observation.json").is_file()
    record = store.record("apply", apply_id)
    assert record["phase"] == "recovery_conflict" and record["recovery_observation"] == observation
    assert store.ref("refs/ue/published") is None

    # The conflict is not silently overwritten by the next publication either.
    with pytest.raises(SyncError) as blocked:
        call(project, "push", [MATERIAL])
    assert blocked.value.code == "recovery_conflict"
    assert len(ue.applied) == 1


def test_a_request_that_never_reached_the_editor_is_rejected(project, monkeypatch):
    ue, env, workspace, store = project
    prepared = []

    def crash(bridge, workspace_handle, record):
        prepared.append(record["id"])
        raise OSError("process lost before the request was submitted")

    monkeypatch.setattr(transactions, "execute", crash)
    edit(project, MATERIAL, "Constant(R=0.000001)", "Constant(R=0.04)")
    call(project, "commit", all=True, message="A")
    interrupted = call(project, "push", [MATERIAL])
    assert interrupted["errors"][MATERIAL]["code"] == "publication_io_failed", interrupted
    assert store.record("apply", prepared[0])["phase"] == "prepared"
    assert ue.applied == []

    monkeypatch.undo()
    resumed = call(project, "push", [MATERIAL])
    assert resumed["status"] == "published", resumed
    abandoned = store.record("apply", prepared[0])
    assert abandoned["phase"] == "rejected"
    assert abandoned["reason"] == "interrupted before submitting to UE"
    assert len(ue.applied) == 1


def test_a_partial_batch_keeps_the_saved_asset_and_restores_the_failed_one(project):
    ue, env, workspace, store = project
    edit(project, MATERIAL, "Constant(R=0.000001)", "Constant(R=0.04)")
    edit(project, SECOND, "Constant(R=0.000001)", "Constant(R=0.07)")
    call(project, "commit", all=True, message="both")
    ue.fail_save = set([SECOND])
    result = call(project, "push", stop_on_error=False)
    assert result["status"] == "partial", result
    assert result["errors"][SECOND]["code"] == "apply_rolled_back"
    assert result["applied"] == 1
    assert node_value(ue, MATERIAL, CONSTANT, "R") == "0.04"
    assert node_value(ue, SECOND, CONSTANT, "R") == "0.000001"
    assert "R=0.04" in published_text(store, MATERIAL)
    assert "R=0.000001" in published_text(store, SECOND)
    rolled = next(item for item in store.records("apply") if item["asset"] == SECOND)
    assert rolled["phase"] == "rolled_back"

    ue.fail_save = False
    retried = call(project, "push")
    assert retried["status"] == "published", retried
    assert node_value(ue, SECOND, CONSTANT, "R") == "0.07"
    assert "R=0.07" in published_text(store, SECOND)


def test_publication_saves_an_asset_that_is_only_dirty_in_memory(project):
    ue, env, workspace, store = project
    ue.dirty.add(MATERIAL)
    result = call(project, "push", [MATERIAL])
    assert result["status"] == "published", result
    assert [row["action"] for row in result["rows"]] == ["pushed"]
    assert [item["plan"] for item in ue.applied] == [[]]
    assert MATERIAL not in ue.dirty
    assert store.record("apply", result["rows"][0]["apply_id"])["receipt"]["after"]["dirty"] is False


def test_a_rolled_back_save_keeps_the_memory_checkpoint(project):
    ue, env, workspace, store = project
    ue.dirty.add(MATERIAL)
    ue.fail_save = set([MATERIAL])
    result = call(project, "push", [MATERIAL])
    assert result["errors"][MATERIAL]["code"] == "apply_rolled_back", result
    assert MATERIAL in ue.dirty
    ue.fail_save = False
    assert call(project, "push", [MATERIAL])["status"] == "published"
    assert MATERIAL not in ue.dirty


def test_an_interrupted_worktree_update_is_finished_by_recover(project):
    ue, env, workspace, store = project
    handle = Workspace(store, workspace["id"])
    relative = handle.state["files"][MATERIAL]
    replacement = (handle.root / relative).read_text(encoding="utf-8").replace("BLEND_Translucent", "BLEND_Opaque")
    journal = prepare(handle, dict(handle.state), dict([(relative, replacement.encode())]), "pull")

    with pytest.raises(SyncError) as failure:
        call(project, "commit", all=True, message="A")
    assert failure.value.code == "projection_pending"
    assert failure.value.details["projection_ids"] == [journal["id"]]
    assert failure.value.details["recover"]["options"]["projection_id"] == journal["id"]

    preview = call(project, "recover", projection_id=journal["id"], dry_run=True)
    assert preview["phase"] == "prepared"
    assert (handle.root / relative).read_text(encoding="utf-8") != replacement

    finished = call(project, "recover", projection_id=journal["id"])
    assert finished["phase"] == "completed"
    assert (handle.root / relative).read_text(encoding="utf-8") == replacement
    assert call(project, "commit", all=True, message="A")["commit_id"]


def test_a_malformed_transport_reply_rejects_the_execution(project):
    ue, env, workspace, store = project
    edit(project, MATERIAL, "Constant(R=0.000001)", "Constant(R=0.04)")
    call(project, "commit", all=True, message="A")

    def garbled(operation, payload, **kwargs):
        return "not an object" if operation == "transcode_apply" else ue.call(operation, payload, **kwargs)

    result = call(project, "push", [MATERIAL], bridge=garbled)
    assert result["errors"][MATERIAL]["code"] == "protocol_mismatch", result
    rejected = store.record("apply", result["errors"][MATERIAL]["details"]["apply_id"])
    assert rejected["phase"] == "rejected" and rejected["receipt"] is None
    assert ue.applied == []
    assert call(project, "push", [MATERIAL])["status"] == "published"


def record_for(store, workspace_id):
    handle = Workspace(store, workspace_id)
    handle.schema = None
    return handle, dict(id="apply-1", asset=MATERIAL, request_digest="digest-1")


@pytest.mark.parametrize("receipt,code", [
    (dict(apply_id="apply-2"), "receipt_invalid"),
    (dict(apply_id="apply-1", request_digest="digest-2"), "receipt_invalid"),
    (dict(apply_id="apply-1", after=dict(asset_path=SECOND)), "receipt_invalid"),
])
def test_a_receipt_for_another_request_is_never_accepted(project, receipt, code):
    ue, env, workspace, store = project
    handle, record = record_for(store, workspace["id"])
    with pytest.raises(SyncError) as failure:
        transactions.verify(handle, record, receipt)
    assert failure.value.code == code
    assert failure.value.details["apply_id"] == "apply-1"


def test_a_receipt_from_another_schema_environment_is_stale(project):
    ue, env, workspace, store = project
    handle, record = record_for(store, workspace["id"])
    from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock

    handle.schema = SchemaLock(store.root, SCHEMA_KEY)
    matching = dict(apply_id="apply-1", after=dict(asset_path=MATERIAL, schema_key=SCHEMA_KEY))
    assert transactions.verify(handle, record, matching) is matching
    with pytest.raises(SyncError) as failure:
        transactions.verify(handle, record, dict(apply_id="apply-1", after=dict(asset_path=MATERIAL, schema_key="5.6.0-other")))
    assert failure.value.code == "schema_stale"
    assert failure.value.details["received"] == "5.6.0-other"
    assert transactions.verify(handle, record, "not a receipt") is None


def test_a_non_object_reply_is_wrapped_as_a_protocol_error():
    assert transactions.envelope("nope")["error"]["code"] == "protocol_mismatch"
    assert transactions.envelope(dict(ok=True)) == dict(ok=True)
