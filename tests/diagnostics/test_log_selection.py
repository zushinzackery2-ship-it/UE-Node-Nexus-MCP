"""History belongs to the selected process even when another process logs later."""

import os

import pytest

from ue_node_nexus_mcp.diagnostics_logs import read_latest_project_log


@pytest.mark.parametrize("syntax", ['-abslog="{path}"', '"-abslog={path}"', '-abslog={path}'])
def test_declared_log_beats_newer_project_log(tmp_path, syntax):
    saved = tmp_path / "Saved"
    logs = saved / "Logs"
    logs.mkdir(parents=True)
    chosen = saved / "Selected.log"
    chosen.write_text("selected process", encoding="utf-8")
    newer = logs / "Other.log"
    newer.write_text("another process", encoding="utf-8")
    os.utime(newer, (chosen.stat().st_mtime + 10,) * 2)
    text, path = read_latest_project_log(dict(data=dict(project_saved_dir=str(saved),
                         command_line=syntax.format(path=chosen.as_posix()))))
    assert text == "selected process" and path == chosen


def test_quoted_whole_argument_with_spaces(tmp_path):
    chosen = tmp_path / "Some Folder/Selected.log"
    chosen.parent.mkdir()
    chosen.write_text("exact process", encoding="utf-8")
    text, path = read_latest_project_log(dict(data=dict(project_saved_dir=str(tmp_path),
                         command_line=f'"-abslog={chosen.as_posix()}"')))
    assert text == "exact process" and path == chosen


def test_missing_declared_log_does_not_read_another_process(tmp_path):
    logs = tmp_path / "Logs"
    logs.mkdir()
    (logs / "Other.log").write_text("wrong process", encoding="utf-8")
    chosen = tmp_path / "Missing.log"
    text, path = read_latest_project_log(dict(data=dict(project_saved_dir=str(tmp_path),
                         command_line=f'-abslog="{chosen.as_posix()}"')))
    assert text == "" and path == chosen
