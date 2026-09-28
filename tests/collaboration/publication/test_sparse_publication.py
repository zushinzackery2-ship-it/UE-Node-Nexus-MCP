"""Issue 4 #2: a sparse worktree publishes what it holds, and a failure names its asset.

A push without paths from a worktree that projects one material observed, normalised
and validated all 846 assets of the project, so one unrelated Blueprint elsewhere
stopped it with an error that named no asset, file or field.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import from_raw
from ue_node_nexus_mcp.transcode.collaboration.semantic.validation import validate
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.collaboration.semantic.schema import bind
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock
from ue_node_nexus_mcp.transcode.sync.service import run_sync
from ue_node_nexus_mcp.transcode.errors import SyncError
from tests.collaboration.fake_bridge import ProtocolUe
from tests.transcode.fixtures import blueprint_raw, material_raw

MATERIAL = "/Game/Materials/M_Glass.M_Glass"
SECOND = "/Game/Materials/M_Rim.M_Rim"


@pytest.fixture
def project(tmp_path):
    ue = ProtocolUe()
    second = material_raw()
    second["asset_path"] = SECOND
    ue.assets[SECOND] = second
    env = dict(UE_NEXUS_TRANSCODE_DIR=str(tmp_path / "decoded"))
    sparse = run_sync(ue, "checkout", [MATERIAL], dict(dry_run=False, agent_id="A"), env=env)
    full = run_sync(ue, "checkout", options=dict(dry_run=False, agent_id="B"), env=env)
    return ue, env, sparse, full


def call(project, action, workspace, paths=None, **options):
    ue, env = project[0], project[1]
    return run_sync(ue, action, paths, dict(dry_run=False, workspace_id=workspace["id"], **options), env=env)


def change(workspace, asset, old, new):
    path = Path(workspace["file_paths"][asset])
    text = path.read_text(encoding="utf-8")
    assert old in text, (asset, text)
    path.write_text(text.replace(old, new), encoding="utf-8")


def exported(ue, since: int) -> set[str]:
    return set(path for operation, payload in ue.calls[since:] if operation == "transcode_export"
               for path in payload.get("asset_paths") or ())


def break_in_editor(ue, asset):
    """UE now holds ``asset`` in a state this mirror cannot even decode."""
    nodes = ue.assets[asset]["graph"]["nodes"]
    nodes.append(dict(nodes[0]))
    ue.assets[asset]["saved_hash"] = "saved-elsewhere"


def test_a_sparse_push_without_paths_publishes_only_what_the_worktree_holds(project):
    ue, env, sparse, full = project
    break_in_editor(ue, SECOND)
    change(sparse, MATERIAL, "Constant(R=0.000001)", "Constant(R=0.04)")
    call(project, "commit", sparse, all=True, message="one material")
    before = len(ue.calls)

    result = call(project, "push", sparse)

    assert result["status"] == "published", result
    assert [row["asset"] for row in result["rows"]] == [MATERIAL]
    assert SECOND not in exported(ue, before)


def test_a_change_the_branch_carries_outside_the_projection_still_reaches_the_editor(project):
    ue, env, sparse, full = project
    change(full, SECOND, "Constant(R=0.000001)", "Constant(R=0.42)")
    carried = call(project, "commit", full, all=True, message="B edits the second material")["commit_id"]
    merged = call(project, "merge", sparse, revision=carried)
    assert merged["status"] in ("merged", "fast-forward", "completed"), merged

    result = call(project, "push", sparse)

    assert result["status"] == "published", result
    assert SECOND in [row["asset"] for row in result["rows"] if row["action"] == "pushed"]
    constant = next(node for node in ue.assets[SECOND]["graph"]["nodes"] if node["class_short"] == "Constant")
    assert next(item["value"] for item in constant["props"] if item["name"] == "R") == "0.42"


def test_an_observation_failure_names_the_asset_and_the_stage(project):
    ue, env, sparse, full = project
    break_in_editor(ue, MATERIAL)
    change(sparse, MATERIAL, "Constant(R=0.000001)", "Constant(R=0.04)")
    call(project, "commit", sparse, all=True, message="one material")

    with pytest.raises(SyncError) as failure:
        call(project, "push", sparse)

    assert failure.value.code == "invalid_raw"
    assert failure.value.details["asset"] == MATERIAL
    assert failure.value.details["stage"] == "observe"


def test_an_observed_state_whose_text_no_longer_resolves_is_recorded_and_validation_names_the_line(tmp_path):
    """Recording is not validating: a shared class name is a finding against its line."""
    directory = tmp_path / ".nexus" / "schema" / "key"
    lights = ("/Game/Props/A/BP_Light.BP_Light_C", "/Game/Props/B/BP_Light.BP_Light_C")
    publish(directory, "key", dict(component=dict((path, dict(path=path, props=dict())) for path in lights)))
    lock = SchemaLock(directory, "key")
    raw = blueprint_raw()
    components = raw["blueprint"]["components"]
    components.append(dict(components[0], name="Bulb", guid="C-3", **{"class": lights[0], "class_short": "BP_Light_C"}))
    # Written before the catalog knew the second light: the text still says BP_Light_C.
    snapshot = from_raw(raw)

    recorded = bind(Store(tmp_path / "store"), snapshot, lock)

    assert recorded["schema_binding"]["schema_key"] == "key"
    findings = [item for item in validate(recorded, lock) if item["conflict_type"] == "ambiguous_class"]
    assert len(findings) == 1 and findings[0]["line"] and lights[1] in findings[0]["reason"], findings
