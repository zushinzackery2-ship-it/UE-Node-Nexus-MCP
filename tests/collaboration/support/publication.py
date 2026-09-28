"""Shared fixtures used across the regression suites."""

from pathlib import Path
import pytest
from ue_node_nexus_mcp.transcode.sync.service import run_sync
from ..fake_bridge import ProtocolUe


ASSET = "/Game/Materials/M_Glass.M_Glass"


@pytest.fixture
def project(tmp_path):
    ue = ProtocolUe()
    env = dict(UE_NEXUS_TRANSCODE_DIR=str(tmp_path / "decoded"))
    first = run_sync(ue, "checkout", options=dict(dry_run=False, agent_id="A"), env=env)
    second = run_sync(ue, "checkout", options=dict(dry_run=False, agent_id="B"), env=env)
    return ue, env, first, second


def call(project, action, workspace, **options):
    ue, env, _, _ = project
    return run_sync(ue, action, options=dict(workspace_id=workspace["id"], dry_run=False, **options), env=env)


def edit(workspace, old, new):
    file = Path(workspace["file_paths"][ASSET])
    text = file.read_text(encoding="utf-8")
    assert old in text
    file.write_text(text.replace(old, new), encoding="utf-8")


def value(ue, name):
    return next(row["value"] for row in ue.assets[ASSET]["props"] if row["name"] == name)
