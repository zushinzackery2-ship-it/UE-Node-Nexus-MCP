"""Copy project inputs successfully while the navigation cache holds its real lock."""

import json
import sys

import pytest

from ue_node_nexus_mcp.safety.isolation.snapshot import copy_project, inventory, unchanged


@pytest.mark.skipif(sys.platform != "win32", reason="Uses the observed Windows byte-range lock")
def test_snapshot_excludes_locked_navigation_cache_and_retains_source_validation(tmp_path):
    import msvcrt

    source = tmp_path / "source"
    source.mkdir()
    project = source / "Scene.uproject"
    project.write_text(json.dumps(dict(FileVersion=3, EngineAssociation="5.5")), encoding="utf-8")
    content = source / "Content"
    content.mkdir()
    asset = content / "Scene.uasset"
    asset.write_bytes(b"native package input")
    tools = source / "Tools"
    tools.mkdir()
    script = tools / "scene.py"
    script.write_text("SCENE = 'entrance'\n", encoding="utf-8")
    cache = tools / ".auto-index-mcp"
    cache.mkdir()
    lock_path = cache / "watcher.lock"
    lock_path.write_bytes(b"1")

    with lock_path.open("r+b") as lock:
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        try:
            with pytest.raises(PermissionError):
                lock_path.read_bytes()
            plan = inventory(project)
            snapshot = copy_project(plan, tmp_path / "copy")
            copied_paths = set(item["path"] for item in snapshot["files"])
            assert copied_paths == set(("Scene.uproject", "Content/Scene.uasset", "Tools/scene.py"))
            assert (tmp_path / "copy/Content/Scene.uasset").read_bytes() == asset.read_bytes()
            assert (tmp_path / "copy/Tools/scene.py").read_text() == script.read_text()
            assert unchanged(snapshot)
            asset.write_bytes(b"changed native package input")
            assert not unchanged(snapshot)
        finally:
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
