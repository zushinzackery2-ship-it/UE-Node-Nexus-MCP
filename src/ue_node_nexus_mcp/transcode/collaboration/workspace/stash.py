"""Stashes retain both local layers and original file bytes."""

from __future__ import annotations

from uuid import uuid4

from ...sync_project import SyncError
from ..merge.sessions import Sessions
from ..store.io import confined
from .files import capture_files
from .projection import blob


def push(workspace, message="") -> dict:
    entries, files, _ = capture_files(workspace, delete=True)
    if not workspace.status()["dirty"]:
        return dict(status="unchanged")
    tree = workspace.history.tree(entries)
    identifier = uuid4().hex
    originals = dict()
    for asset, relative in files.items():
        path = confined(workspace.root, relative)
        originals[relative] = blob(workspace.store, path.read_bytes() if path.is_file() else None)
    record = dict(id=identifier, workspace_id=workspace.state["id"], message=message, head=workspace.state["head"],
                  index=workspace.state["index"], files_tree=tree, files=files, originals=originals)
    roots = [record["head"], record["index"], tree, *[item for item in originals.values() if item]]
    workspace.store.put_record("stash", identifier, record, roots, expected=0)
    workspace.install(workspace.state["head"], reason="stash_push")
    return dict(stash_id=identifier, status="stashed")


def apply(workspace, identifier: str, pop=False, auto_finish=True) -> dict:
    stash = workspace.store.record("stash", identifier)
    if stash is None:
        raise SyncError("stash_not_found", identifier)
    working, _, _ = capture_files(workspace, delete=True)
    working_tree = workspace.history.tree(working)
    stages = [("head", stash["head"], workspace.state["index"], stash["index"]),
              ("local_files", workspace.state["index"], "head", working_tree),
              ("files", stash["index"], "local_files", stash["files_tree"])]
    sessions = Sessions(workspace)
    report = sessions.start("stash_pop" if pop else "stash_apply", stash["head"], stash["index"], [],
                            layer_sources=stages, no_commit=True, stash_id=identifier, pop=pop)
    if report["status"] == "ready" and auto_finish:
        return finish(workspace, report["merge_id"])
    return report


def finish(workspace, merge_id: str) -> dict:
    sessions = Sessions(workspace)
    session = sessions.get(merge_id)
    result = sessions.finish(merge_id)
    if result["status"] == "completed" and session["metadata"].get("pop"):
        stash = workspace.store.record("stash", session["metadata"]["stash_id"])
        if stash:
            workspace.store.drop_record("stash", stash["id"], stash["generation"])
    return result


def drop(workspace, identifier: str) -> dict:
    stash = workspace.store.record("stash", identifier)
    if not stash:
        raise SyncError("stash_not_found", identifier)
    if any(item["status"] not in ("aborted", "completed") and item.get("metadata", dict()).get("stash_id") == identifier for item in workspace.store.records("session")):
        raise SyncError("stash_in_use", "stash is part of an active merge")
    workspace.store.drop_record("stash", identifier, stash["generation"])
    return dict(stash_id=identifier, status="dropped")
