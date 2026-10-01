"""Keep a running publication's preconditions current as its own applies land."""

from __future__ import annotations

from ...material.calls import CONSUMER_KINDS, changed_calls, refresh_ops, with_refresh
from ...material.interfaces import tree as interface_tree
from ...storage.paths import object_path
from ...errors import SyncError
from ...sync.project import call_ok
from ..merge.trees import merge_trees
from . import planning, transactions
from .observe import capture


def measure(bridge, context, workspace, assets, source: str, observation: dict) -> dict:
    """Export ``assets`` again into the running observation; returns the entries before."""
    before = workspace.history.entries(observation["commit"])
    fresh = capture(bridge, context, workspace.store, sorted(assets), reference=source, fresh=assets)
    for asset in fresh["measured"]:
        if asset in fresh["revisions"]:
            observation["raw"][asset] = fresh["raw"][asset]
            observation["revisions"][asset] = fresh["revisions"][asset]
        else:
            observation["raw"].pop(asset, None)
            observation["revisions"].pop(asset, None)
    observation.update(commit=fresh["commit"], tree=fresh["tree"])
    observation["current"].update(fresh["measured"])
    return before


def replan(workspace, asset: str, source: str, candidate: str, fixed: dict, observation: dict, batch: dict, options: dict, code: str) -> None:
    """Merge the fixed intent for ``asset`` onto what UE now holds, and plan it again."""
    history = workspace.history
    tree, conflicts = merge_trees(history, fixed["base"], fixed["ours"], observation["commit"], workspace.schema, [asset])
    if conflicts:
        raise SyncError(code, "the state UE reached during this publication conflicts with the submitted intent",
                        dict(asset=asset, conflicts=conflicts))
    desired_id = history.entries(tree).get(asset)
    candidate_entries = history.entries(batch.get("candidate", candidate))
    if desired_id:
        candidate_entries[asset] = desired_id
    else:
        candidate_entries.pop(asset, None)
    batch["candidate"] = history.create(history.tree(candidate_entries), [observation["commit"], source],
                                        "Candidate after an in-publication change", operation="candidate")
    current = planning.snapshot(workspace, history.entries(observation["commit"]).get(asset))
    with interface_tree(workspace.store, candidate_entries):
        batch["units"][asset] = planning.unit(workspace, asset, planning.snapshot(workspace, desired_id), current,
                                             observation["raw"].get(asset), options)


def settle(bridge, context, workspace, asset: str, source: str, candidate: str, fixed: dict,
           observation: dict, batch: dict, options: dict) -> None:
    """Measure again what this unit guards when an earlier apply may have moved it.

    Compiling a published asset regenerates its class, and UE reinstances the
    component templates and pins built from it inside other packages without
    dirtying them. Revisions measured before that apply describe memory that is
    gone; sending them rejects the publication for a change it made itself.
    Only a new state of the unit's own asset needs a new plan: a dependency is
    guarded, not rewritten, so its new revision simply becomes the precondition.
    """
    stale = (set((asset,)) | set(batch["units"][asset]["dependencies"])) - observation["current"]
    if not stale:
        return
    before = measure(bridge, context, workspace, stale, source, observation)
    if workspace.history.entries(observation["commit"]).get(asset) != before.get(asset):
        replan(workspace, asset, source, candidate, fixed, observation, batch, options, "publication_side_effect_conflict")


def callers(bridge, context, workspace, function: str, source: str, observation: dict, batch: dict,
            options: dict, ahead: set[str]) -> list[dict]:
    """Refresh the material consumers of ``function`` this publication does not apply itself.

    A unit still ``ahead`` whose own document calls ``function`` rebuilds those
    nodes inside its own apply (``calls.with_refresh``). Every other consumer, a
    settled unit included, is refreshed here; a failure is that consumer's,
    returned with the function whose interface change caused it.
    """
    data = call_ok(bridge, "asset_referencers_get", dict(asset_path=function, limit=10000)).get("data") or dict()
    rows = data.get("items") or data.get("rows") or []
    if data.get("truncated") or int(data.get("total", len(rows))) > len(rows):
        raise SyncError("referencers_incomplete", "function refresh needs a complete referencer set", dict(function=function))
    assets = set(object_path(row[0]) for row in rows if isinstance(row, list) and row and (len(row) < 2 or row[1] == "hard"))
    calling = set(asset for asset, item in batch["units"].items() if changed_calls(item["kind"], item["dependencies"], set((function,))))
    assets.update(calling)
    assets -= (calling & ahead) | set((function,))
    unselected = assets - set(batch["selected"])
    deferred = batch.setdefault("deferred_callers", dict())
    for asset in sorted(unselected):
        functions = deferred.setdefault(asset, [])
        if function not in functions:
            functions.append(function)
    assets -= unselected
    if not assets:
        return []
    measure(bridge, context, workspace, assets, source, observation)
    failures = []
    for asset in sorted(assets):
        try:
            consumer(bridge, context, workspace, asset, set((function,)), source, observation, options)
        except SyncError as exc:
            failures.append(dict(asset=asset, function=function, code=exc.code, message=str(exc), details=exc.details))
    return failures


def consumer(bridge, context, workspace, asset: str, functions: set[str], source: str, observation: dict, options: dict) -> None:
    """Rebuild ``asset``'s call nodes of ``functions`` as an apply of their own."""
    stale = (set((asset,)) | functions) - observation["current"]
    if stale:
        measure(bridge, context, workspace, stale, source, observation)
    current = planning.snapshot(workspace, workspace.history.entries(observation["commit"]).get(asset))
    if not current or current["semantic"]["kind"] not in CONSUMER_KINDS:
        return
    kind = current["semantic"]["kind"]
    item = dict(asset=asset, kind=kind, dependencies=sorted(functions),
                payload=dict(asset_path=asset, kind=kind, plan=refresh_ops(functions), ids=dict()))
    record = transactions.request(workspace, item, source, observation["commit"], observation["commit"], observation, options, consume=False)
    transactions.execute(bridge, workspace, record)
    published = transactions.publish(workspace, record)
    transactions.adopt(observation, record, published, workspace.history)


def with_calls(item: dict, functions: set[str]) -> dict:
    return dict(item, payload=dict(item["payload"], plan=with_refresh(item["payload"]["plan"], functions)))
