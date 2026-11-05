"""Close saves dirty copied packages only after replacing a shared directory link."""

import threading
from pathlib import Path
from types import SimpleNamespace

import pytest

from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.safety.isolation import process as isolation_process
from ue_node_nexus_mcp.safety.isolation import session as session_module
from ue_node_nexus_mcp.safety.isolation.close import is_reparse, package_file
from ue_node_nexus_mcp.safety.isolation.session import ValidationSession
from ue_node_nexus_mcp.safety.isolation.snapshot import copy_project, inventory, unchanged


def project_tree(tmp_path):
    root = tmp_path / "source"
    (root / "Content" / "Mat").mkdir(parents=True)
    (root / "Content" / "Works" / "PBR").mkdir(parents=True)
    (root / "Config").mkdir()
    (root / "Game.uproject").write_text("{}", encoding="utf-8")
    (root / "Content" / "Mat" / "MF_A.uasset").write_bytes(b"A" * 128)
    (root / "Content" / "Mat" / "MF_B.uasset").write_bytes(b"B" * 128)
    map_file = root / "Content" / "Works" / "PBR" / "L_EntrancePBR.umap"
    map_file.write_bytes(b"source map bytes")
    return root


def worker(tmp_path, monkeypatch, package):
    source = project_tree(tmp_path)
    snapshot = copy_project(inventory(source / "Game.uproject"),
                            tmp_path / "validation" / "Project",
                            candidates={"Content/Mat/MF_A.uasset", "Content/Mat/MF_B.uasset"})
    copy_root = Path(snapshot["project"]).parent
    linked = copy_root / "Content" / "Works"
    if not linked.is_symlink():
        pytest.skip("linking is not permitted for this account")
    state = dict(instance_id="worker", state="READY", dirty_packages=[package], users=[], pid=42)
    saves = []

    session = ValidationSession.__new__(ValidationSession)
    session.project = Path(snapshot["project"])
    session.content_mounts = {"/Game": str(copy_root / "Content")}
    session.timeout = 1
    session.manager = SimpleNamespace(current=lambda: dict(state), status=lambda *args, **kwargs: dict(state),
                                      release=lambda: None, close=lambda: None,
                                      broker=SimpleNamespace(session_id="self"),
                                      lock=threading.RLock(), scopes={})

    def call(operation, payload):
        assert operation == "asset_save"
        file = package_file(session.project, payload["asset_path"], session.content_mounts)
        assert not any(is_reparse(path) for path in (file, *file.parents) if path != copy_root)
        saves.append(file)
        file.write_bytes(b"saved copy")
        state["dirty_packages"] = []
        return dict(ok=True)

    def close(manager, payload):
        if payload.get("dry_run", True):
            blockers = ["instance_dirty"] if state["dirty_packages"] else []
            return dict(instance=dict(state), blockers=blockers)
        return dict(exit_confirmed=True, state="EXITED", exit_code=0)

    forced = []

    def stop(current, project):
        forced.append(current)
        return dict(exit_confirmed=True, forced=True, state="EXITED", exit_code=88, pid=42)

    session.call = call
    monkeypatch.setattr(session_module, "close_instance", close)
    monkeypatch.setattr(isolation_process, "stop_owned", stop)
    return session, snapshot, saves, forced, source


def test_linked_dirty_map_is_materialized_without_touching_source(tmp_path, monkeypatch):
    session, snapshot, saves, forced, source = worker(tmp_path, monkeypatch, "/Game/Works/PBR/L_EntrancePBR")
    original = source / "Content" / "Works" / "PBR" / "L_EntrancePBR.umap"
    before = (original.read_bytes(), original.stat().st_mtime_ns)
    result = session.close()
    assert result["exit_confirmed"] is True and result["exit_code"] == 0
    assert forced == []
    assert saves and not any(is_reparse(path) for path in (saves[0], *saves[0].parents)
                              if path != Path(snapshot["project"]).parent)
    assert (original.read_bytes(), original.stat().st_mtime_ns) == before
    assert unchanged(snapshot)


def test_unowned_dirty_package_is_refused_before_save(tmp_path, monkeypatch):
    session, _, saves, forced, _ = worker(tmp_path, monkeypatch, "/Tests/Shared/L_Other")
    result = session.close()
    assert result["error"]["code"] == "isolation_shared_package_dirty"
    assert saves == []
    assert forced and result["exit_code"] == 88
