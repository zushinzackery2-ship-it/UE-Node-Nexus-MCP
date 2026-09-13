"""Local reference, reset and restore commands."""

from __future__ import annotations

from copy import deepcopy

from ...sync_project import SyncError
from ..merge.resolutions import set_path
from ..semantic.validation import validate
from ..store.io import digest
from ..store.refs import move_ref
from ..workspace.files import capture_files, filename, select


def branch(workspace, name=None, revision="HEAD", delete=False, expected=None) -> dict:
    store, history = workspace.store, workspace.history
    if not name:
        return dict(branches=store.refs("refs/heads/"))
    ref = name if name.startswith("refs/heads/") else "refs/heads/" + name
    target = history.resolve(revision, workspace.state["head"])
    if delete:
        if any(item["branch"] == ref and not item.get("closed") for item in store.records("workspace")):
            raise SyncError("branch_in_use", "close or switch workspaces before removing their branch")
        if expected is None:
            raise SyncError("expected_ref_required", "branch removal requires its expected commit")
        target = None
    store.move(ref, target, expected, workspace.state["agent_id"], "branch")
    return dict(ref=ref, commit_id=target)


def tag(workspace, name=None, revision="HEAD", message="", expected=None) -> dict:
    store, history = workspace.store, workspace.history
    if not name:
        return dict(tags=store.refs("refs/tags/"))
    ref = name if name.startswith("refs/tags/") else "refs/tags/" + name
    commit = history.resolve(revision, workspace.state["head"])
    identifier = store.objects.put("tag", dict(commit=commit, message=message, author=workspace.state["agent_id"]), [commit])
    store.move(ref, identifier, expected, workspace.state["agent_id"], "tag")
    return dict(ref=ref, object_id=identifier, commit_id=commit)


def switch(workspace, name: str) -> dict:
    workspace.require_clean()
    ref = name if name.startswith("refs/heads/") else "refs/heads/" + name
    target = workspace.store.ref(ref)
    if not target:
        raise SyncError("branch_not_found", name)
    return workspace.install(target, branch=ref, reason="switch", base=target)


def reset(workspace, revision: str, mode="mixed") -> dict:
    if mode not in ("soft", "mixed", "hard"):
        raise SyncError("invalid_reset", "mode must be soft, mixed or hard")
    history, store, before = workspace.history, workspace.store, workspace.state
    history.private(before["head"], before["id"])
    target = history.resolve(revision, before["head"])
    safety = history.safety(before["id"], before["head"], "reset")
    if mode == "hard":
        workspace.install(target, reason="reset", base=target)
    else:
        after = dict(before, head=target)
        if mode == "mixed":
            after["index"] = history.commit(target)["tree"]
        with store.db.connection(write=True) as connection:
            move_ref(connection, before["branch"], target, before["head"], before["id"], "reset")
            workspace.persist(after, connection=connection)
    return dict(commit_id=target, safety_ref=safety, mode=mode, workspace_id=before["id"])


def restore(workspace, revision: str, paths=None, destination="files", entity: str | None = None, field_path: list | None = None) -> dict:
    if destination not in ("files", "index", "both"):
        raise SyncError("invalid_restore", "destination must be files, index or both")
    history, store = workspace.history, workspace.store
    source = history.entries(history.resolve(revision, workspace.state["head"]))
    working, files, _ = capture_files(workspace, delete=True)
    current = history.entries(workspace.state["index"]) if destination == "index" else working
    for asset, identifier in source.items():
        files.setdefault(asset, filename(asset, store.objects.data(identifier, "snapshot")))
    for asset in select(workspace.root, files, paths):
        if asset not in source:
            current.pop(asset, None)
            continue
        identifier = source[asset]
        desired = store.objects.data(identifier, "snapshot")
        if entity or field_path:
            if asset not in current:
                raise SyncError("restore_entity_missing", asset)
            candidate = deepcopy(store.objects.data(current[asset], "snapshot"))
            path = field_path
            if entity:
                match = next(((scope, sid) for scope, section in desired["semantic"]["sections"].items() for sid, item in section["entities"].items() if sid == entity or item["alias"] == entity), None)
                if match is None:
                    raise SyncError("entity_not_found", entity)
                path = ["sections", match[0], "entities", match[1]]
            from ..merge.engine import MISSING, at

            value = at(desired["semantic"], path)
            candidate["semantic"] = set_path(candidate["semantic"], path, value, value is MISSING)
            candidate["bindings"].update(desired["bindings"])
            candidate["semantic_hash"] = digest(candidate["semantic"])
            desired = candidate
        findings = validate(desired, workspace.schema)
        if findings:
            raise SyncError("history_schema_conflict", "restored state fails current semantic constraints", dict(asset=asset, diagnostics=findings))
        current[asset] = workspace.snapshot(desired)
    tree = history.tree(current)
    if destination == "index":
        workspace.persist(dict(workspace.state, index=tree, files=files))
    else:
        workspace.install(workspace.state["head"], index=tree if destination == "both" else workspace.state["index"], files_tree=tree, reason="restore")
    return workspace.status()


def close(workspace) -> dict:
    workspace.require_clean()
    status = workspace.status()
    if status["sessions"] or status["applies"]:
        raise SyncError("workspace_active", "workspace has an unfinished session or publication")
    import time

    workspace.persist(dict(workspace.state, closed=True, closed_at=time.time()))
    return dict(workspace_id=workspace.state["id"], closed=True, branch=workspace.state["branch"])
