"""Ignore rules are relative to the authored files root and prune directories."""

import pytest

from ue_node_nexus_mcp.transcode.errors import SyncError
from ue_node_nexus_mcp.transcode.storage.paths import iter_text_files


@pytest.mark.parametrize(("rules", "ignored", "included"), [
    ("Cache/\n", "Cache/Backup.mat.nexus", "Materials/Main.mat.nexus"),
    ("Cache/\n", "Materials/Cache/Backup.mat.nexus", "Materials/Main.mat.nexus"),
    ("/Cache/\n", "Cache/Backup.mat.nexus", "Materials/Cache/Main.mat.nexus"),
    ("Materials/Cache/\n", "Materials/Cache/Backup.mat.nexus", "Other/Materials/Cache/Main.mat.nexus"),
    ("*.mat.nexus\n!Main.mat.nexus\n", "Materials/Backup.mat.nexus", "Materials/Main.mat.nexus"),
    ("**/Backup.mat.nexus\n", "Materials/Backup.mat.nexus", "Materials/Main.mat.nexus"),
    ("Materials/**/Backup.mat.nexus\n", "Materials/A/B/Backup.mat.nexus", "Materials/Main.mat.nexus"),
    ("Materials/**/Backup.mat.nexus\n", "Materials/Backup.mat.nexus", "Materials/Main.mat.nexus"),
    ("Materials/*/Backup.mat.nexus\n", "Materials/A/Backup.mat.nexus", "Materials/A/B/Main.mat.nexus"),
    ("Backup?.mat.nexus\n", "Backup1.mat.nexus", "Backup12.mat.nexus"),
    ("Backup[0-9].mat.nexus\n", "Backup7.mat.nexus", "BackupA.mat.nexus"),
    ("Cache/\n!Cache/\nCache/Backup.mat.nexus\n", "Cache/Backup.mat.nexus", "Cache/Main.mat.nexus"),
    ("# comment\n\n*.tmp.mat.nexus\n", "Backup.tmp.mat.nexus", "Main.mat.nexus"),
    ("\\#Backup.mat.nexus\n", "#Backup.mat.nexus", "Main.mat.nexus"),
    ("\\!Backup.mat.nexus\n", "!Backup.mat.nexus", "Main.mat.nexus"),
])
def test_ignore_patterns(tmp_path, rules, ignored, included):
    for name in (ignored, included):
        path = tmp_path / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("fixture", encoding="utf-8")
    (tmp_path / ".nexusignore").write_text(rules, encoding="utf-8")
    assert [path.relative_to(tmp_path).as_posix() for path in iter_text_files(tmp_path)] == [included]


def test_negation_requires_the_parent_directory_to_be_included(tmp_path):
    cache = tmp_path / "Cache"
    cache.mkdir()
    (cache / "Keep.mat.nexus").write_text("fixture", encoding="utf-8")
    (tmp_path / ".nexusignore").write_text("Cache/\n!Cache/Keep.mat.nexus\n", encoding="utf-8")
    assert iter_text_files(tmp_path) == []
    (tmp_path / ".nexusignore").write_text("Cache/*\n!Cache/Keep.mat.nexus\n", encoding="utf-8")
    assert iter_text_files(tmp_path) == [cache / "Keep.mat.nexus"]


def test_ignore_file_is_reloaded_for_each_scan_and_accepts_bom(tmp_path):
    file = tmp_path / "Main.mat.nexus"
    file.write_text("fixture", encoding="utf-8")
    ignore = tmp_path / ".nexusignore"
    ignore.write_text("Main.mat.nexus\n", encoding="utf-8-sig")
    assert iter_text_files(tmp_path) == []
    ignore.write_text("# allowed\n", encoding="utf-8")
    assert iter_text_files(tmp_path) == [file]


def test_gitignore_does_not_control_asset_publication(tmp_path):
    file = tmp_path / "Main.mat.nexus"
    file.write_text("fixture", encoding="utf-8")
    (tmp_path / ".gitignore").write_text("*.nexus\n", encoding="utf-8")
    assert iter_text_files(tmp_path) == [file]


def test_builtin_metadata_exclusions_cannot_be_negated(tmp_path):
    cache = tmp_path / ".cache"
    cache.mkdir()
    (cache / "Backup.mat.nexus").write_text("fixture", encoding="utf-8")
    (tmp_path / ".nexusignore").write_text("!**\n", encoding="utf-8")
    assert iter_text_files(tmp_path) == []


def test_invalid_ignore_file_is_reported(tmp_path):
    (tmp_path / ".nexusignore").write_bytes(b"\xff\xfe\xff")
    with pytest.raises(SyncError) as failure:
        iter_text_files(tmp_path)
    assert failure.value.code == "invalid_ignore_file"
