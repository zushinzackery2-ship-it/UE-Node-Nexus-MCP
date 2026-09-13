from __future__ import annotations

import multiprocessing
import time

import pytest

from ue_node_nexus_mcp.transcode.collaboration.history import History
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.collaboration.store.maintenance import collect, integrity
from ue_node_nexus_mcp.transcode.sync_project import SyncError


def _compete(path, head, target, barrier, output):
    store = Store(path)
    barrier.wait(timeout=20)
    try:
        store.move("refs/heads/main", target, head)
        output.put("ok")
    except SyncError as exc:
        output.put(exc.code)


def test_objects_deduplicate_and_detect_tampering(tmp_path):
    store = Store(tmp_path)
    left = store.objects.put("snapshot", dict(b=2, a=1))
    right = store.objects.put("snapshot", dict(a=1, b=2))
    assert left == right
    store.objects.path(left).write_text('{"format":1}', encoding="utf-8")
    with pytest.raises(SyncError, match="checksum"):
        store.objects.get(left)


def test_refs_cas_reflog_and_process_restart(tmp_path):
    store = Store(tmp_path)
    first = store.objects.put("snapshot", dict(value=1))
    second = store.objects.put("snapshot", dict(value=2))
    store.move("refs/heads/main", first, None, "A", "initial")
    with pytest.raises(SyncError) as failure:
        store.move("refs/heads/main", second, None)
    assert failure.value.code == "branch_moved"
    restored = Store(tmp_path)
    assert restored.ref("refs/heads/main") == first
    assert len(restored.reflog()) == 1
    assert restored.project_id == store.project_id


def test_metadata_and_ref_update_rollback_together(tmp_path):
    from ue_node_nexus_mcp.transcode.collaboration.store.refs import move_ref

    store = Store(tmp_path)
    root = store.objects.put("tree", dict())
    with pytest.raises(RuntimeError):
        with store.db.connection(write=True) as connection:
            move_ref(connection, "refs/heads/main", root, None)
            store.put_record("workspace", "A", dict(head=root), [root], expected=0, connection=connection)
            raise RuntimeError("crash before commit")
    assert store.ref("refs/heads/main") is None
    assert store.record("workspace", "A") is None
    assert store.reflog() == []


def test_two_processes_cannot_advance_same_ref(tmp_path):
    store = Store(tmp_path)
    identifiers = [store.objects.put("tree", dict(value=value)) for value in range(3)]
    store.move("refs/heads/main", identifiers[0], None)
    context = multiprocessing.get_context("spawn")
    barrier, output = context.Barrier(2), context.Queue()
    processes = [context.Process(target=_compete, args=(tmp_path, identifiers[0], target, barrier, output)) for target in identifiers[1:]]
    for process in processes:
        process.start()
    for process in processes:
        process.join(timeout=30)
        assert process.exitcode == 0
    assert sorted([output.get(timeout=5), output.get(timeout=5)]) == ["branch_moved", "ok"]
    assert len(store.reflog()) == 2


def test_gc_protects_sessions_stashes_reflog_and_has_grace(tmp_path):
    store = Store(tmp_path)
    initial = time.time()
    objects = [store.objects.put("snapshot", dict(value=value)) for value in range(4)]
    store.move("refs/heads/main", objects[0], None)
    store.move("refs/heads/main", objects[1], objects[0])
    store.put_record("session", "merge", dict(), objects[2:3])
    collect(store, dry_run=False, now=initial + 1)
    result = collect(store, dry_run=False, now=initial + 31 * 86400)
    assert result["garbage"] == [objects[3]]
    assert integrity(store)["objects"] == 3
    assert store.objects.get(objects[0])
    collect(store, dry_run=False, now=initial + 91 * 86400)
    collect(store, dry_run=False, now=initial + 122 * 86400)
    assert store.objects.get(objects[2])
    assert not store.objects.path(objects[0]).exists()


def test_history_preserves_dag_and_multiple_merge_bases(tmp_path):
    history = History(Store(tmp_path))
    tree = history.tree(dict())
    root = history.create(tree, [], "root")
    a = history.create(tree, [root], "A")
    b = history.create(tree, [root], "B")
    ab = history.create(tree, [a, b], "AB")
    ba = history.create(tree, [b, a], "BA")
    assert history.merge_bases(ab, ba) == sorted([a, b])
    assert history.resolve(ab + "^2") == b
    assert history.resolve(ab + "~2") == root
    assert history.is_ancestor(root, ba)


def test_repository_rejects_another_project(tmp_path):
    Store(tmp_path / "store", str(tmp_path / "a.uproject"))
    with pytest.raises(SyncError) as failure:
        Store(tmp_path / "store", str(tmp_path / "b.uproject"))
    assert failure.value.code == "project_mismatch"
