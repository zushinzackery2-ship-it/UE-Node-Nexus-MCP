"""Refreshing a catalog retains record evidence and one atomic manifest."""

import json
import pytest

from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.schema.catalog import read_entry, validate_binding
from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock
from ue_node_nexus_mcp.transcode.schema.records import normalize
from ue_node_nexus_mcp.transcode.errors import SyncError
from ue_node_nexus_mcp.transcode.storage.io import digest, read_json


def test_refresh_does_not_accumulate_full_manifest_copies(tmp_path):
    directory = tmp_path / "schema"
    previous = []
    for value in range(4):
        record = dict(path="/Script/Engine.Example", props=dict(Value=dict(type="int", default=str(value))))
        manifest = publish(directory, "environment", dict(asset=dict(Example=record)))
        previous.append(manifest["tables"]["asset"]["Example"])
    assert not (directory / "indexes").exists()
    assert read_json(directory / "key.json") == manifest
    lock = SchemaLock(directory, "environment")
    assert lock.resolve_class("asset", "Example").props["Value"]["default"] == "3"
    for entry in previous:
        assert digest(read_json(directory / entry["file"])) == entry["object_hash"]


def test_existing_historical_indexes_are_preserved_without_new_copies(tmp_path):
    indexes = tmp_path / "indexes"
    indexes.mkdir()
    archived = indexes / "old.json"
    archived.write_bytes(b"old evidence")
    publish(tmp_path, "environment", dict(asset=dict(Example=dict(path="/Script/Engine.Example"))))
    assert list(indexes.iterdir()) == [archived]
    assert archived.read_bytes() == b"old evidence"


def test_environment_changes_share_record_storage_and_preserve_binding(tmp_path):
    tables = dict(asset=dict(Example=dict(path="/Script/Engine.Example", props=dict(Value=dict(type="int", default="1")))))
    first = publish(tmp_path / "old", "old", tables, dict(build="first"))
    second = publish(tmp_path / "new", "new", tables, dict(build="second"))
    old_entry = first["tables"]["asset"]["Example"]
    new_entry = second["tables"]["asset"]["Example"]
    assert old_entry["object_hash"] == new_entry["object_hash"]
    assert (tmp_path / "old" / old_entry["file"]).resolve() == (tmp_path / "new" / new_entry["file"]).resolve()
    old_lock, new_lock = SchemaLock(tmp_path / "old", "old"), SchemaLock(tmp_path / "new", "new")
    assert read_entry(old_lock, "asset", "Example", old_entry)["schema_key"] == "old"
    assert read_entry(new_lock, "asset", "Example", new_entry)["schema_key"] == "new"
    with pytest.raises(SyncError) as failure:
        validate_binding(new_lock, old_lock.binding())
    assert failure.value.code == "schema_stale"


def test_shared_records_refuse_an_escape_from_the_record_pool(tmp_path):
    manifest = publish(tmp_path / "catalog", "key", dict(asset=dict(Example=dict(path="/Script/Engine.Example"))))
    entry = dict(manifest["tables"]["asset"]["Example"], file="../.objects/../../outside.json")
    with pytest.raises(SyncError):
        read_entry(SchemaLock(tmp_path / "catalog", "key"), "asset", "Example", entry)


def test_format_two_refresh_keeps_existing_record_bindings(tmp_path):
    directory = tmp_path / "catalog"
    directory.mkdir()
    raw = dict(path="/Script/Engine.Example", props=dict(Value=dict(type="int", default="1")))
    record = normalize("asset", "Example", raw, "environment")
    record["content_hash"] = digest(record)
    old_hash = digest(record)
    (directory / "record.json").write_bytes(json.dumps(record).encode())
    manifest = dict(format=2, key="environment", tables=dict(asset=dict(Example=dict(file="record.json", hash=old_hash))))
    (directory / "key.json").write_bytes(json.dumps(manifest).encode())
    lock = SchemaLock(directory, "environment")
    read_entry(lock, "asset", "Example", manifest["tables"]["asset"]["Example"])
    binding = lock.binding()
    current = publish(directory, "environment", dict(asset=dict(Example=raw)))
    assert current["tables"]["asset"]["Example"]["hash"] == old_hash
    validate_binding(SchemaLock(directory, "environment"), binding)
