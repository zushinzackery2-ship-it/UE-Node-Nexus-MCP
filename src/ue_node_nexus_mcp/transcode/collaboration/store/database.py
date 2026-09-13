"""Short WAL transactions and versioned repository metadata."""

from __future__ import annotations

import sqlite3
from contextlib import contextmanager
from pathlib import Path
from typing import Iterator

from ...sync_project import SyncError

FORMAT_VERSION = 1
DDL = """
CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS objects
    (id TEXT PRIMARY KEY, kind TEXT NOT NULL, created REAL NOT NULL, unreachable REAL);
CREATE TABLE IF NOT EXISTS refs
    (name TEXT PRIMARY KEY, target TEXT NOT NULL, generation INTEGER NOT NULL DEFAULT 1);
CREATE TABLE IF NOT EXISTS reflog
    (sequence INTEGER PRIMARY KEY AUTOINCREMENT, ref TEXT NOT NULL, old TEXT,
     new TEXT, actor TEXT NOT NULL, reason TEXT NOT NULL, at REAL NOT NULL);
CREATE INDEX IF NOT EXISTS reflog_ref ON reflog(ref, sequence);
CREATE TABLE IF NOT EXISTS records
    (category TEXT NOT NULL, id TEXT NOT NULL, payload TEXT NOT NULL, roots TEXT NOT NULL,
     generation INTEGER NOT NULL, updated REAL NOT NULL, PRIMARY KEY(category, id));
CREATE TABLE IF NOT EXISTS changes
    (commit_id TEXT NOT NULL, asset TEXT NOT NULL, snapshot TEXT, PRIMARY KEY(commit_id, asset));
CREATE INDEX IF NOT EXISTS changes_asset ON changes(asset, commit_id);
CREATE TABLE IF NOT EXISTS events
    (sequence INTEGER PRIMARY KEY AUTOINCREMENT, at REAL NOT NULL, action TEXT NOT NULL, payload TEXT NOT NULL);
"""


class Database:
    def __init__(self, path: Path) -> None:
        self.path = path
        path.parent.mkdir(parents=True, exist_ok=True)
        with self.connection() as connection:
            version = connection.execute("PRAGMA user_version").fetchone()[0]
            if version > FORMAT_VERSION:
                raise SyncError("store_version", f"repository format {version} exceeds supported {FORMAT_VERSION}")
            connection.execute("PRAGMA journal_mode=WAL")
            connection.executescript("BEGIN IMMEDIATE;\n" + DDL + f"\nPRAGMA user_version={FORMAT_VERSION};\nCOMMIT;")

    @contextmanager
    def connection(self, write: bool = False) -> Iterator[sqlite3.Connection]:
        connection = sqlite3.connect(self.path, timeout=5, isolation_level=None)
        connection.row_factory = sqlite3.Row
        connection.execute("PRAGMA synchronous=FULL")
        connection.execute("PRAGMA foreign_keys=ON")
        try:
            if write:
                connection.execute("BEGIN IMMEDIATE")
            yield connection
            if write:
                connection.commit()
        except sqlite3.DatabaseError as exc:
            connection.rollback()
            code = "store_busy" if "locked" in str(exc).lower() else "store_corrupt"
            raise SyncError(code, str(exc), dict(path=str(self.path))) from exc
        except BaseException:
            connection.rollback()
            raise
        finally:
            connection.close()
