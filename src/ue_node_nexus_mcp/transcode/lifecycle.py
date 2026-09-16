"""Decide editor use before acquiring the collaboration publication lock."""

from contextlib import closing, nullcontext
import json
import os
from pathlib import Path
import sqlite3

from ..instances.session import instance_manager

OFFLINE_ACTIONS = frozenset(("workspaces", "lint", "stage", "unstage", "commit", "amend", "merge", "resolve", "abort",
                             "close", "branch", "switch", "tag", "log", "show", "diff", "blame", "reflog", "stash",
                             "restore", "revert", "reset", "cherry-pick", "rebase"))


def schema_refresh_requested(options: dict) -> bool:
    query_options = set(options) - {"project", "workspace_id"}
    return bool(options.get("refresh", not query_options))


def requires_editor(action: str, options: dict, project: Path | None = None) -> bool:
    if action in OFFLINE_ACTIONS:
        return False
    if action == "schema":
        if options.get("revision"):
            return False
        return schema_refresh_requested(options) or bool(
            (options.get("function") or options.get("target")) and options.get("refresh", True))
    if action == "checkout" and options.get("revision") not in (None, "UE"):
        return False
    if action == "recover" and options.get("projection_id"):
        return False
    if action == "status" and project and (project / ".nexus/collaboration/active.json").is_file():
        return bool(options.get("discover"))
    if action != "continue":
        return True
    database = project / ".nexus/collaboration/index.sqlite" if project else None
    if not options.get("merge_id") or not database or not database.is_file():
        return False
    with closing(sqlite3.connect(database.resolve().as_uri() + "?mode=ro", uri=True)) as connection:
        row = connection.execute("SELECT payload FROM records WHERE category='session' AND id=?", (options["merge_id"],)).fetchone()
    return bool(row and json.loads(row[0]).get("operation") == "push")


def run_managed_sync(bridge, action: str, paths, options: dict | None, runner) -> dict:
    mapping = instance_manager.repository()
    options = dict(options or dict())
    options.setdefault("project", mapping["mirror_project_name"])
    root = Path(mapping["mirror_root"])
    online = requires_editor(action, options, root / mapping["mirror_project_name"])
    scope = instance_manager.work_scope("ue_sync_" + action) if online else nullcontext()
    with scope:
        return runner(bridge, action, paths, options,
                      env=dict(os.environ, UE_NEXUS_TRANSCODE_DIR=str(root)))
