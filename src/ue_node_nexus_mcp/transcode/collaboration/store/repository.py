"""Repository facade for immutable objects and generation-checked metadata."""

from __future__ import annotations

import json
import logging
import os
import time
import uuid
from pathlib import Path
from typing import Any, Iterable

from ...sync_project import SyncError
from ...transaction.lock import MirrorLock
from .database import Database
from .io import canonical
from .objects import Objects
from .refs import get_ref, move_ref, reflog

LOG = logging.getLogger("ue_nexus.collaboration")


class Store:
    def __init__(self, root: Path, project_file: str = "") -> None:
        self.root = root.resolve()
        self.db = Database(self.root / "index.sqlite")
        self.objects = Objects(self.root / "objects", self.db)
        canonical_project = os.path.normcase(str(Path(project_file).resolve())) if project_file else ""
        with self.db.connection(write=True) as connection:
            connection.execute("INSERT OR IGNORE INTO meta VALUES ('project_id', ?)", (str(uuid.uuid4()),))
            row = connection.execute("SELECT value FROM meta WHERE key='project_file'").fetchone()
            if row and canonical_project and row[0] != canonical_project:
                raise SyncError("project_mismatch", "repository belongs to a different project", dict(expected=row[0], actual=canonical_project))
            if canonical_project:
                connection.execute("INSERT OR IGNORE INTO meta VALUES ('project_file', ?)", (canonical_project,))
            self.project_id = connection.execute("SELECT value FROM meta WHERE key='project_id'").fetchone()[0]

    def lock(self, name: str) -> MirrorLock:
        if not name.replace("-", "").replace("_", "").isalnum():
            raise SyncError("invalid_lock", "invalid lock name")
        return MirrorLock(self.root, lock_file=self.root / "locks" / f"{name}.lock")

    def snapshot(self, snapshot: dict, schema=None) -> str:
        from ...schema.binding import bind

        snapshot = bind(self, snapshot, schema)
        return self.objects.put("snapshot", snapshot, snapshot.get("schema_objects", []))

    def ref(self, name: str) -> str | None:
        with self.db.connection() as connection:
            return get_ref(connection, name)

    def move(self, name: str, target: str | None, expected: str | None, actor: str = "", reason: str = "") -> None:
        if target:
            self.objects.get(target)
        with self.db.connection(write=True) as connection:
            move_ref(connection, name, target, expected, actor, reason)
        self.event("ref", ref=name, old=expected, new=target, actor=actor, reason=reason)

    def refs(self, prefix: str = "refs/") -> dict[str, str]:
        with self.db.connection() as connection:
            return dict(connection.execute("SELECT name, target FROM refs WHERE substr(name, 1, ?)=? ORDER BY name", (len(prefix), prefix)))

    def reflog(self, ref: str | None = None, before: int | None = None, limit: int = 50) -> list[dict]:
        with self.db.connection() as connection:
            return reflog(connection, ref, before, limit)

    def record(self, category: str, identifier: str) -> dict | None:
        with self.db.connection() as connection:
            row = connection.execute("SELECT payload, generation FROM records WHERE category=? AND id=?", (category, identifier)).fetchone()
        if row is None:
            return None
        result = json.loads(row[0])
        result["generation"] = row[1]
        return result

    def records(self, category: str) -> list[dict]:
        with self.db.connection() as connection:
            rows = list(connection.execute("SELECT id, payload, generation FROM records WHERE category=? ORDER BY updated, id", (category,)))
        return [dict(json.loads(row[1]), id=row[0], generation=row[2]) for row in rows]

    def put_record(self, category: str, identifier: str, payload: dict, roots: Iterable[str] = (), expected: int | None = None, connection=None) -> int:
        targets = sorted(set(item for item in roots if item))
        for target in targets:
            self.objects.get(target)
        if connection is None:
            with self.db.connection(write=True) as transaction:
                return self.put_record(category, identifier, payload, targets, expected, transaction)
        row = connection.execute("SELECT generation FROM records WHERE category=? AND id=?", (category, identifier)).fetchone()
        actual = row[0] if row else 0
        if expected is not None and expected != actual:
            raise SyncError("stale_state", "metadata generation changed", dict(category=category, id=identifier, expected=expected, actual=actual))
        data = dict(payload)
        data.pop("generation", None)
        connection.execute("INSERT INTO records VALUES (?, ?, ?, ?, ?, ?) ON CONFLICT(category,id) DO UPDATE SET payload=excluded.payload, roots=excluded.roots, generation=excluded.generation, updated=excluded.updated",
                           (category, identifier, canonical(data).decode(), json.dumps(targets), actual + 1, time.time()))
        return actual + 1

    def drop_record(self, category: str, identifier: str, expected: int) -> None:
        with self.db.connection(write=True) as connection:
            changed = connection.execute("DELETE FROM records WHERE category=? AND id=? AND generation=?", (category, identifier, expected)).rowcount
            if not changed:
                raise SyncError("stale_state", "record changed or was already removed")

    def event(self, action: str, **payload: Any) -> None:
        payload["project_id"] = self.project_id
        LOG.info("%s %s", action, canonical(payload).decode())
        with self.db.connection(write=True) as connection:
            connection.execute("INSERT INTO events(at, action, payload) VALUES (?, ?, ?)", (time.time(), action, canonical(payload).decode()))
