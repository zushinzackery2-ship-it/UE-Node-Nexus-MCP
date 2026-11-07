"""Candidate interfaces carried by stored snapshots.

The material layer derives signatures from documents; snapshots are a
collaboration artifact, so decoding them into documents happens here.
"""

from contextlib import contextmanager

from ...interfaces import documents
from ...material.interfaces import signatures
from ...schema.catalog import publish
from ...storage.paths import object_path
from .decode import to_document


@contextmanager
def snapshots(items):
    with documents((item["semantic"]["kind"], to_document(item, include_defaults=True)) for item in items
                   if item and item["semantic"]["kind"] in ("material_function", "blueprint")):
        yield


@contextmanager
def tree(store, entries):
    with snapshots(store.objects.data(identifier, "snapshot") for identifier in entries.values()):
        yield


def publish_observed(schema, snapshot):
    """A verified function receipt updates the dynamic catalog for later calls."""
    if schema is None:
        return
    ports = signatures([(snapshot["semantic"]["kind"], to_document(snapshot, include_defaults=True))])
    entries = schema.info().get("tables", dict()).get("material_functions", dict())
    records = dict()
    for path, value in ports.items():
        keys = [key for key, entry in entries.items() if object_path(entry.get("path", "")) == path]
        for key in keys or [path]:
            records[key] = dict(value, path=path, source="verified_publication")
    if records:
        publish(schema.directory, schema.key, dict(material_functions=records), incremental=True)
