"""Metadata belongs to a declaration's contract, independently of its stable ID."""

from ...blueprint.classes import node_name
from ...diff.common import same_class
from ...text.semantic import value
from .blueprint import FUNCTION_NODES, positional_values


def same_contract(decl, previous, family, schema=None, metadata=None):
    if not previous:
        return True
    if not same_class(schema, family, decl.type_name, previous["type"]):
        return False
    if family == "k2node" and node_name(previous["type"]) in FUNCTION_NODES:
        positional = [value(text) for text in positional_values(decl, family, metadata, schema)]
        return positional == previous.get("positional", [])
    return True


def entity_contract(entity):
    identity = [entity["type"]]
    if node_name(entity["type"]) in FUNCTION_NODES:
        identity.append(entity.get("positional", []))
    return identity


def merged_bindings(semantic, snapshots):
    result = dict()
    for snapshot in snapshots:
        if snapshot:
            result.update(kept_bindings(semantic, snapshot))
    return result


def kept_bindings(semantic, snapshot):
    """Bindings of the snapshot's entities whose contract the merged semantic still declares."""
    kept = dict()
    bindings = snapshot.get("bindings", dict())
    for scope, section in snapshot["semantic"]["sections"].items():
        candidates = semantic["sections"].get(scope, dict()).get("entities", dict())
        for identifier, entity in section["entities"].items():
            candidate = candidates.get(identifier)
            binding = bindings.get(identifier)
            if candidate and binding is not None and entity_contract(candidate) == entity_contract(entity):
                kept[identifier] = binding
    return kept
