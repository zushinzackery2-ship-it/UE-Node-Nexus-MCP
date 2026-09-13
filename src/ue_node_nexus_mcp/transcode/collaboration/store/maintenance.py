"""Integrity checks and reachability collection with two retention windows."""

from __future__ import annotations

import json
import time

from ...sync_project import SyncError
from .repository import Store

DAY = 86400


def roots(connection, now: float, retention_days: int) -> set[str]:
    result = set(row[0] for row in connection.execute("SELECT target FROM refs"))
    for row in connection.execute("SELECT roots FROM records"):
        result.update(json.loads(row[0]))
    for row in connection.execute("SELECT old, new FROM reflog WHERE at>=?", (now - retention_days * DAY,)):
        result.update(item for item in row if item)
    return result


def reachable(store: Store, initial: set[str]) -> set[str]:
    seen: set[str] = set()
    pending = list(initial)
    while pending:
        identifier = pending.pop()
        if identifier in seen:
            continue
        value = store.objects.get(identifier)
        seen.add(identifier)
        pending.extend(link for link in value["links"] if link not in seen)
    return seen


def integrity(store: Store) -> dict:
    with store.db.connection() as connection:
        checks = [row[0] for row in connection.execute("PRAGMA integrity_check")]
        if checks != ["ok"]:
            raise SyncError("store_corrupt", "; ".join(checks))
        initial = roots(connection, time.time(), 90)
        all_objects = list(connection.execute("SELECT id FROM objects"))
    marked = reachable(store, initial)
    for row in all_objects:
        value = store.objects.get(row[0])
        for link in value["links"]:
            store.objects.get(link)
    return dict(ok=True, reachable=len(marked), objects=len(all_objects))


def collect(store: Store, dry_run: bool = True, retention_days: int = 90,
            grace_days: int = 30, now: float | None = None) -> dict:
    if retention_days < 1 or grace_days < 30:
        raise SyncError("invalid_retention", "reflog retention must be positive and unreachable grace at least 30 days")
    instant = time.time() if now is None else now
    with store.lock("gc"), store.db.connection(write=True) as connection:
        marked = reachable(store, roots(connection, instant, retention_days))
        candidates = []
        for row in connection.execute("SELECT id, created, unreachable FROM objects").fetchall():
            identifier, created, lost_at = row
            if identifier in marked:
                if not dry_run:
                    connection.execute("UPDATE objects SET unreachable=NULL WHERE id=?", (identifier,))
                continue
            since = instant if lost_at is None else max(created, lost_at)
            if since <= instant - grace_days * DAY:
                candidates.append(identifier)
            if not dry_run and lost_at is None:
                connection.execute("UPDATE objects SET unreachable=? WHERE id=?", (instant, identifier))
        if not dry_run:
            # A tombstone records intent before physical removal. On interruption
            # integrity reports the exact object instead of silently losing a ref.
            for identifier in candidates:
                store.objects.path(identifier).unlink(missing_ok=True)
                connection.execute("DELETE FROM objects WHERE id=?", (identifier,))
            connection.execute("DELETE FROM reflog WHERE at<?", (instant - retention_days * DAY,))
    return dict(dry_run=dry_run, reachable=len(marked), garbage=candidates, grace_days=grace_days)
