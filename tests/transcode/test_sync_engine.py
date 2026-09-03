from __future__ import annotations

from pathlib import Path

import pytest

from ue_node_nexus_mcp.transcode.sync import run_sync
from ue_node_nexus_mcp.transcode.sync_project import SyncError

from .fake_ue import SCHEMA_KEY, FakeUe
from .fixtures import material_instance_raw, material_raw, material_function_raw


@pytest.fixture
def mirror(tmp_path: Path) -> tuple[FakeUe, dict[str, str], Path]:
    ue = FakeUe({"mf": material_function_raw(), "mat": material_raw(), "mi": material_instance_raw()})
    ue.referencers["/Game/WaterStains/Functions/MF_WS_S.MF_WS_S"] = ["/Game/Materials/M_Glass"]
    root = tmp_path / "Content_Transcoded"
    env = {"UE_NEXUS_TRANSCODE_DIR": str(root)}
    return ue, env, root


def test_init_pulls_everything_and_writes_schema(mirror) -> None:
    ue, env, root = mirror
    report = run_sync(ue, "init", env=env)
    assert report["schema_key"] == SCHEMA_KEY
    assert (root / ".nexus" / "schema" / SCHEMA_KEY / "key.json").is_file()
    project = root / "Shadetest"
    assert (project / "WaterStains" / "Functions" / "MF_WS_S.mf.nexus").is_file()
    assert (project / "Materials" / "M_Glass.mat.nexus").is_file()
    assert (project / "Materials" / "MI_Glass_Soft.mi.nexus").is_file()
    assert (project / ".nexus" / "base" / "Materials" / "M_Glass.json").is_file()
    assert not list((project / ".nexus" / "pending").rglob("*.json"))
    assert report["counts"] == {"pulled": 3}
    assert ue.root == str(root)


def test_status_after_init_is_clean_and_tracks_local_edits(mirror) -> None:
    ue, env, root = mirror
    run_sync(ue, "init", env=env)
    status = run_sync(ue, "status", env=env)
    assert status["counts"] == {"clean": 3}
    assert status["rows"] == []
    mf = root / "Shadetest" / "WaterStains" / "Functions" / "MF_WS_S.mf.nexus"
    mf.write_text(mf.read_text(encoding="utf-8").replace("Constant(R=0.000001)", "Constant(R=0.000002)"), encoding="utf-8")
    status = run_sync(ue, "status", env=env)
    assert status["counts"] == {"clean": 2, "local-modified": 1}
    ue.dirty.add("/Game/Materials/M_Glass.M_Glass")
    status = run_sync(ue, "status", env=env)
    assert status["counts"] == {"clean": 1, "local-modified": 1, "ue-modified": 1}


def test_lint_reports_unknown_class_with_file_line(mirror) -> None:
    ue, env, root = mirror
    run_sync(ue, "init", env=env)
    mf = root / "Shadetest" / "WaterStains" / "Functions" / "MF_WS_S.mf.nexus"
    mf.write_text(mf.read_text(encoding="utf-8") + "bogus : NoSuchNode(Foo=1) @ 0,0\n", encoding="utf-8")
    report = run_sync(ue, "lint", env=env)
    assert report["error_count"] == 1
    assert report["diagnostics"][0].startswith("WaterStains/Functions/MF_WS_S.mf.nexus:")
    assert "unknown_class" in report["diagnostics"][0]


def test_push_dry_run_then_apply_and_normalize(mirror) -> None:
    ue, env, root = mirror
    run_sync(ue, "init", env=env)
    mf = root / "Shadetest" / "WaterStains" / "Functions" / "MF_WS_S.mf.nexus"
    text = mf.read_text(encoding="utf-8")
    text = text.replace("Constant(R=0.000001)", "Constant(R=0.000002)")
    text = text.replace("constant -> add.B", "mul : Multiply(ConstB=2.000) @ -600,200\nconstant -> add.B\nadd -> mul.A")
    mf.write_text(text, encoding="utf-8")

    dry = run_sync(ue, "push", env=env)
    assert dry["dry_run"] is True
    assert dry["counts"] == {"planned": 1, "skipped": 2}
    assert dry["plans"][0]["counts"] == {"connect_pins": 1, "create_node": 1, "set_node_param": 1}
    assert ue.applied == []

    real = run_sync(ue, "push", env=env, options={"dry_run": False})
    assert real["counts"] == {"pushed": 1, "skipped": 2}, real
    assert real["error_count"] == 0
    payload = ue.applied[0]
    assert payload["asset_path"] == "/Game/WaterStains/Functions/MF_WS_S.MF_WS_S"
    assert payload["ids"]["constant"] == "G-EPS"
    assert [verb["op"] for verb in payload["plan"]] == ["set_node_param", "create_node", "connect_pins"]
    assert payload["dry_run"] is False and payload["compile"] is True and payload["save"] is True
    # the new node keeps the id the agent wrote and the file was rewritten in canonical form
    after = mf.read_text(encoding="utf-8")
    assert "mul" in after and "Multiply(ConstB=2)" in after
    assert real.get("normalized_files") == ["WaterStains/Functions/MF_WS_S.mf.nexus"]
    status = run_sync(ue, "status", env=env)
    assert status["counts"] == {"clean": 3}, status
    # pushing again is a no-op
    again = run_sync(ue, "push", env=env, options={"dry_run": False})
    assert again["counts"] == {"skipped": 3}
    assert len(ue.applied) == 1


def test_interface_change_refreshes_callers(mirror) -> None:
    ue, env, root = mirror
    run_sync(ue, "init", env=env)
    mf = root / "Shadetest" / "WaterStains" / "Functions" / "MF_WS_S.mf.nexus"
    mf.write_text(mf.read_text(encoding="utf-8").replace("InputName=B", "InputName=Bee"), encoding="utf-8")
    report = run_sync(ue, "push", env=env, options={"dry_run": False})
    assert report["counts"] == {"pushed": 1, "skipped": 2}
    assert report["refreshed_callers"] == ["/Game/Materials/M_Glass.M_Glass"]
    refresh = [payload for payload in ue.applied if payload["asset_path"] == "/Game/Materials/M_Glass.M_Glass"]
    assert refresh and refresh[0]["plan"] == [{"op": "refresh_function_calls", "function": "/Game/WaterStains/Functions/MF_WS_S.MF_WS_S"}]
    # the caller was re-pulled so status stays clean
    assert run_sync(ue, "status", env=env)["counts"] == {"clean": 3}


def test_conflict_is_refused_without_force(mirror) -> None:
    ue, env, root = mirror
    run_sync(ue, "init", env=env)
    mat = root / "Shadetest" / "Materials" / "M_Glass.mat.nexus"
    mat.write_text(mat.read_text(encoding="utf-8").replace("BLEND_Translucent", "BLEND_Opaque"), encoding="utf-8")
    ue.assets["/Game/Materials/M_Glass.M_Glass"]["saved_hash"] = "changed-in-editor"
    status = run_sync(ue, "status", env=env)
    assert status["counts"]["both-modified"] == 1
    push = run_sync(ue, "push", env=env, options={"dry_run": False})
    assert push["rows"][0]["action"] == "conflict" if push["rows"][0]["asset"].endswith("M_Glass.M_Glass") else True
    assert ue.applied == []
    pull = run_sync(ue, "pull", env=env)
    assert any(row["action"] == "conflict" for row in pull["rows"])
    assert (root / "Shadetest" / "Materials" / "M_Glass.mat.ue.nexus").is_file()
    forced = run_sync(ue, "push", env=env, options={"dry_run": False, "force": "local"})
    assert forced["counts"].get("pushed") == 1


def test_new_asset_from_text_is_created(mirror) -> None:
    ue, env, root = mirror
    run_sync(ue, "init", env=env)
    new = root / "Shadetest" / "Materials" / "M_New.mat.nexus"
    new.write_text(
        "nexus: 1\nasset: /Game/Materials/M_New\nclass: Material\nschema: " + SCHEMA_KEY + "\n\n[asset]\nBlendMode = BLEND_Translucent\n\n[graph]\nc : Constant(R=1) @ 0,0\n\nc -> out.BaseColor\n",
        encoding="utf-8",
    )
    status = run_sync(ue, "status", env=env)
    assert status["counts"]["local-new"] == 1
    report = run_sync(ue, "push", env=env, options={"dry_run": False})
    assert report["counts"].get("pushed") == 1, report
    created = [payload for payload in ue.applied if payload["asset_path"] == "/Game/Materials/M_New.M_New"][0]
    assert created["create"] is True and created["asset_class"] == "Material"
    assert [verb["op"] for verb in created["plan"]] == ["set_asset_prop", "create_node", "connect_pins"]
    assert "/Game/Materials/M_New.M_New" in ue.assets


def test_apply_failure_maps_back_to_file_line(mirror) -> None:
    ue, env, root = mirror
    run_sync(ue, "init", env=env)
    mf = root / "Shadetest" / "WaterStains" / "Functions" / "MF_WS_S.mf.nexus"
    mf.write_text(mf.read_text(encoding="utf-8").replace("Constant(R=0.000001)", "Constant(R=0.5)"), encoding="utf-8")
    ue.fail_apply_index = 0
    report = run_sync(ue, "push", env=env, options={"dry_run": False})
    row = next(row for row in report["rows"] if row["asset"].endswith("MF_WS_S.MF_WS_S"))
    assert row["action"] == "pushed-with-errors"
    assert report["error_count"] == 1
    assert report["diagnostics"][0].startswith("WaterStains/Functions/MF_WS_S.mf.nexus:")
    assert "simulated" in report["diagnostics"][0]


def test_offline_lint_and_status_without_bridge(mirror) -> None:
    ue, env, root = mirror
    run_sync(ue, "init", env=env)

    def offline(operation: str, payload: dict) -> dict:
        return {"ok": False, "operation": operation, "error": {"code": "mcp_bridge_error", "message": "no pipe"}, "diagnostics": [], "warnings": []}

    status = run_sync(offline, "status", env=env)
    assert status["bridge_available"] is False and status["ue_known"] is False
    assert status["counts"] == {"unknown": 3}
    lint = run_sync(offline, "lint", env=env)
    assert lint["error_count"] == 0 and lint["ok_files"] == 3
    with pytest.raises(SyncError):
        run_sync(offline, "pull", env=env)
