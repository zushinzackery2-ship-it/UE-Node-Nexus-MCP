"""Excluded trees cost one directory entry, never a recursive enumeration."""

from pathlib import Path

import pytest

from ue_node_nexus_mcp.transcode.errors import SyncError
from ue_node_nexus_mcp.transcode.storage.ignore import walk
from ue_node_nexus_mcp.transcode.storage.paths import iter_text_files


def test_ignored_directories_are_pruned_before_any_access(tmp_path, monkeypatch):
    for name in (".auto-index-mcp", "Backups", "Materials"):
        directory = tmp_path / name
        directory.mkdir()
        (directory / "Main.mat.nexus").write_text("fixture", encoding="utf-8")
    (tmp_path / ".nexusignore").write_text("Backups/\n", encoding="utf-8")
    scanned = []
    original = walk.os.scandir

    def guarded(path):
        path = Path(path)
        assert path.name not in (".auto-index-mcp", "Backups")
        scanned.append(path)
        return original(path)

    monkeypatch.setattr(walk.os, "scandir", guarded)
    assert iter_text_files(tmp_path) == [tmp_path / "Materials/Main.mat.nexus"]
    assert set(scanned) == set([tmp_path, tmp_path / "Materials"])


def test_nonignored_scan_errors_are_visible(tmp_path, monkeypatch):
    original = walk.os.scandir
    directory = tmp_path / "Materials"
    directory.mkdir()

    def denied(path):
        if Path(path) == directory:
            raise PermissionError(13, "fixture access denied", str(path))
        return original(path)

    monkeypatch.setattr(walk.os, "scandir", denied)
    with pytest.raises(SyncError) as failure:
        iter_text_files(tmp_path)
    assert failure.value.code == "mirror_scan_failed"


def test_scan_logs_show_pruning_and_candidate_counts(tmp_path, caplog):
    caplog.set_level("DEBUG", logger="ue_nexus.discovery")
    (tmp_path / ".cache").mkdir()
    (tmp_path / "Main.mat.nexus").write_text("fixture", encoding="utf-8")
    assert iter_text_files(tmp_path) == [tmp_path / "Main.mat.nexus"]
    assert "pruned ignored directory" in caplog.text
    assert "visited=1 ignored_directories=1 ignored_files=0 candidates=1" in caplog.text
