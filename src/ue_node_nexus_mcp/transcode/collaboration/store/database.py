"""Short WAL transactions and versioned repository metadata."""

from __future__ import annotations

import sqlite3
import threading
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
CREATE TABLE IF NOT EXISTS tombstones
    (id TEXT NOT NULL PRIMARY KEY, kind TEXT NOT NULL, removed REAL NOT NULL);
"""


class Database:
    """One connection per thread; nested blocks join the outermost transaction.

    Reconnecting per statement cost more than every query it carried: a store
    that writes one object per asset spent its time in ``connect``/``close``.
    The handle lives as long as the repository object that owns it, so a batch
    of thousands of objects pays for the WAL handshake once.
    """

    def __init__(self, path: Path) -> None:
        self.path = path
        self.local = threading.local()
        path.parent.mkdir(parents=True, exist_ok=True)
        with self.connection() as connection:
            version = connection.execute("PRAGMA user_version").fetchone()[0]
            if version > FORMAT_VERSION:
                raise SyncError("store_version", f"repository format {version} exceeds supported {FORMAT_VERSION}")
            connection.execute("PRAGMA journal_mode=WAL")
            connection.executescript("BEGIN IMMEDIATE;\n" + DDL + f"\nPRAGMA user_version={FORMAT_VERSION};\nCOMMIT;")

    def handle(self) -> sqlite3.Connection:
        connection = getattr(self.local, "connection", None)
        if connection is None:
            connection = sqlite3.connect(self.path, timeout=5, isolation_level=None)
            connection.row_factory = sqlite3.Row
            connection.execute("PRAGMA synchronous=FULL")
            connection.execute("PRAGMA foreign_keys=ON")
            self.local.connection = connection
        return connection

    def discard(self) -> None:
        connection = getattr(self.local, "connection", None)
        self.local.connection = None
        if connection is not None:
            connection.close()

    @contextmanager
    def connection(self, write: bool = False) -> Iterator[sqlite3.Connection]:
        connection = self.handle()
        owner = write and not connection.in_transaction
        try:
            if owner:
                connection.execute("BEGIN IMMEDIATE")
            yield connection
            if owner:
                connection.commit()
        except sqlite3.DatabaseError as exc:
            self.unwind(connection, owner)
            code = "store_busy" if "locked" in str(exc).lower() else "store_corrupt"
            raise SyncError(code, str(exc), dict(path=str(self.path))) from exc
        except BaseException:
            self.unwind(connection, owner)
            raise

    def unwind(self, connection: sqlite3.Connection, owner: bool) -> None:
        """Roll back from the block that opened the transaction, never inside it.

        A nested block that rolled back on its own would discard writes its
        caller still believes it owns; letting the error travel keeps the whole
        transaction as the unit that succeeds or disappears.
        """
        if not owner and connection.in_transaction:
            return
        try:
            connection.rollback()
        except sqlite3.Error:
            self.discard()
