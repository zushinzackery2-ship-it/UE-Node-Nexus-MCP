from pathlib import Path

import pytest

from ue_node_nexus_mcp.transcode.paths import base_path
from ue_node_nexus_mcp.transcode.sync import run_sync

MF = "/Game/WaterStains/Functions/MF_WS_S.MF_WS_S"


@pytest.mark.parametrize("target", ["text", "base", "state"])
def test_commit_error_restores_text_base_and_state(sync_workspace, monkeypatch, target):
    ue, env, project = sync_workspace
    file = project / "WaterStains/Functions/MF_WS_S.mf.nexus"
    file.write_text(file.read_text(encoding="utf-8").replace("R=0.000001", "R=0.5000"), encoding="utf-8")
    paths = dict(text=file, base=base_path(project, MF), state=project / ".nexus/state.json")
    before = dict((path, path.read_bytes()) for path in paths.values())
    replace = Path.replace

    def fail_once(path, destination):
        if Path(destination) == paths[target]:
            monkeypatch.setattr(Path, "replace", replace)
            raise OSError("simulated disk write failure")
        return replace(path, destination)

    monkeypatch.setattr(Path, "replace", fail_once)
    report = run_sync(ue, "push", [MF], dict(dry_run=False), env)

    assert report["error_count"] == 1, report
    assert "mirror_commit_failed" in report["diagnostics"][0]
    assert before == dict((path, path.read_bytes()) for path in paths.values())
    assert not list(project.rglob("*.push-tmp"))


def test_pull_conflict_preserves_accepted_base_and_local_file(sync_workspace):
    ue, env, project = sync_workspace
    file = project / "WaterStains/Functions/MF_WS_S.mf.nexus"
    file.write_text(file.read_text(encoding="utf-8").replace("R=0.000001", "R=0.5"), encoding="utf-8")
    intended = file.read_bytes()
    base = base_path(project, MF).read_bytes()
    ue.assets[MF]["saved_hash"] = "editor-change"

    report = run_sync(ue, "pull", [MF], env=env)

    assert report["counts"] == dict(conflict=1)
    assert file.read_bytes() == intended
    assert base_path(project, MF).read_bytes() == base
    assert file.with_name("MF_WS_S.mf.ue.nexus").is_file()


def test_header_must_match_selected_mirror_file(sync_workspace):
    ue, env, project = sync_workspace
    file = project / "WaterStains/Functions/MF_WS_S.mf.nexus"
    file.write_text(file.read_text(encoding="utf-8").replace("asset: /Game/WaterStains/Functions/MF_WS_S", "asset: /Game/Other"), encoding="utf-8")

    report = run_sync(ue, "push", [MF], dict(dry_run=False), env)

    assert report["error_count"] == 1
    assert "asset_path_mismatch" in report["diagnostics"][0]
    assert ue.applied == []
