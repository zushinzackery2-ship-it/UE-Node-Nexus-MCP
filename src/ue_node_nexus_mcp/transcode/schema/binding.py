"""Pin only the schema facts used by a snapshot or fixed execution intent."""

from __future__ import annotations

from .catalog import read_entry


def bind(store, snapshot: dict, schema) -> dict:
    if schema is None:
        return snapshot
    from ..collaboration.semantic.decode import to_document
    from ..lint import lint_document

    # This snapshot's own facts are collected in isolation, then folded back into
    # the session set a preview pins with SchemaLock.binding().
    session = dict(schema.used)
    schema.used.clear()
    lint_document(to_document(snapshot), snapshot["semantic"]["kind"], schema)
    used = dict(schema.used)
    schema.used = {**session, **used}
    entries, objects = [], []
    manifest = schema.info()
    # A binding is a set of pinned facts. Sorting keeps the snapshot identical
    # whichever traversal order discovered them, so equal states stay equal.
    for (family, name), entry_hash in sorted(used.items()):
        entry = manifest.get("tables", dict()).get(family, dict()).get(name)
        if entry:
            record = read_entry(schema, family, name, entry)
            objects.append(store.objects.put("schema", record))
            entries.append(dict(family=family, name=name, hash=entry_hash))
    snapshot = dict(snapshot)
    snapshot["schema_binding"] = dict(schema_key=schema.key, entries=entries)
    snapshot["schema_objects"] = sorted(objects)
    return snapshot
