"""``paths`` selection for ue_sync: folders expand, typos fail, nothing passes silently.

Regression pins for a live finding: ``lint /Game/ToonShade`` checked zero files and
still reported success, because the folder was coerced to the object path
``/Game/ToonShade.ToonShade`` and the missing text was skipped without a word.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from ue_node_nexus_mcp.transcode.sync import run_sync
from ue_node_nexus_mcp.transcode.sync_project import SyncError

from .fake_ue import FakeUe
from .fixtures import material_function_raw, material_instance_raw, material_raw

MF = "/Game/WaterStains/Functions/MF_WS_S.MF_WS_S"
MAT = "/Game/Materials/M_Glass.M_Glass"
MI = "/Game/Materials/MI_Glass_Soft.MI_Glass_Soft"


@pytest.fixture
def mirror(tmp_path: Path) -> tuple[FakeUe, dict[str, str], Path]:
    ue = FakeUe({"mf": material_function_raw(), "mat": material_raw(), "mi": material_instance_raw()})
    root = tmp_path / "Content_Transcoded"
    env = {"UE_NEXUS_TRANSCODE_DIR": str(root)}
    run_sync(ue, "init", env=env)
    return ue, env, root


def _linted(report: dict) -> set[str]:
    return {row["asset"] for row in report["rows"]}


def test_game_folder_expands_to_every_mirrored_asset_below_it(mirror) -> None:
    ue, env, _ = mirror
    report = run_sync(ue, "lint", ["/Game/Materials"], env=env)
    assert _linted(report) == {MAT, MI}
    assert report["error_count"] == 0

    report = run_sync(ue, "lint", ["/Game/WaterStains"], env=env)
    assert _linted(report) == {MF}

    status = run_sync(ue, "status", ["/Game/Materials"], env=env, options={"include_clean": True})
    assert status["total"] == 2
    assert "unknown" not in status["counts"]


@pytest.mark.parametrize("spelling", [MAT, "/Game/Materials/M_Glass"])
def test_single_asset_paths_still_select_exactly_one(mirror, spelling: str) -> None:
    ue, env, _ = mirror
    assert _linted(run_sync(ue, "lint", [spelling], env=env)) == {MAT}


def test_unmirrored_game_asset_is_kept_so_pull_can_learn_it(mirror) -> None:
    ue, env, _ = mirror
    status = run_sync(ue, "status", ["/Game/Materials/M_New"], env=env, options={"include_clean": True})
    assert status["total"] == 1
    assert status["rows"][0][0] == "/Game/Materials/M_New.M_New"


def test_lint_of_unmirrored_asset_is_an_error_not_a_silent_pass(mirror) -> None:
    ue, env, _ = mirror
    report = run_sync(ue, "lint", ["/Game/Materials/M_New"], env=env)
    assert report["rows"] == []
    assert report["error_count"] == 1
    assert "not_mirrored" in report["diagnostics"][0]


def test_mirror_relative_paths_resolve_against_project_and_root(mirror) -> None:
    ue, env, root = mirror
    from_project = run_sync(ue, "lint", ["Materials/M_Glass.mat.nexus"], env=env)
    from_root = run_sync(ue, "lint", ["Shadetest/Materials/M_Glass.mat.nexus"], env=env)
    absolute = run_sync(ue, "lint", [str(root / "Shadetest" / "Materials")], env=env)
    assert _linted(from_project) == {MAT}
    assert _linted(from_root) == {MAT}
    assert _linted(absolute) == {MAT, MI}


def test_nonexistent_file_path_is_rejected(mirror) -> None:
    ue, env, _ = mirror
    with pytest.raises(SyncError) as excinfo:
        run_sync(ue, "lint", ["Shadetest/Materials/M_Typo.mat.nexus"], env=env)
    assert excinfo.value.code == "invalid_path"


def test_empty_folder_selection_is_rejected(mirror, tmp_path: Path) -> None:
    ue, env, root = mirror
    empty = root / "Shadetest" / "Nothing"
    empty.mkdir()
    with pytest.raises(SyncError) as excinfo:
        run_sync(ue, "lint", [str(empty)], env=env)
    assert excinfo.value.code == "no_match"
