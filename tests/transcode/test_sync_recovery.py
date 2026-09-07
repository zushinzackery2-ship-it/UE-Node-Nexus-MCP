from pathlib import Path

import pytest

from ue_node_nexus_mcp.transcode.emitter import emit
from ue_node_nexus_mcp.transcode.parser import parse
from ue_node_nexus_mcp.transcode.paths import base_path
from ue_node_nexus_mcp.transcode.sync import run_sync

MF = "/Game/WaterStains/Functions/MF_WS_S.MF_WS_S"
MAT = "/Game/Materials/M_Glass.M_Glass"


def _edit_function(project: Path) -> Path:
    path = project / "WaterStains/Functions/MF_WS_S.mf.nexus"
    path.write_text(
        path.read_text(encoding="utf-8").replace("Constant(R=0.000001)", "Constant(R=0.5)"),
        encoding="utf-8",
    )
    return path


def test_partial_failure_preserves_text_base_and_pending_export(sync_workspace):
    ue, env, project = sync_workspace
    path = _edit_function(project)
    intended = path.read_bytes()
    base = base_path(project, MF).read_bytes()
    ue.fail_apply_index = 0

    report = run_sync(ue, "push", [MF], dict(dry_run=False), env)

    assert report["error_count"] == 1
    assert path.read_bytes() == intended
    assert base_path(project, MF).read_bytes() == base
    assert list((project / ".nexus/pending").rglob("*.json"))


def test_force_local_compares_with_live_asset_even_when_only_ue_changed(sync_workspace):
    ue, env, project = sync_workspace
    for prop in ue.assets[MAT]["props"]:
        if prop["name"] == "BlendMode":
            prop["value"] = "BLEND_Opaque"
    ue.assets[MAT]["saved_hash"] = "editor-edit"

    report = run_sync(ue, "push", [MAT], dict(dry_run=False, force="local"), env)

    assert report["counts"] == dict(pushed=1), report
    assert any(
        verb.get("name") == "BlendMode" and verb.get("value") == "BLEND_Translucent"
        for verb in ue.applied[-1]["plan"]
    )
    assert run_sync(ue, "status", [MAT], env=env)["counts"] == dict(clean=1)


def test_function_refresh_keeps_unselected_callers_local_edits(sync_workspace):
    ue, env, project = sync_workspace
    ue.referencers[MF] = [MAT.rsplit(".", 1)[0]]
    function = project / "WaterStains/Functions/MF_WS_S.mf.nexus"
    function.write_text(
        function.read_text(encoding="utf-8").replace("InputName=B", "InputName=Bee"),
        encoding="utf-8",
    )
    caller = project / "Materials/M_Glass.mat.nexus"
    caller.write_text(
        caller.read_text(encoding="utf-8").replace("BLEND_Translucent", "BLEND_Opaque"),
        encoding="utf-8",
    )
    intended = caller.read_bytes()
    base = base_path(project, MAT).read_bytes()

    report = run_sync(ue, "push", [MF], dict(dry_run=False), env)

    assert report["error_count"] == 0, report
    assert caller.read_bytes() == intended
    assert base_path(project, MAT).read_bytes() == base


def test_status_exposes_dirty_separately_from_saved_changes(sync_workspace):
    ue, env, project = sync_workspace
    ue.dirty.add(MAT)

    report = run_sync(ue, "status", [MAT], env=env)

    row = dict(zip(report["columns"], report["rows"][0]))
    assert row["ue_dirty"] is True
    assert row["ue_saved_changed"] is False


def test_retry_uses_live_state_and_preserves_created_node_ids(sync_workspace):
    ue, env, project = sync_workspace
    initial_nodes = len(ue.assets[MF]["graph"]["nodes"])
    path = _edit_function(project)
    path.write_text(
        path.read_text(encoding="utf-8") + "fresh : Multiply(ConstB=2) @ 0,0\nconstant -> fresh.A\n",
        encoding="utf-8",
    )
    ue.fail_apply_index = 2
    failed = run_sync(ue, "push", [MF], dict(dry_run=False), env)
    assert failed["error_count"] == 1, failed
    assert len(ue.assets[MF]["graph"]["nodes"]) == initial_nodes + 1
    ue.fail_apply_index = None

    report = run_sync(ue, "push", [MF], dict(dry_run=False, force="local"), env)

    assert report["error_count"] == 0, report
    assert [verb["op"] for verb in ue.applied[-1]["plan"]] == ["connect_pins"]
    assert ue.applied[-1]["ids"]["fresh"] == "G-NEW-1"
    assert len(ue.assets[MF]["graph"]["nodes"]) == initial_nodes + 1
    assert not list((project / ".nexus/pending").rglob("*.json"))
    assert run_sync(ue, "status", [MF], env=env)["counts"] == dict(clean=1)


@pytest.mark.parametrize("failure", ["compile", "save", "export", "transport"])
def test_failures_preserve_intent_and_accepted_state(sync_workspace, failure):
    ue, env, project = sync_workspace
    path = _edit_function(project)
    intended = path.read_bytes()
    base = base_path(project, MF).read_bytes()
    state = (project / ".nexus/state.json").read_bytes()

    def bridge(operation, payload):
        if operation != "transcode_apply":
            return ue(operation, payload)
        if failure == "transport":
            raise OSError("pipe disconnected")
        response = ue(operation, payload)
        data = response["data"]
        if failure == "compile":
            data["compile"] = dict(ran=True, ok=False, error_count=0)
        elif failure == "save":
            data.update(saved=False, changed=True, save_error="read-only package")
        else:
            data["file"] = ""
        return response

    report = run_sync(bridge, "push", [MF], dict(dry_run=False), env)

    assert report["error_count"] == 1, report
    assert path.read_bytes() == intended
    assert base_path(project, MF).read_bytes() == base
    assert (project / ".nexus/state.json").read_bytes() == state
    assert list((project / ".nexus/pending").rglob("*.push.json"))


def test_edit_during_apply_is_preserved(sync_workspace):
    ue, env, project = sync_workspace
    path = _edit_function(project)
    intended = path.read_text(encoding="utf-8").replace("R=0.5", "R=0.75")

    def bridge(operation, payload):
        response = ue(operation, payload)
        if operation == "transcode_apply":
            path.write_text(intended, encoding="utf-8")
        return response

    report = run_sync(bridge, "push", [MF], dict(dry_run=False), env)

    assert report["error_count"] == 1
    assert "local_changed_during_push" in report["diagnostics"][0]
    assert path.read_text(encoding="utf-8") == intended


def test_layout_only_dry_run_keeps_text_base_and_state_unchanged(sync_workspace):
    ue, env, project = sync_workspace
    path = project / "WaterStains/Functions/MF_WS_S.mf.nexus"
    document, sink = parse(path.read_text(encoding="utf-8"))
    assert not sink.has_errors
    graph = document.section("graph")
    graph.entries[0], graph.entries[1] = graph.entries[1], graph.entries[0]
    path.write_text(emit(document), encoding="utf-8")
    before = dict((file, file.read_bytes()) for file in project.rglob("*") if file.is_file())

    report = run_sync(ue, "push", [MF], env=env)

    assert report["counts"] == dict(unchanged=1), report
    assert before == dict((file, file.read_bytes()) for file in project.rglob("*") if file.is_file())
    assert ue.applied == []


def test_force_local_restores_values_unchanged_locally_but_changed_in_ue(sync_workspace):
    ue, env, project = sync_workspace
    path = _edit_function(project)
    ue.assets[MF]["graph"]["nodes"][0]["x"] = 900
    ue.assets[MF]["saved_hash"] = "editor-moved-node"

    report = run_sync(ue, "push", [MF], dict(dry_run=False, force="local"), env)

    assert report["error_count"] == 0, report
    assert any(verb["op"] == "set_node_position" for verb in ue.applied[-1]["plan"])
    assert "900" not in path.read_text(encoding="utf-8")
