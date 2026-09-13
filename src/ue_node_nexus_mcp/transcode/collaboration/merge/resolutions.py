"""Apply explicit conflict decisions while maintaining semantic references."""

from __future__ import annotations

from copy import deepcopy

from ...sync_project import SyncError
from ..store.io import digest


def set_path(root: dict, path: list, value, delete=False) -> dict | None:
    if not path:
        return None if delete else deepcopy(value)
    current = root
    for key in path[:-1]:
        if isinstance(current, list):
            current = current[key]
        else:
            current = current.setdefault(key, dict())
    if delete:
        if isinstance(current, list):
            current.pop(path[-1])
        else:
            current.pop(path[-1], None)
    else:
        current[path[-1]] = deepcopy(value)
    return root


def remove_references(semantic: dict, identifier: str) -> None:
    removed_sections = []
    for scope, section in semantic["sections"].items():
        if section.get("owner") == identifier:
            removed_sections.append(scope)
            continue
        section["links"] = dict((key, link) for key, link in section["links"].items() if identifier not in (link["src"], link["dst"]))
        if "order" in section:
            section["order"] = [key for key in section["order"] if key != identifier]
        for entity in section["entities"].values():
            for name in ("args", "props"):
                entity[name] = dict((key, value) for key, value in entity[name].items() if value.get("ref") != identifier)
    for scope in removed_sections:
        semantic["sections"].pop(scope)


def resolve_tree(history, tree: str, conflict: dict, decision: dict) -> str:
    choice = decision["choice"]
    if choice not in conflict["allowed_resolutions"]:
        raise SyncError("invalid_resolution", choice)
    entries = history.entries(tree)
    asset, path = conflict["asset"], conflict["field_path"]
    identifier = entries.get(asset)
    if identifier is None:
        source = next((item for item in conflict["snapshots"] if item), None)
        snapshot = history.store.objects.data(source, "snapshot")
    else:
        snapshot = history.store.objects.data(identifier, "snapshot")
    snapshot = deepcopy(snapshot)
    delete = choice == "delete"
    if choice in ("ours", "theirs", "base"):
        value = conflict[choice]
        delete = value == dict(state="missing")
    elif choice == "custom":
        if "value" not in decision:
            raise SyncError("invalid_resolution", "custom resolution requires value")
        value = decision["value"]
        original = conflict["ours"]
        if isinstance(original, dict) and "type" in original and "state" in original:
            if not isinstance(value, dict) or value.get("type") != original["type"] or value.get("state") not in ("explicit", "default", "missing", "opaque"):
                raise SyncError("resolution_type", "custom field values require matching type and explicit state")
    elif choice == "rename":
        if len(path) < 4 or path[2] != "entities" or not decision.get("name"):
            raise SyncError("invalid_resolution", "rename needs a declaration conflict and name")
        path = path[:4] + ["alias"]
        value = decision["name"]
    else:
        value = None
    semantic = set_path(snapshot["semantic"], path, value, delete)
    if semantic is None:
        entries.pop(asset, None)
    else:
        if delete and len(path) == 4 and path[2] == "entities":
            remove_references(semantic, path[3])
        snapshot.update(semantic=semantic, semantic_hash=digest(semantic))
        entries[asset] = history.store.objects.put("snapshot", snapshot, snapshot.get("schema_objects", []))
    return history.tree(entries)
