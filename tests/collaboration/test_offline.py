"""The complete workspace lint/stage/commit path must work without a UE call."""

from pathlib import Path

import pytest

from ue_node_nexus_mcp.transcode.sync import run_sync
from .fake_bridge import ProtocolUe

MATERIAL = "/Game/Materials/M_Glass.M_Glass"


@pytest.fixture
def workspace(tmp_path):
    env = dict(UE_NEXUS_TRANSCODE_DIR=str(tmp_path / "mirror"))
    created = run_sync(ProtocolUe(), "checkout", [MATERIAL], dict(agent_id="offline", dry_run=False), env=env)
    return created, env


def offline(operation, payload):
    raise AssertionError("offline work tried to call UE: " + operation)


def command(workspace, action, **options):
    created, env = workspace
    return run_sync(offline, action, [MATERIAL], dict(workspace_id=created["id"], dry_run=False, **options), env=env)


def replace(workspace, new):
    file = Path(workspace[0]["file_paths"][MATERIAL])
    text = file.read_text(encoding="utf-8")
    assert "Constant(R=0.000001)" in text
    file.write_text(text.replace("Constant(R=0.000001)", "Constant(R=" + new + ")"), encoding="utf-8")


def test_lint_stage_and_commit_keep_working_offline(workspace):
    replace(workspace, "0.04")
    result = command(workspace, "lint")
    assert result["error_count"] == 0 and not result["bridge_available"], result
    assert [row["asset"] for row in result["rows"]] == [MATERIAL]
    command(workspace, "stage")
    assert command(workspace, "commit", message="offline edit")["commit_id"]


def test_lint_errors_use_diagnostic_severity_and_report_the_selected_file(workspace):
    replace(workspace, "bad")
    result = command(workspace, "lint")
    assert result["error_count"] > 0 and result["diagnostics"], result
    assert result["rows"][0]["errors"] == result["error_count"]
    assert result["rows"][0]["file"] in result["diagnostics"][0]
