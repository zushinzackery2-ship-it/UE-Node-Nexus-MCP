"""Legacy mirrors preserve tracked assets while excluding tool candidates."""

import pytest

from ue_node_nexus_mcp.transcode.errors import SyncError
from ue_node_nexus_mcp.transcode.sync.service import run_sync

from .test_sync_selection import MAT, MI, mirror as mirror


@pytest.mark.parametrize("directory", [".auto-index-mcp", "Materials/.cache", "Backups"])
def test_legacy_status_and_lint_exclude_metadata(mirror, directory):
    ue, env, root = mirror
    project = root / "Shadetest"
    cache = project / directory
    cache.mkdir(parents=True)
    (cache / "Bad.mat.nexus").write_text("invalid tool data", encoding="utf-8")
    if directory == "Backups":
        (project / ".nexusignore").write_text("Backups/\n", encoding="utf-8")
    report = run_sync(ue, "status", options=dict(include_clean=True), env=env)
    assert report["total"] == 3 and report["counts"] == dict(clean=3)
    assert run_sync(ue, "lint", env=env)["error_count"] == 0
    assert run_sync(ue, "push", env=env)["error_count"] == 0


def test_legacy_ignore_rules_do_not_make_tracked_files_look_deleted(mirror):
    ue, env, root = mirror
    project = root / "Shadetest"
    (project / ".nexusignore").write_text("*.nexus\n", encoding="utf-8")
    report = run_sync(ue, "status", options=dict(include_clean=True), env=env)
    assert report["counts"] == dict(clean=3)
    linted = run_sync(ue, "lint", ["Materials"], env=env)
    assert set(row["asset"] for row in linted["rows"]) == set([MAT, MI])
    assert linted["error_count"] == 0
    file = project / "Materials/M_Glass.mat.nexus"
    file.write_text(file.read_text(encoding="utf-8").replace("BaseColor", "Metallic"), encoding="utf-8")
    pushed = run_sync(ue, "push", ["Materials"], env=env)
    assert pushed["error_count"] == 0
    assert any(row["asset"] == MAT and row["action"] == "planned" for row in pushed["rows"])


def test_legacy_explicit_file_selection_respects_ignore(mirror):
    ue, env, root = mirror
    project = root / "Shadetest"
    cache = project / "Backups"
    cache.mkdir()
    file = cache / "Bad.mat.nexus"
    file.write_text("invalid tool data", encoding="utf-8")
    (project / ".nexusignore").write_text("Backups/\n", encoding="utf-8")
    with pytest.raises(SyncError) as failure:
        run_sync(ue, "lint", [str(file)], env=env)
    assert failure.value.code == "path_ignored"
