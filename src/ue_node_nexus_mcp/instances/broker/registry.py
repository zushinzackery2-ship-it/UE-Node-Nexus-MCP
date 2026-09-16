"""Durable lifecycle facts; transactions never span process or pipe waits."""

from __future__ import annotations

import json
from pathlib import Path
import sqlite3
import threading


class Registry:
    def __init__(self, root: Path) -> None:
        root.mkdir(parents=True, exist_ok=True)
        self.path = root / "instances.sqlite"
        self.lock = threading.RLock()
        self.connection = self.connect()
        with self.lock:
            connection = self.connection
            connection.executescript("""
                PRAGMA journal_mode=WAL;
                CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value INTEGER NOT NULL);
                CREATE TABLE IF NOT EXISTS records
                (category TEXT NOT NULL, id TEXT NOT NULL, payload TEXT NOT NULL, PRIMARY KEY(category,id));
                INSERT OR IGNORE INTO meta VALUES ('epoch',0);
            """)
            connection.execute("UPDATE meta SET value=value+1 WHERE key='epoch'")
            self.epoch = connection.execute("SELECT value FROM meta WHERE key='epoch'").fetchone()[0]
            connection.commit()

    def connect(self):
        connection = sqlite3.connect(self.path, timeout=5, check_same_thread=False)
        connection.execute("PRAGMA synchronous=FULL")
        return connection

    def get(self, category: str, identifier: str) -> dict | None:
        with self.lock:
            row = self.connection.execute("SELECT payload FROM records WHERE category=? AND id=?", (category, identifier)).fetchone()
        return json.loads(row[0]) if row else None

    def all(self, category: str) -> list[dict]:
        with self.lock:
            rows = self.connection.execute("SELECT payload FROM records WHERE category=? ORDER BY id", (category,)).fetchall()
        return [json.loads(row[0]) for row in rows]

    def put(self, category: str, identifier: str, record: dict) -> None:
        payload = json.dumps(record, ensure_ascii=False, separators=(",", ":"), sort_keys=True)
        with self.lock:
            self.connection.execute("INSERT INTO records VALUES (?,?,?) ON CONFLICT(category,id) DO UPDATE SET payload=excluded.payload",
                               (category, identifier, payload))
            self.connection.commit()

    def delete(self, category: str, identifier: str) -> None:
        self.delete_many(category, [identifier])

    def delete_many(self, category: str, identifiers) -> None:
        with self.lock:
            self.connection.executemany("DELETE FROM records WHERE category=? AND id=?", ((category, item) for item in identifiers))
            self.connection.commit()

    def close(self) -> None:
        with self.lock:
            self.connection.close()


def atomic_json(path: Path, record: dict) -> None:
    import os
    import uuid
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    try:
        with temporary.open("x", encoding="utf-8") as stream:
            json.dump(record, stream, ensure_ascii=False, sort_keys=True)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)
