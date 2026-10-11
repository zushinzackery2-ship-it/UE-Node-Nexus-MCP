"""Tool metadata directories never become authored asset candidates."""

import pytest

from ue_node_nexus_mcp.transcode.collaboration.workspace.files import discover
from ue_node_nexus_mcp.transcode.storage.paths import iter_text_files, parse_text_path


@pytest.mark.parametrize("directory", [".auto-index-mcp", ".git", "Materials/.cache", ".nexus/undo"])
def test_tool_directories_are_excluded_from_every_discovery(tmp_path, directory):
    authored = tmp_path / "Materials" / "M_Main.mat.nexus"
    authored.parent.mkdir(parents=True)
    authored.write_text("authored", encoding="utf-8")
    tool_file = tmp_path / directory / "M_Backup.mat.nexus"
    tool_file.parent.mkdir(parents=True, exist_ok=True)
    tool_file.write_text("tool data", encoding="utf-8")

    assert iter_text_files(tmp_path) == [authored]
    assert discover(tmp_path, dict()) == dict([("/Game/Materials/M_Main.M_Main", "Materials/M_Main.mat.nexus")])
    assert parse_text_path(tmp_path, tool_file) is None


def test_discovery_keeps_registered_assets_in_tool_directories(tmp_path):
    registered = dict([("/Game/Materials/M_Main.M_Main", ".cache/M_Main.mat.nexus")])
    assert discover(tmp_path, registered) == registered


def test_hidden_files_and_python_cache_are_excluded(tmp_path):
    (tmp_path / ".backup.mat.nexus").write_text("tool data", encoding="utf-8")
    cache = tmp_path / "__pycache__"
    cache.mkdir()
    (cache / "Backup.mat.nexus").write_text("tool data", encoding="utf-8")
    assert iter_text_files(tmp_path) == []
    assert discover(tmp_path, dict()) == dict()


def test_directories_with_nexus_suffix_are_not_assets(tmp_path):
    (tmp_path / "Directory.mat.nexus").mkdir()
    assert iter_text_files(tmp_path) == []
    assert discover(tmp_path, dict()) == dict()
