"""Reconcile an interrupted creation with native and generated component defaults."""

from __future__ import annotations

from typing import Any


def preserve_created_defaults(base: dict[str, Any] | None, current: dict[str, Any], desired: dict[str, Any]) -> dict[str, Any]:
    result = dict(desired, actors=[])
    accepted = dict((row["id"], row) for row in (base or dict()).get("actors", []))
    live = dict((row["id"], row) for row in current.get("actors", []))
    for actor in desired["actors"]:
        if actor["id"] not in live:
            result["actors"].append(actor)
            continue
        actor = dict(actor, components=list(actor.get("components", [])))
        result["actors"].append(actor)
        declared = set(row["id"] for row in actor.get("components", []))
        old_components = set(row["id"] for row in accepted.get(actor["id"], dict()).get("components", []))
        for component in live[actor["id"]].get("components", []):
            if component["id"] not in declared and component["id"] not in old_components and not component.get("removable", False):
                actor["components"].append(component)
    return result
