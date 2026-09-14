"""Execute local previews with isolated metadata and workspace projections."""

from __future__ import annotations

from copy import copy
from contextlib import closing
from pathlib import Path
import shutil
import sqlite3
from tempfile import TemporaryDirectory

from ..history.query import diff
from ..store.database import Database
from ..workspace import cache
from ..workspace.commands import run
from ..workspace.files import capture_files
from ..workspace.service import Workspace


def simulate(workspace, action: str, paths, options: dict) -> dict:
    store = workspace.store
    directory = store.root / "previews"
    directory.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(dir=directory) as temporary:
        sandbox = copy(store)
        sandbox.root = Path(temporary)
        database = sandbox.root / "index.sqlite"
        with store.db.connection() as source, closing(sqlite3.connect(database)) as target:
            source.backup(target)
        sandbox.db = Database(database)
        try:
            return preview(workspace, sandbox, action, paths, options)
        finally:
            # The sandbox dies with this block; an open handle would keep its
            # database file alive and turn cleanup into a permission error.
            sandbox.db.discard()


def preview(workspace, sandbox, action: str, paths, options: dict) -> dict:
    # Content-addressed objects are immutable preview artifacts. Only refs,
    # operation records and editable files need an isolated copy.
    clone = Workspace(sandbox, workspace.state["id"], workspace.schema)
    shutil.copytree(workspace.root, clone.root, dirs_exist_ok=True)
    if cache.location(workspace).is_file():
        shutil.copyfile(cache.location(workspace), cache.location(clone))
    refs = sandbox.refs()
    before_files = clone.history.tree(capture_files(clone, delete=True)[0])
    result = run(clone, action, paths, options)
    clone.reload()
    after_files = clone.history.tree(capture_files(clone, delete=True)[0])
    candidate = clone.state["index"]
    if result.get("merge_id"):
        session = sandbox.record("session", result["merge_id"])
        candidate = session["candidates"]["head"]
    elif action == "restore" and options.get("destination", "files") == "files":
        candidate = after_files
    layers = dict(head=diff(clone.history, workspace.state["head"], clone.state["head"]),
                  index=diff(clone.history, workspace.state["index"], clone.state["index"]),
                  files=diff(clone.history, before_files, after_files))
    updated = sandbox.refs()
    ref_changes = [dict(ref=name, before=refs.get(name), after=updated.get(name))
                   for name in sorted(refs.keys() | updated.keys()) if refs.get(name) != updated.get(name)]
    return dict(candidate=candidate, result=result, layers=layers, refs=ref_changes,
                objects=[candidate, clone.state["head"], clone.state["index"], after_files])
