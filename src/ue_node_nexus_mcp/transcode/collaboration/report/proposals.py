"""Immutable previews bind precise refs, workspace bytes and schema facts."""

from __future__ import annotations

from uuid import uuid4

from ...sync_project import SyncError
from ..history.query import diff
from ..store.io import atomic_write, canonical
from ..workspace.files import capture_files, discover, hashes, select


def refs_for(history, options: dict, head: str) -> dict:
    refs = dict()
    for key in ("revision", "source", "onto"):
        revision = options.get(key)
        if revision:
            refs[revision] = history.resolve(revision, head)
    return refs


def create(workspace, action: str, paths, options: dict, preview: dict | None = None) -> dict:
    state, store, history = workspace.state, workspace.store, workspace.history
    working, files, _ = capture_files(workspace, delete=True)
    selected = select(workspace.root, files, paths)
    record = dict(id=uuid4().hex, workspace_id=state["id"], action=action,
                  options=dict((key, value) for key, value in options.items() if key not in ("dry_run", "proposal_id")), paths=paths,
                  original=dict(state), hashes=hashes(workspace.root, files), refs=refs_for(history, options, state["head"]),
                  schema=None, status="preview", generation=0)
    candidate = state["index"]
    if action not in ("push", "fetch"):
        from .simulation import simulate

        execution = dict(options)
        if action == "pull" and preview:
            execution["revision"] = preview["target"]
        simulated = simulate(workspace, action, paths, execution)
        candidate = simulated["candidate"]
        record["inputs"] = simulated.pop("objects")
        preview = dict(preview or dict(), **simulated)
    elif action == "push" and preview:
        candidate = preview["push_candidate"]
        record["inputs"] = [preview["source"], preview["target"], preview["base"]]
        record["refs"].setdefault(state["branch"], state["head"])
    record["records"] = dict()
    for key, category in (("merge_id", "session"), ("rebase_id", "rebase"), ("stash_id", "stash")):
        if options.get(key):
            item = store.record(category, options[key])
            record["records"][category + ":" + options[key]] = item["generation"] if item else None
    record["schema"] = workspace.schema.binding() if workspace.schema else None
    record["candidate"] = candidate
    if action == "switch":
        record["refs"][options["name"]] = history.resolve(options["name"], state["head"])
    record["preview"] = preview or dict()
    record["changes"] = diff(history, state["head"], candidate, selected if paths else None)
    roots = [state["head"], state["index"], history.tree(working), candidate, *record.get("inputs", []), *record["refs"].values()]
    store.put_record("proposal", record["id"], record, roots, expected=0)
    file = store.root / "proposals" / (record["id"] + ".json")
    atomic_write(file, canonical(record))
    return dict(record["preview"], action=action, dry_run=True, proposal_id=record["id"], workspace_id=state["id"], candidate=candidate,
                changes=record["changes"], artifact=str(file))


def check(workspace, action: str, paths, options: dict) -> dict:
    record = workspace.store.record("proposal", options["proposal_id"])
    if not record or record["workspace_id"] != workspace.state["id"] or record["action"] != action:
        raise SyncError("proposal_not_found", options["proposal_id"])
    original = record["options"]
    supplied = dict((key, value) for key, value in options.items() if key not in ("dry_run", "proposal_id"))
    if any(key not in original or value != original[key] for key, value in supplied.items()) or (paths is not None and paths != record["paths"]):
        raise SyncError("stale_proposal", "execution parameters differ from the preview")
    if any(workspace.history.resolve(name, workspace.state["head"]) != target for name, target in record["refs"].items()):
        raise SyncError("stale_proposal", "a source reference moved")
    for key, generation in record.get("records", dict()).items():
        category, identifier = key.split(":", 1)
        item = workspace.store.record(category, identifier)
        if (item["generation"] if item else None) != generation:
            raise SyncError("stale_proposal", "the previewed operation record changed")
    if action != "push":
        files = discover(workspace.root, workspace.state["files"])
        if record["original"]["generation"] != workspace.state["generation"] or hashes(workspace.root, files) != record["hashes"]:
            raise SyncError("stale_proposal", "workspace generation or file bytes changed")
    if workspace.schema and record.get("schema"):
        from ...schema.catalog import validate_binding

        validate_binding(workspace.schema, record["schema"])
    return dict(record, execution_options=dict(original, **options))
