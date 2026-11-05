"""Isolation autosave overrides apply to the exact Unreal config section and key."""

from configparser import ConfigParser

import pytest

from ue_node_nexus_mcp.safety.isolation.snapshot import quiet_worker_copy


SECTION = "/Script/UnrealEd.EditorLoadingSavingSettings"


@pytest.mark.parametrize("source", [
    "; bAutoSaveEnable is inherited from editor defaults\n",
    "[Unrelated]\nbAutoSaveEnable=True\n",
    f"[{SECTION}]\nbAutoSaveEnableBackup=True\n",
    f"[{SECTION}]\nbAutoSaveEnable=True\n",
    f"[{SECTION}]\nOther=Preserved\n[Unrelated]\nbAutoSaveEnable=True\n",
])
def test_worker_disables_autosave_in_target_section_only(tmp_path, source):
    settings = tmp_path / "Config/DefaultEditorPerProjectUserSettings.ini"
    settings.parent.mkdir()
    settings.write_text(source, encoding="utf-8")
    quiet_worker_copy(tmp_path / "Game.uproject")
    text = settings.read_text(encoding="utf-8")
    parsed = ConfigParser(strict=False)
    parsed.read_string(text)
    assert parsed.getboolean(SECTION, "bAutoSaveEnable") is False
    if "[Unrelated]" in source:
        assert parsed.getboolean("Unrelated", "bAutoSaveEnable") is True
    if "bAutoSaveEnableBackup" in source:
        assert parsed.getboolean(SECTION, "bAutoSaveEnableBackup") is True
    quiet_worker_copy(tmp_path / "Game.uproject")
    assert settings.read_text(encoding="utf-8") == text
