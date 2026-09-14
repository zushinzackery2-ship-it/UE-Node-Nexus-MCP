"""Integrity checks and reachability collection with two retention windows."""

from __future__ import annotations

import json
import time

from ...sync_project import SyncError
from .repository import Store

DAY = 86400
# Terminal metadata keeps protecting objects forever unless it also expires.
# Each entry decides when a record of that category has nothing left to protect.
EXPIRING = dict(
    workspace=lambda item: bool(item.get("closed")),
    session=lambda item: item.get("status") in ("completed", "aborted", "stale"),
    apply=lambda item: item.get("phase") in ("completed", "rejected", "rolled_back"),
    projection=lambda item: item.get("phase") in ("completed", "aborted"),
    proposal=lambda item: True,
    observation=lambda item: True,
)


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


def pending_removals(connection) -> list[str]:
    return [row[0] for row in connection.execute("SELECT id FROM tombstones ORDER BY id")]


def integrity(store: Store) -> dict:
    with store.db.connection() as connection:
        checks = [row[0] for row in connection.execute("PRAGMA integrity_check")]
        if checks != ["ok"]:
            raise SyncError("store_corrupt", "; ".join(checks))
        initial = roots(connection, time.time(), 90)
        all_objects = [row[0] for row in connection.execute("SELECT id FROM objects")]
        interrupted = pending_removals(connection)
    marked = reachable(store, initial)
    for identifier in all_objects:
        value = store.objects.get(identifier)
        for link in value["links"]:
            store.objects.get(link)
    return dict(ok=True, reachable=len(marked), objects=len(all_objects), pending_removal=interrupted)


def finish(store: Store) -> list[str]:
    """Complete a committed sweep; the tombstone is the removal decision.

    Removal shares the write transaction that clears the tombstone, so a writer
    that re-created the object in the meantime cannot lose its bytes, and an
    interruption leaves the tombstone in place for the next pass.
    """
    with store.db.connection(write=True) as connection:
        interrupted = pending_removals(connection)
        for identifier in interrupted:
            store.objects.path(identifier).unlink(missing_ok=True)
        connection.executemany("DELETE FROM tombstones WHERE id=?", [(item,) for item in interrupted])
    return interrupted


def expire(connection, instant: float, retention_days: int) -> dict[str, int]:
    counts: dict[str, int] = dict()
    horizon = instant - retention_days * DAY
    for category, terminal in EXPIRING.items():
        rows = connection.execute("SELECT id, payload FROM records WHERE category=? AND updated<?", (category, horizon)).fetchall()
        stale = [(category, row[0]) for row in rows if terminal(json.loads(row[1]))]
        if stale:
            connection.executemany("DELETE FROM records WHERE category=? AND id=?", stale)
            counts[category] = len(stale)
    return counts


def collect(store: Store, dry_run: bool = True, retention_days: int = 90,
            grace_days: int = 30, now: float | None = None) -> dict:
    if retention_days < 1 or grace_days < 30:
        raise SyncError("invalid_retention", "reflog retention must be positive and unreachable grace at least 30 days")
    instant = time.time() if now is None else now
    with store.lock("gc", timeout=30):
        resumed = finish(store)
        with store.db.connection(write=True) as connection:
            expired = expire(connection, instant, retention_days) if not dry_run else dict()
            marked = reachable(store, roots(connection, instant, retention_days))
            candidates = []
            for identifier, created, lost_at in connection.execute("SELECT id, created, unreachable FROM objects").fetchall():
                if identifier in marked:
                    if not dry_run:
                        connection.execute("UPDATE objects SET unreachable=NULL WHERE id=?", (identifier,))
                    continue
                since = instant if lost_at is None else max(created, lost_at)
                if since <= instant - grace_days * DAY:
                    candidates.append(identifier)
                elif not dry_run and lost_at is None:
                    connection.execute("UPDATE objects SET unreachable=? WHERE id=?", (instant, identifier))
            if not dry_run:
                # The tombstone commits the removal before any byte disappears. An
                # interruption then reports the exact object instead of a corrupt read.
                connection.executemany("INSERT OR REPLACE INTO tombstones(id, kind, removed) VALUES (?, 'object', ?)",
                                       [(identifier, instant) for identifier in candidates])
                connection.executemany("DELETE FROM objects WHERE id=?", [(identifier,) for identifier in candidates])
                connection.execute("DELETE FROM reflog WHERE at<?", (instant - retention_days * DAY,))
        if not dry_run:
            finish(store)
    return dict(dry_run=dry_run, reachable=len(marked), garbage=candidates, grace_days=grace_days,
                resumed=resumed, expired_records=expired)
