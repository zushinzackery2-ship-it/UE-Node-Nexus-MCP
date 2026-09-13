"""Merge project trees and synthesize virtual common ancestors."""

from __future__ import annotations

from ...sync_project import SyncError
from ..history import History
from ..store.io import digest
from .engine import merge_snapshots


def merge_trees(history: History, base: str, ours: str, theirs: str, schema=None,
                selected: list[str] | None = None, layer: str = "head") -> tuple[str, list[dict]]:
    trees = [history.entries(item) for item in (base, ours, theirs)]
    result = dict(trees[1])
    conflicts = []
    assets = set(selected) if selected is not None else trees[0].keys() | trees[1].keys() | trees[2].keys()
    for asset in sorted(assets):
        identifiers = [tree.get(asset) for tree in trees]
        if identifiers[1] == identifiers[2] or identifiers[2] == identifiers[0]:
            continue
        if identifiers[1] == identifiers[0]:
            if identifiers[2]:
                result[asset] = identifiers[2]
            else:
                result.pop(asset, None)
            continue
        snapshots = [history.store.objects.data(identifier, "snapshot") if identifier else None for identifier in identifiers]
        merged = merge_snapshots(*snapshots, schema)
        if merged.candidate is None:
            result.pop(asset, None)
        else:
            result[asset] = history.store.snapshot(merged.candidate, schema)
        for item in merged.conflicts:
            item.update(asset=asset, layer=layer, kind=(snapshots[1] or snapshots[2])["semantic"]["kind"], snapshots=identifiers)
            item["conflict_id"] = digest(dict(layer=layer, inputs=identifiers, id=item["conflict_id"]))[:24]
            conflicts.append(item)
    return history.tree(result), conflicts


def common_base(history: History, ours: str, theirs: str, schema=None) -> tuple[str, list[str]]:
    bases = history.merge_bases(ours, theirs)
    if not bases:
        raise SyncError("unrelated_history", "versions have no recorded common ancestor")
    merged = bases[0]
    for next_base in bases[1:]:
        previous, _ = common_base(history, merged, next_base, schema)
        tree, conflicts = merge_trees(history, previous, merged, next_base, schema, layer="base")
        if conflicts:
            raise SyncError("base-conflict", "multiple common ancestors require resolution", dict(ancestors=bases, conflicts=conflicts))
        merged = history.create(tree, [merged, next_base], "virtual merge base", operation="virtual_base")
    return merged, bases
