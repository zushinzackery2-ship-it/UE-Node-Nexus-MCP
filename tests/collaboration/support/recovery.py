"""Shared fixtures used across the regression suites."""

from __future__ import annotations
from pathlib import Path
import pytest
from ue_node_nexus_mcp.transcode.collaboration.apply import transactions
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import text_of
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.storage.io import digest
from ue_node_nexus_mcp.transcode.collaboration.workspace import Workspace
from ue_node_nexus_mcp.transcode.sync.service import run_sync
from tests.transcode.fixtures import material_raw
from ..fake_bridge import ProtocolUe


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


def record_for(store, workspace_id):
    handle = Workspace(store, workspace_id)
    handle.schema = None
    request = dict(asset_path=MATERIAL, apply_id="apply-1")
    return handle, dict(id="apply-1", asset=MATERIAL, request=request, operation="transcode_apply", request_digest=digest(request))
