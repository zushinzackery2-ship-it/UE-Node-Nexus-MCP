"""Refresh function consumers, then remerge their fixed original intent."""

from __future__ import annotations

from ...paths import object_path
from ...sync_project import SyncError, call_ok
from ..merge.trees import merge_trees
from . import planning, transactions
from .observe import capture


def callers(bridge, context, workspace, function: str, source: str, candidate: str, fixed: dict,
            observation: dict, batch: dict, options: dict) -> None:
    from .publish import adopt

    data = call_ok(bridge, "asset_referencers_get", dict(asset_path=function, limit=10000)).get("data") or dict()
    rows = data.get("items") or data.get("rows") or []
    if data.get("truncated") or int(data.get("total", len(rows))) > len(rows):
        raise SyncError("referencers_incomplete", "function refresh needs a complete referencer set")
    assets = set(object_path(row[0]) for row in rows if isinstance(row, list) and row and (len(row) < 2 or row[1] == "hard"))
    assets.update(asset for asset, item in batch["units"].items() if function in item["dependencies"] and item["kind"] in ("material", "material_function"))
    assets.discard(function)
    if not assets:
        return
    fresh = capture(bridge, context, workspace.store, sorted(assets), reference=source)
    observation["raw"].update(fresh["raw"])
    observation["revisions"].update(fresh["revisions"])
    observation.update(commit=fresh["commit"], tree=fresh["tree"])
    for asset in sorted(assets):
        entries = workspace.history.entries(observation["commit"])
        current = planning.snapshot(workspace, entries.get(asset))
        if not current or current["semantic"]["kind"] not in ("material", "material_function"):
            continue
        item = dict(asset=asset, kind=current["semantic"]["kind"], dependencies=[function],
                    payload=dict(asset_path=asset, kind=current["semantic"]["kind"], plan=[dict(op="refresh_function_calls", function=function)], ids=dict()))
        record = transactions.request(workspace, item, source, observation["commit"], observation["commit"], observation, options)
        transactions.execute(bridge, workspace, record)
        published = transactions.publish(workspace, record, consume=False)
        adopt(observation, record, published, workspace.history)
        if asset not in batch["units"]:
            continue
        tree, conflicts = merge_trees(workspace.history, fixed["base"], fixed["ours"], published, workspace.schema, [asset])
        if conflicts:
            raise SyncError("caller_refresh_conflict", "function refresh conflicts with the original caller intent", dict(asset=asset, conflicts=conflicts))
        desired_id = workspace.history.entries(tree).get(asset)
        candidate_entries = workspace.history.entries(batch.get("candidate", candidate))
        if desired_id:
            candidate_entries[asset] = desired_id
        else:
            candidate_entries.pop(asset, None)
        batch["candidate"] = workspace.history.create(workspace.history.tree(candidate_entries), [published, source], "Candidate after function interface refresh", operation="candidate")
        current = planning.snapshot(workspace, workspace.history.entries(published).get(asset))
        batch["units"][asset] = planning.unit(workspace, asset, planning.snapshot(workspace, desired_id), current, observation["raw"][asset], options)
