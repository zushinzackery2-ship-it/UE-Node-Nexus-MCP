"""Verify persistent IDs through paging, failed revisions and batched edits."""

from __future__ import annotations

import uuid

from .fixtures import transform


def exercise(workspace) -> dict:
    session = workspace.session
    result = dict()
    for actor in workspace.snapshot()["actors"]:
        for component in actor["components"]:
            if "instance_data" not in component:
                continue
            path = actor["actor_path"] + "." + component["name"]
            initial = session.require("component_instances_get", component_path=path, limit=1)
            assert initial["identity_bound"] and initial["total"] == 4, initial
            tail = session.require("component_instances_get", component_path=path, offset=1, limit=10, revision=initial["revision"])
            before = initial["instances"] + tail["instances"]
            assert len(before) == 4 and tail["next_offset"] == -1, tail
            added = uuid.uuid4().hex.upper()
            ops = [dict(op="remove", id=before[0]["id"]),
                   dict(op="update", id=before[1]["id"], transform=transform(x=321), custom_data=[0.75]),
                   dict(op="add", id=added, transform=transform(x=987), custom_data=[0.5])]
            session.require("component_instances_patch", component_path=path, revision=initial["revision"], ops=ops, dry_run=True)
            assert session.require("component_instances_get", component_path=path)["revision"] == initial["revision"]
            session.require("component_instances_patch", component_path=path, revision=initial["revision"], ops=ops, dry_run=False, save=True)
            current = session.require("component_instances_get", component_path=path)
            ids = set(item["id"] for item in current["instances"])
            assert ids == set(item["id"] for item in before[1:]) | set((added,)), current
            changed = next(item for item in current["instances"] if item["id"] == before[1]["id"])
            assert changed["custom_data"] == [0.75] and "321" in changed["transform"], changed
            stale = session.call("component_instances_patch", dict(component_path=path, revision=initial["revision"], ops=ops, dry_run=False))
            assert stale.get("ok") is False, stale
            assert session.require("component_instances_get", component_path=path)["revision"] == current["revision"]
            result[component["class"]] = dict(component=path, remaining_ids=sorted(ids), stale_revision_rejected=True)
    assert len(result) == 2, result
    pulled = workspace.sync("pull", force="ue")
    assert not pulled.get("scene_error_count", 0), pulled
    return result
