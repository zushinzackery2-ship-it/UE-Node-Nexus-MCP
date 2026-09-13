"""Verified immutable object storage; references are explicit GC edges."""

from __future__ import annotations

import time
from pathlib import Path
from typing import Any, Iterable

from ...sync_project import SyncError
from .database import Database
from .io import OBJECT_ID, atomic_write, byte_hash, canonical, read_json


class Objects:
    def __init__(self, root: Path, database: Database) -> None:
        self.root = root
        self.database = database

    def path(self, object_id: str) -> Path:
        if not isinstance(object_id, str) or not OBJECT_ID.fullmatch(object_id):
            raise SyncError("invalid_object", f"invalid object ID: {object_id!r}")
        return self.root / object_id[:2] / f"{object_id}.json"

    def put(self, kind: str, data: Any, links: Iterable[str] = ()) -> str:
        targets = sorted(set(item for item in links if item))
        for target in targets:
            self.get(target)
        payload = canonical(dict(format=1, kind=kind, data=data, links=targets))
        object_id = byte_hash(payload)
        path = self.path(object_id)
        if path.exists():
            self.get(object_id, kind)
        else:
            atomic_write(path, payload)
        with self.database.connection(write=True) as connection:
            connection.execute("INSERT OR IGNORE INTO objects(id, kind, created) VALUES (?, ?, ?)", (object_id, kind, time.time()))
            connection.execute("UPDATE objects SET unreachable=NULL WHERE id=?", (object_id,))
        return object_id

    def get(self, object_id: str, kind: str | None = None) -> dict:
        path = self.path(object_id)
        value = read_json(path)
        if byte_hash(canonical(value)) != object_id or value.get("format") != 1:
            raise SyncError("store_corrupt", f"object checksum/format mismatch: {object_id}", dict(path=str(path)))
        if kind and value.get("kind") != kind:
            raise SyncError("store_corrupt", f"expected {kind}, found {value.get('kind')}", dict(object_id=object_id))
        return value

    def data(self, object_id: str, kind: str | None = None) -> Any:
        return self.get(object_id, kind)["data"]
