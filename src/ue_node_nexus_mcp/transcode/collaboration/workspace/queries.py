"""Workspace enumeration reads compact metadata; status inspects one worktree."""

from ...storage.io import confined

SESSION_TERMINAL = ("completed", "aborted")
APPLY_TERMINAL = ("completed", "rejected", "rolled_back")


def pending(store, workspace_id=None) -> tuple[list, list]:
    return (store.records("session", workspace_id=workspace_id, exclude_status=SESSION_TERMINAL, summary=True),
            store.records("apply", workspace_id=workspace_id, exclude_phase=APPLY_TERMINAL, summary=True))


def workspaces(store, schema) -> dict:
    sessions, applies = pending(store)
    by_owner = dict()
    for key, records in (("sessions", sessions), ("applies", applies)):
        for record in records:
            by_owner.setdefault(record.get("workspace_id"), dict(sessions=[], applies=[]))[key].append(record)
    rows = []
    for item in store.records("workspace", summary=True):
        root = confined(store.root / "workspaces", item["id"]) / "files"
        active = by_owner.get(item["id"], dict(sessions=[], applies=[]))
        status = dict(detail="summary", workspace_id=item["id"], session_count=len(active["sessions"]),
                      apply_count=len(active["applies"]), **active)
        rows.append(dict(item, files_root=str(root), status=None if item.get("closed") else status,
                         schema_key=schema.key if schema else "", workspace_schema_key=item.get("schema_key")))
    return dict(workspaces=rows, project_id=store.project_id, detail="summary",
                inspect_with=dict(action="status", options=dict(workspace_id="<workspace id>")))
