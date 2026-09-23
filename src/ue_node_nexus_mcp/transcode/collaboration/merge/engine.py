"""Linear indexed three-way comparison, independent of UE execution plans."""

from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass, field

from ..semantic.validation import validate
from ..store.io import canonical, digest
from .order import merge_order

MISSING = object()
# A conflict names the values it is about; it does not have to carry them. An
# asset-level finding sits at the empty path, whose value is the whole state, so
# three inline copies per finding made one conflict worth megabytes and a session
# worth tens of them. A larger value stays in the snapshot the conflict names.
INLINE_VALUE_BYTES = 2048


def wire(value):
    return dict(state="missing") if value is MISSING else value


def shown(value):
    """The value as a conflict record carries it: inline, or named by digest."""
    size = len(canonical(value))
    if size <= INLINE_VALUE_BYTES:
        return value
    return dict(state="elided", bytes=size, digest=digest(value)[:24])


def at(value, path):
    for key in path:
        if isinstance(value, dict):
            value = value.get(key, MISSING)
        elif isinstance(value, list) and isinstance(key, int) and key < len(value):
            value = value[key]
        else:
            return MISSING
    return value


@dataclass
class MergeResult:
    candidate: dict | None
    conflicts: list[dict] = field(default_factory=list)


class Comparison:
    def __init__(self, asset: str) -> None:
        self.asset = asset
        self.conflicts = []
        self.conflict_ids = set()

    def conflict(self, path, base, ours, theirs, code="value-conflict", reason="both sides changed the same field"):
        values = dict(base=wire(base), ours=wire(ours), theirs=wire(theirs))
        identifier = digest(dict(asset=self.asset, path=path, code=code, **values))[:24]
        entry = dict(conflict_id=identifier, asset=self.asset, entity_path=list(path[:4]), field_path=list(path),
                     conflict_type=code, reason=reason, allowed_resolutions=["ours", "theirs", "base", "custom", "delete", "rename"],
                     **dict((side, shown(item)) for side, item in values.items()))
        if identifier not in self.conflict_ids:
            self.conflicts.append(entry)
            self.conflict_ids.add(identifier)
        return ours

    def merge(self, base, ours, theirs, path=()):
        if ours == theirs or theirs == base:
            return ours
        if ours == base:
            return theirs
        if path and path[-1] == "order" and all(isinstance(item, list) for item in (base, ours, theirs)):
            order, error = merge_order(base, ours, theirs)
            if error:
                self.conflict(path, base, ours, theirs, "order-conflict", error)
            return order
        if ours is MISSING or theirs is MISSING:
            return self.conflict(path, base, ours, theirs, "modify-delete", "one side removed an entity or field changed by the other")
        if base is MISSING and len(path) >= 2 and path[-2] == "entities":
            return self.conflict(path, base, ours, theirs, "add-add", "both sides introduced different declarations with the same identity")
        if all(isinstance(item, dict) for item in (ours, theirs)):
            original = base if isinstance(base, dict) else dict()
            if "state" in ours or "state" in theirs or (len(path) >= 2 and path[-2] == "links"):
                return self.conflict(path, base, ours, theirs)
            if "type" in original and "alias" in original:
                if (ours.get("type") != original["type"] or theirs.get("type") != original["type"]):
                    return self.conflict(path, base, ours, theirs, "type-conflict", "type replacement overlaps changes to the same declaration")
                if original.get("type") == "@opaque":
                    return self.conflict(path, base, ours, theirs, "opaque-conflict", "opaque objects require a whole-object choice")
            result = dict()
            for key in sorted(original.keys() | ours.keys() | theirs.keys()):
                item = self.merge(original.get(key, MISSING), ours.get(key, MISSING), theirs.get(key, MISSING), (*path, key))
                if item is not MISSING:
                    result[key] = item
            return result
        return self.conflict(path, base, ours, theirs)


def merge_snapshots(base: dict | None, ours: dict | None, theirs: dict | None, schema=None) -> MergeResult:
    asset = (ours or theirs or base)["semantic"]["header"]["asset"]
    compare = Comparison(asset)
    inputs = [snapshot["semantic"] if snapshot else MISSING for snapshot in (base, ours, theirs)]
    semantic = compare.merge(*inputs)
    if semantic is MISSING:
        return MergeResult(None, compare.conflicts)
    evidence = theirs or ours or base
    candidate = dict(evidence)
    candidate["semantic"] = deepcopy(semantic)
    candidate["bindings"] = dict((base or dict()).get("bindings", dict()))
    candidate["bindings"].update((ours or dict()).get("bindings", dict()))
    candidate["bindings"].update((theirs or dict()).get("bindings", dict()))
    if ours and theirs and ours["schema_key"] != theirs["schema_key"]:
        # Reached only when the inputs could not be re-read under one environment
        # (no schema is available, or the text no longer encodes under it).
        compare.conflict([], *inputs, "history_schema_conflict", "snapshots bind different schema environments")
    for item in validate(candidate, schema):
        path = item["path"]
        compare.conflict(path, *(at(value, path) for value in inputs), item["conflict_type"], item["reason"])
    candidate["semantic_hash"] = digest(candidate["semantic"])
    return MergeResult(candidate, compare.conflicts)
