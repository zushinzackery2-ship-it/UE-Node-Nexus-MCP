"""Engine upgrades preserve both plugin versions until all writes verify."""

from pathlib import Path

import pytest

from tests.compile_check import install_engine
from tests.compile_check.prepare_host import PLUGINS


@pytest.fixture
def staged_install(tmp_path, monkeypatch):
    host = tmp_path / "build/host"
    engine = tmp_path / "engine"
    executable = engine / "Engine/Binaries/Win64/UnrealEditor.exe"
    executable.parent.mkdir(parents=True)
    executable.touch()
    for name in PLUGINS:
        for parent, marker in ((host / "Plugins", "new"), (engine / "Engine/Plugins/Editor", "old")):
            plugin = parent / name
            (plugin / "Source").mkdir(parents=True)
            (plugin / "Source/marker").write_text(marker)
            (plugin / f"{name}.uplugin").write_text(marker)
            (plugin / "BuildIdentity.json").write_text(marker)
    monkeypatch.setattr(install_engine, "ROOT", tmp_path)
    monkeypatch.setattr(install_engine, "require_editor_closed", lambda: None)
    monkeypatch.setattr(install_engine, "verify_plugin", lambda source, built: None)
    return host, engine


def test_upgrade_relocates_to_marketplace_and_retains_backup(staged_install):
    host, engine = staged_install
    install_engine.install(host, engine)
    for name in PLUGINS:
        assert not (engine / "Engine/Plugins/Editor" / name).exists()
        assert (engine / "Engine/Plugins/Marketplace" / name / "Source/marker").read_text() == "new"
    backups = list((engine / ".nexus-plugin-install").glob("*/backup/Editor/*/Source/marker"))
    assert len(backups) == len(PLUGINS)
    assert all(path.read_text() == "old" for path in backups)


def test_bad_second_plugin_leaves_engine_unchanged(staged_install, monkeypatch):
    host, engine = staged_install

    def verify(source, built):
        if built.name == PLUGINS[1]:
            raise RuntimeError("invalid second plugin")

    monkeypatch.setattr(install_engine, "verify_plugin", verify)
    with pytest.raises(RuntimeError, match="invalid second plugin"):
        install_engine.install(host, engine)
    assert not (engine / "Engine/Plugins/Marketplace").exists()
    for name in PLUGINS:
        assert (engine / "Engine/Plugins/Editor" / name / "Source/marker").read_text() == "old"


def test_failed_promotion_restores_all_original_plugins(staged_install, monkeypatch):
    host, engine = staged_install
    rename = Path.rename

    def fail_second(source, target):
        if source.parent.name == "staged" and source.name == PLUGINS[1]:
            raise OSError("promotion denied")
        return rename(source, target)

    monkeypatch.setattr(Path, "rename", fail_second)
    with pytest.raises(OSError, match="promotion denied"):
        install_engine.install(host, engine)
    for name in PLUGINS:
        assert not (engine / "Engine/Plugins/Marketplace" / name).exists()
        assert (engine / "Engine/Plugins/Editor" / name / "Source/marker").read_text() == "old"
