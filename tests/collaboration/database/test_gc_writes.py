"""Object edges and metadata roots share the collector's write transaction."""

import threading
import time

import pytest

from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.collaboration.store.maintenance import collect, finish, integrity
from ue_node_nexus_mcp.transcode.errors import SyncError


def write_target(store, operation, target):
    if operation == "ref":
        store.move("refs/heads/new", target, None)
    elif operation == "link":
        store.objects.put("tree", dict(edge=True), [target])
    else:
        store.put_record("test", "new", dict(root=True), [target])


@pytest.mark.parametrize("operation", ["ref", "link", "record"])
def test_writer_validation_excludes_concurrent_collection(tmp_path, monkeypatch, operation):
    writer_store, collector = Store(tmp_path), Store(tmp_path)
    target = writer_store.objects.put("tree", dict(target=True))
    old = time.time() - 32 * 86400
    with writer_store.db.connection(write=True) as connection:
        connection.execute("UPDATE objects SET created=?, unreachable=? WHERE id=?", (old, old, target))
    validated, release, collected = threading.Event(), threading.Event(), threading.Event()
    failures = []
    original_get = writer_store.objects.get

    def observe(identifier, kind=None):
        value = original_get(identifier, kind)
        if identifier == target and not validated.is_set():
            validated.set()
            assert release.wait(5)
        return value

    def writer():
        try:
            write_target(writer_store, operation, target)
        except BaseException as exc:
            failures.append(exc)
        finally:
            writer_store.db.discard()

    def sweep():
        try:
            collect(collector, dry_run=False)
        except BaseException as exc:
            failures.append(exc)
        finally:
            collected.set()
            collector.db.discard()

    monkeypatch.setattr(writer_store.objects, "get", observe)
    writing = threading.Thread(target=writer)
    writing.start()
    assert validated.wait(5)
    sweeping = threading.Thread(target=sweep)
    sweeping.start()
    try:
        assert not collected.wait(0.2), "collector passed the writer's validation before its commit"
    finally:
        release.set()
        writing.join(5)
        sweeping.join(5)
    assert not writing.is_alive() and not sweeping.is_alive()
    assert failures == []
    assert integrity(writer_store)["ok"]


@pytest.mark.parametrize("operation", ["ref", "link", "record"])
def test_sweep_decision_rejects_edges_before_file_removal(tmp_path, operation):
    store = Store(tmp_path)
    target = store.objects.put("tree", dict(target=True))
    with store.db.connection(write=True) as connection:
        connection.execute("INSERT INTO tombstones VALUES (?, 'object', ?)", (target, time.time()))
        connection.execute("DELETE FROM objects WHERE id=?", (target,))
    assert store.objects.path(target).is_file()
    with pytest.raises(SyncError, match="registered"):
        write_target(store, operation, target)
    finish(store)
    assert integrity(store)["ok"]


def test_fresh_unreachable_parent_protects_older_child_until_both_expire(tmp_path):
    store = Store(tmp_path)
    target = store.objects.put("tree", dict(target=True))
    instant = time.time()
    old = instant - 32 * 86400
    with store.db.connection(write=True) as connection:
        connection.execute("UPDATE objects SET created=?, unreachable=? WHERE id=?", (old, old, target))
    parent = store.objects.put("tree", dict(parent=True), [target])
    assert collect(store, dry_run=False, now=instant + 1)["garbage"] == []
    assert integrity(store)["ok"]
    assert set(collect(store, dry_run=False, now=instant + 32 * 86400)["garbage"]) == {parent, target}
    assert integrity(store)["ok"]

