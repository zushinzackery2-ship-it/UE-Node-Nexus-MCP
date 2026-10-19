"""Overlay snapshots link unchanging inputs and copy only the packages the worker may save."""

import hashlib

import pytest

from ue_node_nexus_mcp.safety.isolation import baseline, snapshot


def project_tree(tmp_path):
    root = tmp_path / "Projects" / "Game"
    (root / "Content" / "Mat").mkdir(parents=True)
    (root / "Config").mkdir()
    (root / "Game.uproject").write_text("{}", encoding="utf-8")
    (root / "Content" / "Mat" / "MF_Base.uasset").write_bytes(b"candidate" * 128)
    (root / "Content" / "Mat" / "MF_Other.uasset").write_bytes(b"linked" * 256)
    (root / "Config" / "DefaultEngine.ini").write_text("[x]", encoding="utf-8")
    return root


def overlay(root, tmp_path, name="copy"):
    plan = snapshot.inventory(root / "Game.uproject")
    target = tmp_path / name / "Project"
    return snapshot.copy_project(plan, target, candidates={"Content/Mat/MF_Base.uasset"}), target


def test_candidates_come_from_operations(tmp_path):
    root = project_tree(tmp_path)
    entries = [dict(operation="graph_patch_apply", payload=dict(asset_path="/Game/Mat/MF_Base"))]
    assert snapshot.content_candidates(root / "Game.uproject", entries) == {"Content/Mat/MF_Base.uasset"}


def test_scene_plans_are_candidates_too(tmp_path):
    root = project_tree(tmp_path)
    (root / "Plans").mkdir()
    (root / "Plans" / "scene.json").write_text("{}", encoding="utf-8")
    entries = [dict(operation="scene_apply", payload=dict(plan_file="Plans/scene.json", out_file="Saved/out.json"))]
    assert "Plans/scene.json" in snapshot.content_candidates(root / "Game.uproject", entries)


def test_overlay_links_everything_but_the_candidate(tmp_path):
    root = project_tree(tmp_path)
    result, target = overlay(root, tmp_path)
    candidate = target / "Content" / "Mat" / "MF_Base.uasset"
    linked = target / "Content" / "Mat" / "MF_Other.uasset"
    assert result["copy_mode"] == "overlay"
    assert candidate.is_file() and not candidate.is_symlink()
    assert not (target / "Config" / "DefaultEngine.ini").is_symlink()
    if not linked.is_symlink():
        pytest.skip("linking is not permitted for this account")
    assert linked.resolve() == (root / "Content" / "Mat" / "MF_Other.uasset").resolve()
    assert result["linked_files"] == 1
    assert result["copied_files"] == 3
    assert result["bytes"] == len(b"candidate" * 128) + len(b"{}") + len(b"[x]")


def test_created_assets_protect_their_directory(tmp_path):
    root = project_tree(tmp_path)
    (root / "Content" / "Bulk").mkdir()
    (root / "Content" / "Bulk" / "A0.uasset").write_bytes(b"bulk")
    entries = [dict(operation="asset_create", payload=dict(asset_path="/Game/Bulk/NewMat", asset_kind="material"))]
    plan = snapshot.inventory(root / "Game.uproject")
    candidates = snapshot.content_candidates(root / "Game.uproject", entries)
    target = tmp_path / "created" / "Project"
    result = snapshot.copy_project(plan, target, candidates=candidates)
    assert "Content/Bulk/NewMat.uasset" in candidates
    assert not (target / "Content" / "Bulk").is_symlink()
    if (target / "Content" / "Bulk" / "A0.uasset").is_symlink():
        assert result["linked_files"] >= 1


def test_worker_copy_disables_autosave(tmp_path):
    root = project_tree(tmp_path)
    (root / "Config" / "DefaultEditorPerProjectUserSettings.ini").write_text(
        "[/Script/UnrealEd.EditorLoadingSavingSettings]\nbAutoSaveEnable=True\n", encoding="utf-8")
    _, target = overlay(root, tmp_path)
    snapshot.quiet_worker_copy(target / "Game.uproject")
    text = (target / "Config" / "DefaultEditorPerProjectUserSettings.ini").read_text(encoding="utf-8")
    assert "bAutoSaveEnable=False" in text and "bAutoSaveEnable=True" not in text


def test_full_copy_is_still_selectable(tmp_path, monkeypatch):
    monkeypatch.setenv("UE_NEXUS_VALIDATION_COPY", "full")
    result, target = overlay(root=project_tree(tmp_path), tmp_path=tmp_path)
    assert result["copy_mode"] == "full"
    assert result["linked_files"] == 0
    assert not (target / "Content" / "Mat" / "MF_Other.uasset").is_symlink()


def test_whole_directories_are_linked_once(tmp_path):
    root = project_tree(tmp_path)
    (root / "Content" / "Bulk").mkdir()
    for index in range(3):
        (root / "Content" / "Bulk" / f"A{index}.uasset").write_bytes(b"bulk")
    result, target = overlay(root, tmp_path)
    bulk = target / "Content" / "Bulk"
    if not bulk.is_symlink():
        pytest.skip("linking is not permitted for this account")
    assert bulk.resolve() == (root / "Content" / "Bulk").resolve()
    assert result["linked_directories"] == 1
    assert result["linked_files"] == 4
    assert not (root / "Content" / "Bulk" / "A0.uasset").is_symlink()


def test_candidate_directories_are_never_linked(tmp_path):
    root = project_tree(tmp_path)
    result, target = overlay(root, tmp_path)
    assert not (target / "Content" / "Mat").is_symlink()
    assert (target / "Content" / "Mat" / "MF_Base.uasset").read_bytes() == b"candidate" * 128


def test_write_through_a_link_is_detected(tmp_path):
    root = project_tree(tmp_path)
    result, target = overlay(root, tmp_path)
    linked = target / "Content" / "Mat" / "MF_Other.uasset"
    if not linked.is_symlink():
        pytest.skip("linking is not permitted for this account")
    assert snapshot.unchanged(result)
    linked.write_bytes(b"worker saved through the link")
    assert not snapshot.unchanged(result)


def test_unrelated_source_change_is_detected(tmp_path):
    root = project_tree(tmp_path)
    result, _ = overlay(root, tmp_path)
    (root / "Content" / "Mat" / "MF_Other.uasset").write_bytes(b"edited" * 8)
    assert not snapshot.unchanged(result)


def test_baseline_overlay_replaces_the_link_without_touching_the_source(tmp_path):
    root = project_tree(tmp_path)
    result, target = overlay(root, tmp_path)
    linked = target / "Content" / "Mat" / "MF_Other.uasset"
    if not linked.is_symlink():
        pytest.skip("linking is not permitted for this account")
    original = root / "Content" / "Mat" / "MF_Other.uasset"
    held = root / "Saved" / "Nexus" / "SafetyBaseline" / "entry" / "Content" / "Mat" / "MF_Other.uasset"
    held.parent.mkdir(parents=True)
    held.write_bytes(b"live memory")
    row = dict(relative="Content/Mat/MF_Other.uasset", source=str(held), hash=hashlib.md5(b"live memory").hexdigest())
    record = dict(project=str(root / "Game.uproject"), root=str(held.parents[2]), files=[row], dry_run=False)
    baseline.overlay(result, record)
    assert not linked.is_symlink()
    assert linked.read_bytes() == b"live memory"
    assert original.read_bytes() == b"linked" * 256
