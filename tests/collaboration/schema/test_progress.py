"""Schema migration has durable observable counts before asset apply begins."""

from uuid import uuid4
from itertools import count

import pytest

from ue_node_nexus_mcp.transcode.collaboration.jobs.service import create, observe
from ue_node_nexus_mcp.transcode.collaboration.jobs.state import active, update
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.errors import SyncError
from ue_node_nexus_mcp.transcode.schema import catalog
from ue_node_nexus_mcp.transcode.storage.io import digest, read_json


def records(count):
    return dict((f"Class{index}", dict(path=f"/Script/Engine.Class{index}", props=dict(Value=dict(type="float", default="0"))))
                for index in range(count))


def job(tmp_path):
    store = Store(tmp_path / "repository")
    record = create(store, "push", None, dict())
    return store, record["id"]


def test_migration_reports_phase_counts_and_elapsed_before_manifest(tmp_path, monkeypatch):
    from ue_node_nexus_mcp.transcode.schema import progress

    clock = count(step=0.6)
    monkeypatch.setattr(progress, "monotonic", lambda: next(clock))
    store, identifier = job(tmp_path)
    directory = tmp_path / "catalog"
    samples = []
    original = catalog.atomic_write

    def write(path, data):
        if path.parent.name == "asset" and path.suffix == ".json":
            samples.append(observe(store, "job_status", dict(job_id=identifier)))
            assert not (directory / "key.json").exists()
        original(path, data)

    monkeypatch.setattr(catalog, "atomic_write", write)
    with active(store, identifier):
        result = catalog.publish(directory, uuid4().hex, dict(asset=records(3)))
    assert len(samples) == 3
    assert all(sample["stage"] == "schema_migration" for sample in samples)
    progress = [sample["schema_progress"] for sample in samples]
    assert [sample["processed"] for sample in progress] == [0, 1, 2]
    assert all(sample["total"] == 3 and sample["elapsed_seconds"] >= 0 for sample in progress)
    final = observe(store, "job_status", dict(job_id=identifier))["schema_progress"]
    assert final["phase"] == "ready" and final["processed"] == final["total"] == 3
    assert final["io"]["writes"] >= 5
    assert all(final["io"][key] >= 0 for key in ("create_seconds", "lookup_seconds", "flush_seconds"))
    assert read_json(directory / "key.json") == result
    for entry in result["tables"]["asset"].values():
        assert digest(read_json(directory / entry["file"])) == entry["hash"]
    store.db.discard()


def test_cancelled_migration_keeps_committed_manifest_and_hash_binding(tmp_path, monkeypatch):
    directory = tmp_path / "catalog"
    before = catalog.publish(directory, "old", dict(asset=records(1)))
    store, identifier = job(tmp_path)
    original = catalog.atomic_write

    def write(path, data):
        original(path, data)
        if path.parent.name == "asset" and path.suffix == ".json":
            update(store, identifier, cancel_requested=True)

    monkeypatch.setattr(catalog, "atomic_write", write)
    with active(store, identifier), pytest.raises(SyncError) as failure:
        catalog.publish(directory, "new", dict(asset=records(5)))
    assert failure.value.code == "sync_job_cancelled"
    assert read_json(directory / "key.json") == before
    store.db.discard()


def test_cancellation_after_immutable_index_still_preserves_commit_point(tmp_path, monkeypatch):
    directory = tmp_path / "catalog"
    before = catalog.publish(directory, "old", dict(asset=records(1)))
    store, identifier = job(tmp_path)
    original = catalog.atomic_write

    def write(path, data):
        original(path, data)
        if path.parent.name == "indexes":
            update(store, identifier, cancel_requested=True)

    monkeypatch.setattr(catalog, "atomic_write", write)
    with active(store, identifier), pytest.raises(SyncError) as failure:
        catalog.publish(directory, "new", dict(asset=records(2)))
    assert failure.value.code == "sync_job_cancelled"
    assert read_json(directory / "key.json") == before
    store.db.discard()


def test_collection_reports_each_provider_then_migration(tmp_path, monkeypatch):
    from tests.collaboration.fake_bridge import ProtocolUe
    from ue_node_nexus_mcp.transcode.sync import schema
    from ue_node_nexus_mcp.transcode.sync.project import resolve_context

    ue = ProtocolUe()
    context = resolve_context(ue, env=dict(UE_NEXUS_TRANSCODE_DIR=str(tmp_path / "mirror")))
    store, identifier = job(tmp_path)
    samples = []
    original = schema.call_ok

    def call(*args, **kwargs):
        if args[1] in ("schema_export", "vfx_transcode_export"):
            samples.append(observe(store, "job_status", dict(job_id=identifier)))
        return original(*args, **kwargs)

    monkeypatch.setattr(schema, "call_ok", call)
    with active(store, identifier):
        schema.refresh(ue, context)
    assert len(samples) == 2
    assert all(sample["stage"] == "schema_collection" for sample in samples)
    assert [sample["schema_progress"]["processed"] for sample in samples] == [0, 1]
    assert all(sample["schema_progress"]["total"] == 2 for sample in samples)
    assert observe(store, "job_status", dict(job_id=identifier))["schema_progress"]["phase"] == "ready"
    store.db.discard()
