"""A crash prompt is answered before launch, and the data it would discard is kept."""

import json

from ue_node_nexus_mcp.instances.identity import window_state
from ue_node_nexus_mcp.instances.lifecycle import prompts, recovery


def project(tmp_path, enabled=True, malformed=False):
    root = tmp_path / "Game"
    autosaves = root / "Saved" / "Autosaves"
    autosaves.mkdir(parents=True)
    (root / "Game.uproject").write_text("{}", encoding="utf-8")
    index = autosaves / "PackageRestoreData.json"
    if malformed:
        index.write_text("{", encoding="utf-8")
        return root, autosaves
    rows = [dict(PackagePathName="/Game/M_Probe", PackageAssetName="M_Probe", AutoSavePath="AutoSave_M_Probe.uasset")]
    index.write_text(json.dumps(dict(RestoreEnabled=enabled, Packages=rows)), encoding="utf-8")
    (autosaves / "AutoSave_M_Probe.uasset").write_bytes(b"autosaved")
    return root, autosaves


def test_pending_lists_the_packages_ue_would_restore(tmp_path):
    root, _ = project(tmp_path)
    rows = recovery.pending(root / "Game.uproject")
    assert [row["package"] for row in rows] == ["/Game/M_Probe"]


def test_a_clean_shutdown_or_broken_index_offers_nothing(tmp_path):
    clean, _ = project(tmp_path / "clean", enabled=False)
    broken, _ = project(tmp_path / "broken", malformed=True)
    assert recovery.pending(clean / "Game.uproject") == []
    assert recovery.pending(broken / "Game.uproject") == []


def test_default_mode_declines(tmp_path, monkeypatch):
    monkeypatch.delenv("UE_NEXUS_PACKAGE_RECOVERY", raising=False)
    assert recovery.mode() == "decline"
    monkeypatch.setenv("UE_NEXUS_PACKAGE_RECOVERY", "PROMPT")
    assert recovery.mode() == "prompt"
    monkeypatch.setenv("UE_NEXUS_PACKAGE_RECOVERY", "ignore")
    assert recovery.mode() == "decline"


def test_decline_answers_the_prompt_and_keeps_the_data(tmp_path, monkeypatch):
    root, autosaves = project(tmp_path)
    monkeypatch.delenv("UE_NEXUS_PACKAGE_RECOVERY", raising=False)
    assert recovery.launch_flag(root / "Game.uproject") == ["-AutoDeclinePackageRecovery"]
    backups = list((root / "Saved" / "Nexus" / "PackageRecovery").iterdir())
    assert len(backups) == 1
    assert (backups[0] / "PackageRestoreData.json").is_file()
    assert (backups[0] / "AutoSave_M_Probe.uasset").read_bytes() == b"autosaved"
    assert json.loads((backups[0] / "pending.json").read_text(encoding="utf-8"))[0]["asset"] == "M_Probe"
    assert (autosaves / "AutoSave_M_Probe.uasset").is_file()


def test_prompt_mode_leaves_the_dialog_to_the_user(tmp_path, monkeypatch):
    root, _ = project(tmp_path)
    monkeypatch.setenv("UE_NEXUS_PACKAGE_RECOVERY", "prompt")
    assert recovery.launch_flag(root / "Game.uproject") == []
    assert not (root / "Saved" / "Nexus" / "PackageRecovery").exists()


def test_no_pending_recovery_adds_nothing(tmp_path, monkeypatch):
    root, _ = project(tmp_path, enabled=False)
    monkeypatch.delenv("UE_NEXUS_PACKAGE_RECOVERY", raising=False)
    assert recovery.launch_flag(root / "Game.uproject") == []
    assert not (root / "Saved" / "Nexus" / "PackageRecovery").exists()


def test_slate_restore_prompt_is_reported_as_blocking():
    assert window_state.blocking("Restore Packages", "UnrealWindow", True)
    assert not window_state.blocking("Restore Packages", "UnrealWindow", False)
    assert not window_state.blocking("Shadetest - 虚幻编辑器", "UnrealWindow", True)
    assert window_state.blocking("Anything", "#32770", False)


def test_an_answerless_prompt_says_what_the_editor_waits_on():
    rows = [dict(title="Restore Packages", visible=True, class_name="UnrealWindow", dialog=True, message="Restore Packages")]
    record = prompts.observe(dict(), rows, None)
    assert record["waiting_for_user"] is True
    assert record["blocking_dialog"]["title"] == "Restore Packages"
