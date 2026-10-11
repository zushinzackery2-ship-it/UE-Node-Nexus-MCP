"""The source-set check covers additions, removals and ignored cache controls."""

import json

import pytest

from ue_node_nexus_mcp.safety.isolation.snapshot import copy_project, inventory, unchanged


@pytest.mark.parametrize("path", ["Content/New.uasset", "Shaders/New.usf", "Tools/New.py"])
def test_added_source_input_invalidates_existing_snapshot(tmp_path, path):
    source = tmp_path / "source"
    source.mkdir()
    project = source / "Scene.uproject"
    project.write_text(json.dumps(dict(FileVersion=3)))
    snapshot = copy_project(inventory(project), tmp_path / "copy")
    assert unchanged(snapshot)
    added = source / path
    added.parent.mkdir(parents=True)
    added.write_bytes(b"new source input")
    assert not unchanged(snapshot)


def test_removed_input_returns_changed_and_ignored_cache_does_not(tmp_path):
    source = tmp_path / "source"
    source.mkdir()
    project = source / "Scene.uproject"
    project.write_text(json.dumps(dict(FileVersion=3)))
    plan = source / "Saved/plan.json"
    plan.parent.mkdir()
    plan.write_text("{}")
    snapshot = copy_project(inventory(project, ["Saved/plan.json"]), tmp_path / "copy")
    (source / "Saved/cache.json").write_text("{}")
    assert unchanged(snapshot)
    plan.unlink()
    assert not unchanged(snapshot)
