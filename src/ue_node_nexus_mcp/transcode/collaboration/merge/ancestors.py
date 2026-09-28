"""Commit a resolved virtual ancestor through the merge session service."""

from ...errors import SyncError
from .sessions import Sessions


def adopt_base(workspace, session: dict) -> str:
    """Record a resolved virtual ancestor so every later merge reuses it."""
    sessions = Sessions(workspace)
    sessions.check(session)
    if session["status"] != "ready":
        raise SyncError("unresolved_conflicts", "resolve the ancestor conflicts before continuing", sessions.report(session))
    commit = workspace.history.create(session["candidates"]["head"], [session["ours"], session["theirs"]],
                                      "virtual merge base", session["original"]["agent_id"], "virtual_base")
    key = session["metadata"]["base_pair"]
    previous = workspace.store.record("virtual_base", key)
    workspace.store.put_record("virtual_base", key, dict(commit=commit, inputs=[session["ours"], session["theirs"]]),
                               [commit], previous["generation"] if previous else 0)
    session.update(status="completed", result_commit=commit)
    sessions.save(session)
    return commit
