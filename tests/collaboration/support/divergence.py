"""Shared fixtures used across the regression suites."""

from __future__ import annotations
from pathlib import Path
import pytest
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.sync.service import run_sync
from tests.transcode.fixtures import material_raw
from ..fake_bridge import ProtocolUe


MATERIAL = "/Game/Materials/M_Glass.M_Glass"


SECOND = "/Game/Materials/M_Rim.M_Rim"


@pytest.fixture
def project(tmp_path):
    ue = ProtocolUe()
    second = material_raw()
    second["asset_path"] = SECOND
    ue.assets[SECOND] = second
    env = dict(UE_NEXUS_TRANSCODE_DIR=str(tmp_path / "decoded"))
    first = run_sync(ue, "checkout", options=dict(dry_run=False, agent_id="A"), env=env)
    other = run_sync(ue, "checkout", options=dict(dry_run=False, agent_id="B"), env=env)
    return ue, env, first, other, Store(Path(first["files_root"]).parents[2])


def call(project, action, workspace=None, paths=None, **options):
    ue, env = project[0], project[1]
    if workspace:
        options["workspace_id"] = workspace["id"]
    return run_sync(ue, action, paths, dict(dry_run=False, **options), env=env)


def change(workspace, asset, old, new):
    path = Path(workspace["file_paths"][asset])
    text = path.read_text(encoding="utf-8")
    assert old in text, (asset, text)
    path.write_text(text.replace(old, new), encoding="utf-8")


def value(ue, asset, name):
    return next(row["value"] for row in ue.assets[asset]["props"] if row["name"] == name)


def resolve_all(project, workspace, report, choice="ours"):
    for item in report["conflicts"]:
        call(project, "resolve", workspace, merge_id=report["merge_id"], conflict_id=item["conflict_id"], choice=choice)
    return call(project, "continue", workspace, merge_id=report["merge_id"])


def _graph_entity(snapshot, alias):
    graph = snapshot["semantic"]["sections"]["graph:"]
    return next(key for key, entity in graph["entities"].items() if entity["alias"] == alias)
