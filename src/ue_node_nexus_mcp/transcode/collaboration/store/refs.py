"""Compare-and-swap refs with reflog in the same SQLite transaction."""

from __future__ import annotations

import re
import sqlite3
import time

from ...sync_project import SyncError

REF_NAME = re.compile(r"^refs/[A-Za-z0-9_./-]+$")


def validate_ref(name: str) -> None:
    if not REF_NAME.fullmatch(name) or ".." in name or "//" in name or name.endswith(("/", ".")):
        raise SyncError("invalid_ref", f"invalid reference: {name!r}")


def get_ref(connection: sqlite3.Connection, name: str) -> str | None:
    row = connection.execute("SELECT target FROM refs WHERE name=?", (name,)).fetchone()
    return row[0] if row else None


def move_ref(connection: sqlite3.Connection, name: str, target: str | None, expected: str | None,
             actor: str = "", reason: str = "") -> None:
    validate_ref(name)
    actual = get_ref(connection, name)
    if actual != expected:
        raise SyncError("branch_moved", f"{name} moved since it was read", dict(ref=name, expected=expected, actual=actual))
    if target == actual:
        return
    if target is None:
        connection.execute("DELETE FROM refs WHERE name=?", (name,))
    else:
        connection.execute("INSERT INTO refs(name, target) VALUES (?, ?) ON CONFLICT(name) DO UPDATE SET target=excluded.target, generation=refs.generation+1", (name, target))
    connection.execute("INSERT INTO reflog(ref, old, new, actor, reason, at) VALUES (?, ?, ?, ?, ?, ?)",
                       (name, actual, target, actor, reason, time.time()))


def reflog(connection: sqlite3.Connection, ref: str | None, before: int | None, limit: int) -> list[dict]:
    rows = connection.execute("SELECT * FROM reflog WHERE (? IS NULL OR ref=?) AND (? IS NULL OR sequence<?) ORDER BY sequence DESC LIMIT ?",
                              (ref, ref, before, before, max(1, min(limit, 1000))))
    return [dict(row) for row in rows]
