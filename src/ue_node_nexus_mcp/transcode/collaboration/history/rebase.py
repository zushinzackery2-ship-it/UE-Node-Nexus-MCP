"""Durable pick/squash/drop replay; publish the private ref after all steps."""

from __future__ import annotations

from uuid import uuid4

from ...sync_project import SyncError
from ..merge.sessions import Sessions
from .integrate import parent


def save(workspace, record: dict) -> None:
    roots = [record["original"]["head"], record["original"]["index"], record["onto"], record["current"]]
    roots.extend(item["commit"] for item in record["steps"])
    record["generation"] = workspace.store.put_record("rebase", record["id"], record, roots, record["generation"])


def start(workspace, onto: str, steps: list[dict] | None = None) -> dict:
    workspace.require_clean()
    history, state = workspace.history, workspace.state
    history.private(state["head"], state["id"])
    target = history.resolve(onto, state["head"])
    selected = history.ancestors(state["head"]) - history.ancestors(target)
    published = workspace.store.ref("refs/ue/published")
    if published and selected & history.ancestors(published):
        raise SyncError("published_history", "rebase would rewrite published commits")
    ordered = sorted(selected, key=lambda item: (history.commit(item)["generation"], item))
    sequence = steps if steps is not None else [dict(commit=item, action="pick") for item in ordered]
    for item in sequence:
        item["commit"] = history.resolve(item["commit"], state["head"])
        if item["commit"] not in selected or item.get("action", "pick") not in ("pick", "squash", "drop"):
            raise SyncError("invalid_rebase_step", str(item))
        parent(history, item["commit"], item.get("mainline"))
    if len(set(item["commit"] for item in sequence)) != len(sequence):
        raise SyncError("invalid_rebase_step", "duplicate replay commit")
    record = dict(id=uuid4().hex, workspace_id=state["id"], original=dict(state), onto=target,
                  current=target, cursor=0, steps=sequence, session=None, status="running", generation=0,
                  safety_ref=history.safety(state["id"], state["head"], "rebase"))
    save(workspace, record)
    return resume(workspace, record["id"])


def resume(workspace, identifier: str) -> dict:
    record = workspace.store.record("rebase", identifier)
    if not record or record["workspace_id"] != workspace.state["id"]:
        raise SyncError("rebase_not_found", identifier)
    if record["status"] in ("completed", "aborted"):
        return record
    if workspace.state["head"] != record["original"]["head"]:
        if workspace.state["head"] == record["current"] and record["cursor"] == len(record["steps"]):
            record["status"] = "completed"
            save(workspace, record)
            return record
        raise SyncError("stale_session", "workspace changed during rebase")
    sessions, history = Sessions(workspace), workspace.history
    while record["cursor"] < len(record["steps"]):
        step = record["steps"][record["cursor"]]
        action = step.get("action", "pick")
        if action == "drop":
            record["cursor"] += 1
            save(workspace, record)
            continue
        source = history.commit(step["commit"])
        if not record["session"]:
            parents = [record["current"]]
            message = source["message"]
            if action == "squash":
                if record["current"] == record["onto"]:
                    raise SyncError("invalid_squash", "squash requires a preceding picked commit")
                previous = history.commit(record["current"])
                parents, message = previous["parents"], previous["message"] + "\n\n" + message
            report = sessions.start("rebase", parent(history, step["commit"], step.get("mainline")), step["commit"], parents,
                                    ours=record["current"], deferred=True, message=message, rebase_id=identifier, cursor=record["cursor"])
            record["session"] = report["merge_id"]
            save(workspace, record)
        session = sessions.get(record["session"])
        if session["status"] == "conflict":
            return dict(rebase_id=identifier, cursor=record["cursor"], **sessions.report(session))
        result = sessions.finish(record["session"])
        record.update(current=result["commit_id"], cursor=record["cursor"] + 1, session=None)
        save(workspace, record)
    workspace.require_clean()
    workspace.install(record["current"], reason="rebase", base=record["current"])
    record["status"] = "completed"
    save(workspace, record)
    return dict(rebase_id=identifier, status="completed", commit_id=record["current"], safety_ref=record["safety_ref"])


def abort(workspace, identifier: str) -> dict:
    record = workspace.store.record("rebase", identifier)
    if not record or record["workspace_id"] != workspace.state["id"]:
        raise SyncError("rebase_not_found", identifier)
    if record["status"] == "completed":
        raise SyncError("rebase_completed", "completed rebase is recoverable through its safety ref")
    if record["session"]:
        Sessions(workspace).abort(record["session"])
    record["status"] = "aborted"
    save(workspace, record)
    return dict(rebase_id=identifier, status="aborted", original_head=record["original"]["head"])
