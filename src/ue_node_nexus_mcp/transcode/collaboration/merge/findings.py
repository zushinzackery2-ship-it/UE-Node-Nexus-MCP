"""Schema findings on the states one merge layer created, and their decisions."""

from __future__ import annotations

from ..semantic.validation import validate
from ..store.io import digest
from .engine import at, shown, wire
from .resolutions import resolve_tree


class Findings:
    def __init__(self, history, schema) -> None:
        self.history, self.store, self.schema = history, history.store, schema

    def check_tree(self, session: dict, tree: str, layer: str, inputs: tuple, conflicts: list[dict], all_conflicts: list[dict]) -> str:
        """Findings on the states this layer created, and on nothing else.

        A state that either input already recorded is history, not something this
        operation wrote: its findings are neither caused by the merge nor fixed by
        resolving it. Validating those turned a one-asset push in a full workspace
        into a hundred historical findings, and made the preview, which only saw
        merged states, disagree with the session it would open.
        """
        scope = set(session["selected"]) if session["selected"] is not None else None
        recorded = [self.history.entries(item) for item in inputs]
        for asset, snapshot_id in self.history.entries(tree).items():
            if scope is not None and asset not in scope:
                continue
            if any(entries.get(asset) == snapshot_id for entries in recorded):
                continue
            tree = self.check_asset(session, tree, layer, asset, snapshot_id, conflicts, all_conflicts)
        return tree

    def check_asset(self, session: dict, tree: str, layer: str, asset: str, snapshot_id: str,
                    conflicts: list[dict], all_conflicts: list[dict]) -> str:
        snapshot = self.store.objects.data(snapshot_id, "snapshot")
        for issue in validate(snapshot, self.schema):
            if any(item["asset"] == asset and item["field_path"] == issue["path"] and item["layer"] == layer for item in conflicts):
                continue
            item = finding(layer, asset, snapshot, snapshot_id, issue)
            decision = session["resolutions"].get(item["conflict_id"])
            if decision:
                tree = resolve_tree(self.history, tree, item, decision)
                if self.unresolved(tree, asset, issue):
                    conflicts.append(item)
            else:
                conflicts.append(item)
            all_conflicts.append(item)
        return tree

    def unresolved(self, tree: str, asset: str, issue: dict) -> bool:
        """Whether the decision left the finding it answered still standing."""
        updated_id = self.history.entries(tree).get(asset)
        updated = self.store.objects.data(updated_id, "snapshot") if updated_id else None
        remaining = validate(updated, self.schema) if updated else []
        return any(issue["path"] == entry["path"] and issue["conflict_type"] == entry["conflict_type"] for entry in remaining)


def finding(layer: str, asset: str, snapshot: dict, snapshot_id: str, issue: dict) -> dict:
    value = shown(wire(at(snapshot["semantic"], issue["path"])))
    return dict(conflict_id=digest(dict(layer=layer, snapshot=snapshot_id, issue=issue))[:24], asset=asset,
                field_path=issue["path"], entity_path=issue["path"][:4], kind=snapshot["semantic"]["kind"],
                conflict_type=issue["conflict_type"], reason=issue["reason"], layer=layer,
                base=value, ours=value, theirs=value,
                snapshots=[snapshot_id] * 3, allowed_resolutions=["custom", "delete", "rename"])
