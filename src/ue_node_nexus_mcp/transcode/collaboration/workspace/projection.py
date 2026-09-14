"""Recoverable multi-file projection with optimistic byte preconditions."""

from __future__ import annotations

import base64
from uuid import uuid4

from ...sync_project import SyncError
from ..store.io import atomic_write, byte_hash, confined
from ..store.refs import move_ref


def blob(store, data: bytes | None) -> str | None:
    return store.objects.put("file", dict(base64=base64.b64encode(data).decode())) if data is not None else None


def content(store, reference) -> bytes | None:
    """A journal entry names bytes it had to store, or a state that renders them."""
    if not reference:
        return None
    if isinstance(reference, dict):
        from ..semantic.snapshot import text_of

        return text_of(store.objects.data(reference["snapshot"], "snapshot")).encode("utf-8")
    return base64.b64decode(store.objects.data(reference, "file")["base64"])


def prepare(workspace, after: dict, texts: dict[str, bytes | None], reason: str, identifier: str | None = None,
            sources: dict[str, str] | None = None) -> dict:
    store, before = workspace.store, workspace.state
    journal = dict(id=identifier or uuid4().hex, workspace_id=before["id"], before=dict(before), after=after,
                   files=dict(), reason=reason, phase="prepared", completed=[])
    roots = [before["head"], before["index"], after["head"], after["index"]]
    # The journal is one durable decision, so its contents register together.
    with store.db.connection(write=True):
        for relative, desired in texts.items():
            path = confined(workspace.root, relative)
            current = path.read_bytes() if path.is_file() else None
            # A file already holding the desired bytes has nothing to write and
            # nothing to undo; journaling it would copy the untouched worktree.
            if current == desired:
                continue
            # Text that a recorded snapshot renders needs no second copy; only
            # local bytes, which exist nowhere else, are stored as blobs.
            source = (sources or dict()).get(relative)
            old, new = blob(store, current), dict(snapshot=source) if source else blob(store, desired)
            journal["files"][relative] = dict(before=old, after=new)
            roots.extend(item for item in (old, source) if item)
        journal["roots"] = roots
        journal["generation"] = store.put_record("projection", journal["id"], journal, roots, expected=0)
    return journal


def execute(workspace, journal: dict, rollback: bool = False) -> dict:
    store = workspace.store
    if journal["phase"] in ("completed", "aborted"):
        workspace.reload()
        return journal
    workspace.reload()
    published = not rollback and state_matches(workspace.state, journal["after"], journal["before"]["generation"] + 1)
    if published:
        # Metadata is the commit point. A crash between its transaction and the
        # journal update must never roll a committed projection back.
        journal["phase"] = "completed"
        store.put_record("projection", journal["id"], journal, journal["roots"], journal["generation"])
        return journal
    expected_side, target_side = ("after", "before") if rollback else ("before", "after")
    conflicts = []
    for relative, versions in journal["files"].items():
        path = confined(workspace.root, relative)
        current = path.read_bytes() if path.is_file() else None
        expected, target = content(store, versions[expected_side]), content(store, versions[target_side])
        if current == target:
            continue
        if current != expected:
            conflicts.append(dict(file=str(path), current_hash=byte_hash(current) if current is not None else None))
            continue
        if target is None:
            path.unlink(missing_ok=True)
        else:
            atomic_write(path, target)
    if conflicts:
        journal.update(phase="local_restore_conflict", conflicts=conflicts)
        journal["generation"] = store.put_record("projection", journal["id"], journal, journal["roots"], journal["generation"])
        raise SyncError("local_restore_conflict", "new file edits were preserved; projection requires reconciliation", dict(projection_id=journal["id"], conflicts=conflicts))
    if not rollback:
        before, after = journal["before"], journal["after"]
        try:
            with store.db.connection(write=True) as connection:
                if before["branch"] == after["branch"]:
                    move_ref(connection, before["branch"], after["head"], before["head"], before["id"], journal["reason"])
                else:
                    from ..store.refs import get_ref

                    if get_ref(connection, after["branch"]) != after["head"]:
                        raise SyncError("branch_moved", "switch destination changed")
                workspace.persist(after, expected=before["generation"], connection=connection)
        except SyncError:
            execute(workspace, journal, rollback=True)
            raise
    journal["phase"] = "aborted" if rollback else "completed"
    store.put_record("projection", journal["id"], journal, journal["roots"], journal["generation"])
    return journal


def state_matches(current: dict, expected: dict, generation: int) -> bool:
    return current["generation"] == generation and all(current.get(key) == value for key, value in expected.items() if key != "generation")
