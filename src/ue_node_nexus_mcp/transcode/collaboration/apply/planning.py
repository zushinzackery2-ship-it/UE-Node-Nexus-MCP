"""Merge states first, then derive each execution delta from current UE."""

from __future__ import annotations

from copy import deepcopy

from ...diff import build_plan
from ...scene.diff import build_plan as scene_plan
from ...scene.model import from_document as scene_model
from ...sync_deps import document_dependencies, order_assets
from ...sync_project import SyncError
from ..merge.trees import merge_trees, resolve_base
from ..semantic.decode import physical_ids, to_document
from ..semantic.validation import validate
from .observe import scene_selector


def snapshot(workspace, identifier: str | None) -> dict | None:
    return workspace.store.objects.data(identifier, "snapshot") if identifier else None


def selected_assets(workspace, source: str, paths) -> list[str]:
    from ..workspace.files import filename, select

    entries = workspace.history.entries(source)
    entries.update(workspace.history.entries(workspace.state["base"]))
    if not paths:
        return sorted(entries)
    # Only an asset with no projected file needs its name derived from state.
    files = dict(workspace.state["files"])
    for asset, value in entries.items():
        if asset not in files:
            files[asset] = filename(asset, snapshot(workspace, value))
    return select(workspace.root, files, paths)


def references(workspace, source: str, assets: list[str], explicit: bool) -> set[str]:
    """Assets the observation must cover beyond the ones being published.

    A narrowed selection has to be closed over its dependencies, because even an
    untouched member is published when the editor holds it dirty. A full
    workspace already observed every asset the project had, so there only an
    edited one can point somewhere new and the rest need no decoding.
    """
    entries, base = workspace.history.entries(source), workspace.history.entries(workspace.state["base"])
    partial = explicit or bool(workspace.state.get("sparse"))
    result = set()
    for asset in assets:
        identifier = entries.get(asset)
        if not identifier or (not partial and identifier == base.get(asset)):
            continue
        value = snapshot(workspace, identifier)
        result.update(document_dependencies(to_document(value), value["semantic"]["kind"]))
    return result - set(assets)


def merge(workspace, source: str, target: str, assets: list[str]) -> dict:
    history, store = workspace.history, workspace.store
    resolved = resolve_base(history, source, target, workspace.schema, store)
    if resolved["conflicts"]:
        return dict(base=resolved["base"], ancestors=resolved["bases"], ours=None, theirs=target, candidate=None,
                    conflicts=resolved["conflicts"], base_pair=resolved["pair"], base_inputs=resolved["inputs"])
    base, ancestors = resolved["base"], resolved["bases"]
    baselines = history.entries(base)
    source_entries, target_entries = history.entries(source), history.entries(target)
    # A partial publication consumes only the submitted state of each asset.
    # Resolutions remain incorporated even when the source branch keeps editing.
    for record in store.records("integration"):
        if record["asset"] in assets and record["workspace_id"] == workspace.state["id"]:
            if history.is_ancestor(record["source_commit"], source) and history.is_ancestor(record["published_commit"], target):
                if record.get("source_snapshot"):
                    baselines[record["asset"]] = record["source_snapshot"]
                else:
                    baselines.pop(record["asset"], None)
    ours = dict(target_entries)
    for asset in assets:
        if asset in source_entries:
            ours[asset] = source_entries[asset]
        else:
            ours.pop(asset, None)
    base_tree, ours_tree = history.tree(baselines), history.tree(ours)
    candidate, conflicts = merge_trees(history, base_tree, ours_tree, target, workspace.schema, assets)
    return dict(base=base_tree, ancestors=ancestors, ours=ours_tree, theirs=target, candidate=candidate, conflicts=conflicts)


def align_aliases(current: dict, candidate: dict) -> dict:
    result = deepcopy(current)
    desired = candidate["semantic"]["sections"]
    for scope, section in result["semantic"]["sections"].items():
        if section["name"] not in ("graph", "function", "macro"):
            continue
        other = desired.get(scope, dict()).get("entities", dict())
        for identifier, entity in section["entities"].items():
            if identifier in other:
                entity["alias"] = other[identifier]["alias"]
    return result


def unit(workspace, asset: str, candidate: dict | None, current: dict | None, raw: dict | None, options: dict) -> dict:
    kind = (candidate or current)["semantic"]["kind"]
    findings = validate(candidate, workspace.schema) if candidate else []
    if findings:
        raise SyncError("candidate_invalid", "merged candidate failed semantic validation", dict(asset=asset, diagnostics=findings))
    if kind == "stub" or kind == "niagara_emitter":
        if not current or not candidate or candidate["semantic_hash"] != current["semantic_hash"]:
            raise SyncError("read_only", "this asset kind is inspectable but has no writer", dict(asset=asset, kind=kind))
        return dict(asset=asset, kind=kind, empty=True, payload=dict(), dependencies=[])
    if candidate is None:
        if not options.get("allow_delete"):
            raise SyncError("delete_not_allowed", "asset deletion requires allow_delete=true", dict(asset=asset))
        if kind == "scene":
            desired = dict(map_path=raw["map_path"], name=raw["name"], actors=[])
            plan = scene_plan(raw, desired, scene_selector(asset, current), True)
            return dict(asset=asset, kind=kind, empty=False, payload=dict(kind=kind, delete_scene=True), scene_plan=plan, dependencies=[], risky=True)
        if any("opaque" in entity for section in current["semantic"]["sections"].values() for entity in section["entities"].values()):
            raise SyncError("restoration_unsupported", "this asset contains objects the bridge cannot recreate from a snapshot", dict(asset=asset))
        payload = dict(asset_path=asset, kind=kind, delete_asset=True, plan=[], ids=dict())
        return dict(asset=asset, kind=kind, empty=False, payload=payload, dependencies=[], risky=True)
    document = to_document(candidate)
    dependencies = sorted(document_dependencies(document, kind))
    if kind == "scene":
        if raw is None:
            raise SyncError("base_missing", "scene publication needs a current map observation")
        physical = dict(raw, aliases=physical_ids(candidate))
        desired, aliases = scene_model(document, physical)
        selector = scene_selector(asset, current)
        plan = scene_plan(raw, desired, selector, bool(options.get("allow_delete")))
        return dict(asset=asset, kind=kind, empty=not plan["ops"] and not raw.get("dirty"), scene_plan=plan, aliases=aliases,
                    payload=dict(kind=kind), dependencies=dependencies, risky=any(row["op"].startswith(("remove_", "delete_")) for row in plan["ops"]))
    aligned = align_aliases(current, candidate) if current else None
    ids = dict((alias, guid) for guid, alias in physical_ids(aligned).items()) if aligned else dict()
    plan = build_plan(document, to_document(aligned) if aligned else None, kind, ids)
    if plan.has_errors:
        raise SyncError("plan_invalid", "candidate cannot be expressed by this bridge", dict(asset=asset, diagnostics=[item.format() for item in plan.diagnostics]))
    if plan.risky() and not options.get("allow_delete"):
        raise SyncError("delete_not_allowed", "candidate removes entities; allow_delete=true is required", dict(asset=asset, plan=plan.summary()))
    return dict(asset=asset, kind=kind, empty=plan.empty and not (raw or dict()).get("dirty"), payload=plan.to_payload(),
                dependencies=dependencies, interface_changed=plan.interface_changed, summary=plan.summary(), risky=bool(plan.risky()))


def settled(asset: str, raw: dict | None, candidate: dict, current: dict, ours: dict) -> dict | None:
    """Nothing to apply here: identical merged and observed states, saved memory.

    Snapshots are content addressed, so agreement is one comparison. Publishing
    a whole project must not pay to decode every asset that no one touched.
    """
    if candidate.get(asset) != current.get(asset) or (raw or dict()).get("dirty"):
        return None
    if ours.get(asset) == current.get(asset):
        return dict(skip=True)
    # The workspace still submitted this state; the integration record is what
    # lets a later push know its source was published in full.
    return dict(asset=asset, kind="", empty=True, payload=dict(), dependencies=[])


def preflight(workspace, merged: dict, observation: dict, assets: list[str], options: dict) -> dict:
    candidate, current = workspace.history.entries(merged["candidate"]), workspace.history.entries(observation["commit"])
    ours = workspace.history.entries(merged["ours"]) if merged.get("ours") else dict()
    units, errors, documents = dict(), dict(), dict()
    conflicted = set(item["asset"] for item in merged["conflicts"])
    for asset in assets:
        if asset in conflicted:
            errors[asset] = dict(code="conflict", message="resolve the semantic conflicts")
            continue
        if asset not in candidate and asset not in current:
            continue
        unchanged = settled(asset, observation["raw"].get(asset), candidate, current, ours)
        if unchanged is not None:
            if not unchanged.get("skip"):
                units[asset] = unchanged
            continue
        desired, before = snapshot(workspace, candidate.get(asset)), snapshot(workspace, current.get(asset))
        try:
            units[asset] = unit(workspace, asset, desired, before, observation["raw"].get(asset), options)
            if desired:
                documents[asset] = (desired["semantic"]["kind"], to_document(desired))
            missing = set(units[asset]["dependencies"]) - current.keys() - candidate.keys()
            if missing:
                raise SyncError("dependency_missing", "candidate depends on unavailable assets", dict(assets=sorted(missing)))
            absent = (set(units[asset]["dependencies"]) & candidate.keys()) - current.keys() - set(assets)
            if absent:
                raise SyncError("dependency_not_selected", "select the required new assets", dict(assets=sorted(absent)))
        except SyncError as exc:
            errors[asset] = dict(code=exc.code, message=str(exc), details=exc.details)
    required = set(asset for asset, item in units.items() if item.get("interface_changed") or asset not in current or item["kind"] == "material")
    ordered = order_assets(documents, required) + [asset for asset in units if asset not in documents]
    for asset in ordered:
        blocked = set(units[asset]["dependencies"]) & errors.keys() if asset in units else set()
        if blocked:
            errors[asset] = dict(code="dependency_failed", message="blocked by invalid dependencies", assets=sorted(blocked))
    deleted = set(current) - set(candidate)
    if deleted:
        for asset, identifier in candidate.items():
            state = snapshot(workspace, identifier)
            references = document_dependencies(to_document(state), state["semantic"]["kind"]) & deleted
            for target in references:
                errors[target] = dict(code="referenced_asset", message="remaining candidate still references the deleted asset", consumer=asset)
    return dict(units=units, order=ordered, errors=errors)
