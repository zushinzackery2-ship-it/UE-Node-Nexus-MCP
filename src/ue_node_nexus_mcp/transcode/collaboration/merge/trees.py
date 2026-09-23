"""Merge project trees and synthesize virtual common ancestors."""

from __future__ import annotations

from ...sync_project import SyncError
from ..history import History
from ..semantic.snapshot import rebind
from ..store.io import digest
from .engine import merge_snapshots


def aligned(history: History, identifiers: list, schema) -> tuple[list, list, str | None]:
    """The three inputs read under one schema environment, and their stored ids.

    A conflict names the snapshots it compared, so a re-read input is recorded
    and named instead of the one it replaced. When the text no longer encodes the
    inputs keep their own environments and the reason travels with the conflict.
    """
    snapshots = [history.store.objects.data(identifier, "snapshot") if identifier else None for identifier in identifiers]
    try:
        rebound = [rebind(snapshot, schema) for snapshot in snapshots]
    except SyncError as exc:
        return snapshots, identifiers, f"{exc.code}: {exc}"
    stored = [identifier if new is old else history.store.snapshot(new, schema)
              for identifier, old, new in zip(identifiers, snapshots, rebound)]
    return rebound, stored, None


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
        snapshots, identifiers, unreadable = aligned(history, identifiers, schema)
        merged = merge_snapshots(*snapshots, schema)
        if merged.candidate is None:
            result.pop(asset, None)
        else:
            result[asset] = history.store.snapshot(merged.candidate, schema)
        for item in merged.conflicts:
            item.update(asset=asset, layer=layer, kind=(snapshots[1] or snapshots[2])["semantic"]["kind"], snapshots=identifiers)
            item["conflict_id"] = digest(dict(layer=layer, inputs=identifiers, id=item["conflict_id"]))[:24]
            if unreadable and item["conflict_type"] == "history_schema_conflict":
                item["reason"] += f"; re-reading under the current schema failed ({unreadable})"
            conflicts.append(item)
    return history.tree(result), conflicts


def pair_key(left: str, right: str, selected: list[str] | None = None) -> str:
    """A narrowed ancestor resolution answers only for the assets it looked at.

    Recording it under the project-wide key would let a later, wider merge reuse
    an ancestor that never examined the assets it now needs.
    """
    material = sorted([left, right])
    return digest(material if selected is None else dict(pair=material, scope=sorted(selected)))


def resolve_base(history: History, ours: str, theirs: str, schema=None, store=None, selected: list[str] | None = None) -> dict:
    """The virtual ancestor, or the exact inputs whose merge would produce it.

    Two independent ancestors that disagree cannot be collapsed by guessing. The
    conflicting triple is returned so the caller can open a durable resolution
    whose result is recorded and reused by every later attempt.

    ``selected`` scopes the ancestor merge to the assets a publication actually
    covers. Without it a one-asset push inherits every historical validation
    conflict the project ever accumulated, none of which it can act on.
    """
    bases = history.merge_bases(ours, theirs)
    if not bases:
        raise SyncError("unrelated_history", "versions have no recorded common ancestor")
    merged = bases[0]
    for next_base in bases[1:]:
        key = pair_key(merged, next_base, selected)
        recorded = store.record("virtual_base", key) if store else None
        if recorded:
            merged = recorded["commit"]
            continue
        inner = resolve_base(history, merged, next_base, schema, store, selected)
        if inner["conflicts"]:
            return dict(inner, bases=bases)
        tree, conflicts = merge_trees(history, inner["base"], merged, next_base, schema, selected, "base")
        if conflicts:
            return dict(base=inner["base"], bases=bases, conflicts=conflicts, pair=key,
                        inputs=[inner["base"], merged, next_base])
        merged = history.create(tree, [merged, next_base], "virtual merge base", operation="virtual_base")
    return dict(base=merged, bases=bases, conflicts=[], pair="", inputs=[])


def common_base(history: History, ours: str, theirs: str, schema=None, store=None) -> tuple[str, list[str]]:
    resolved = resolve_base(history, ours, theirs, schema, store)
    if resolved["conflicts"]:
        raise SyncError("base-conflict", "multiple common ancestors require resolution",
                        dict(ancestors=resolved["bases"], conflicts=resolved["conflicts"], inputs=resolved["inputs"]))
    return resolved["base"], resolved["bases"]


def adopt_base(workspace, session: dict) -> str:
    """Record a resolved virtual ancestor so every later merge reuses it."""
    from .sessions import Sessions

    sessions = Sessions(workspace)
    sessions.check(session)
    if session["status"] != "ready":
        raise SyncError("unresolved_conflicts", "resolve the ancestor conflicts before continuing", sessions.report(session))
    commit = workspace.history.create(session["candidates"]["head"], [session["ours"], session["theirs"]],
                                      "virtual merge base", session["original"]["agent_id"], "virtual_base")
    key = session["metadata"]["base_pair"]
    previous = workspace.store.record("virtual_base", key)
    workspace.store.put_record("virtual_base", key, dict(commit=commit, inputs=[session["ours"], session["theirs"]]),
                               [commit], previous["generation"] if previous else 0)
    session.update(status="completed", result_commit=commit)
    sessions.save(session)
    return commit
