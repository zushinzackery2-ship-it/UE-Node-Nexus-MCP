"""Use only directly referenced candidate function signatures while validating UE readback."""

from contextlib import contextmanager

from ...material.interfaces import snapshots
from ...storage.paths import object_path


@contextmanager
def function_context(workspace, record, raw):
    if raw.get("kind") not in ("material", "material_function"):
        yield
        return
    references = set()
    for node in raw.get("graph", dict()).get("nodes", []):
        name = node.get("class_short", node.get("class", "")).rsplit(".", 1)[-1]
        if name.removeprefix("MaterialExpression") != "MaterialFunctionCall":
            continue
        references.update(object_path(prop["value"]) for prop in node.get("props", [])
                          if prop.get("name") == "MaterialFunction" and prop.get("value"))
    if not references:
        yield
        return
    entries = workspace.history.entries(record["candidate"])
    identifiers = [entries[path] for path in sorted(references) if path in entries]
    with snapshots(workspace.store.objects.data(identifier, "snapshot") for identifier in identifiers):
        yield
