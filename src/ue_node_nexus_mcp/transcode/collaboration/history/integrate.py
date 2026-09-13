"""Merge, revert and cherry-pick share fixed semantic three-way inputs."""

from __future__ import annotations

from ...sync_project import SyncError
from ..merge.sessions import Sessions
from ..merge.trees import common_base


def parent(history, identifier: str, mainline: int | None) -> str:
    parents = history.commit(identifier)["parents"]
    if len(parents) > 1 and mainline is None:
        raise SyncError("mainline_required", "merge commits require an explicit 1-based mainline parent")
    index = 1 if mainline is None else mainline
    if not parents:
        if mainline is not None:
            raise SyncError("invalid_mainline", "root commit has no parent")
        return history.tree(dict())
    if not 1 <= index <= len(parents):
        raise SyncError("invalid_mainline", "parent index is out of range")
    return parents[index - 1]


def integrate(workspace, operation: str, revision: str, mainline=None, paths=None,
              message=None, source_ref=None, auto_finish=True) -> dict:
    history, head = workspace.history, workspace.state["head"]
    source = history.resolve(revision, head)
    if operation != "pull":
        workspace.require_clean()
    parents = [head]
    metadata = dict(message=message)
    if operation in ("merge", "pull"):
        base, ancestors = common_base(history, head, source, workspace.schema)
        metadata["ancestors"] = ancestors
        if history.is_ancestor(source, head) and operation == "merge":
            return dict(status="unchanged", commit_id=head)
        parents.append(source)
        theirs = source
    elif operation in ("revert", "cherry-pick"):
        ancestor = parent(history, source, mainline)
        base, theirs = (source, ancestor) if operation == "revert" else (ancestor, source)
        metadata.update(source_commit=source, mainline=mainline)
    else:
        raise SyncError("invalid_history_operation", operation)
    sessions = Sessions(workspace)
    report = sessions.start(operation, base, theirs, parents, replay_layers=operation == "pull", source_ref=source_ref, selected=paths, **metadata)
    if report["status"] == "ready" and auto_finish:
        return sessions.finish(report["merge_id"], message)
    return report
