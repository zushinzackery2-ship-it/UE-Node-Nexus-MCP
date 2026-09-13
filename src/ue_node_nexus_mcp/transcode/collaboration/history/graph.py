"""Project trees and a verified commit DAG."""

from __future__ import annotations

import re
from datetime import datetime, timezone
from functools import lru_cache

from ...sync_project import SyncError
from ..store.io import OBJECT_ID
from ..store.repository import Store

REVISION = re.compile(r"^(.*?)([~^])([0-9]*)$")


class History:
    def __init__(self, store: Store) -> None:
        self.store = store

    def tree(self, entries: dict[str, str]) -> str:
        return self.store.objects.put("tree", entries, entries.values())

    def entries(self, revision: str | None) -> dict[str, str]:
        if not revision:
            return dict()
        value = self.store.objects.get(revision)
        if value["kind"] == "commit":
            return self.store.objects.data(value["data"]["tree"], "tree")
        if value["kind"] == "tree":
            return value["data"]
        raise SyncError("invalid_revision", "expected a commit or tree", dict(revision=revision))

    @lru_cache(maxsize=1024)
    def commit(self, identifier: str) -> dict:
        return self.store.objects.data(identifier, "commit")

    def create(self, tree: str, parents: list[str], message: str, author: str = "",
               operation: str = "commit", **metadata) -> str:
        parent_data = [self.commit(parent) for parent in parents]
        data = dict(tree=tree, parents=list(dict.fromkeys(parents)), message=message, author=author,
                    time=datetime.now(timezone.utc).isoformat(), operation=operation,
                    generation=1 + max((parent["generation"] for parent in parent_data), default=0),
                    metadata=metadata)
        identifier = self.store.objects.put("commit", data, [tree, *parents])
        before = self.entries(parents[0] if parents else None)
        after = self.entries(tree)
        changes = [(identifier, asset, after.get(asset)) for asset in sorted(before.keys() | after.keys()) if before.get(asset) != after.get(asset)]
        with self.store.db.connection(write=True) as connection:
            connection.executemany("INSERT OR IGNORE INTO changes VALUES (?, ?, ?)", changes)
        return identifier

    def resolve(self, revision: str, head: str | None = None) -> str:
        if revision == "HEAD" and head:
            return head
        match = REVISION.match(revision)
        if match:
            base, operator, count = match.groups()
            identifier = self.resolve(base, head)
            steps = int(count or 1)
            if operator == "^":
                parents = self.commit(identifier)["parents"]
                if steps == 0:
                    return identifier
                if steps > len(parents):
                    raise SyncError("invalid_revision", "parent does not exist")
                return parents[steps - 1]
            for _ in range(steps):
                parents = self.commit(identifier)["parents"]
                if not parents:
                    raise SyncError("invalid_revision", "revision precedes root commit")
                identifier = parents[0]
            return identifier
        if OBJECT_ID.fullmatch(revision):
            self.commit(revision)
            return revision
        for name in (revision, f"refs/heads/{revision}", f"refs/tags/{revision}", f"refs/ue/{revision}"):
            target = self.store.ref(name)
            if target:
                value = self.store.objects.get(target)
                return value["data"]["commit"] if value["kind"] == "tag" else target
        raise SyncError("unknown_revision", f"unknown revision {revision!r}")

    def ancestors(self, identifier: str) -> set[str]:
        seen: set[str] = set()
        pending = [identifier]
        while pending:
            current = pending.pop()
            if current not in seen:
                seen.add(current)
                pending.extend(self.commit(current)["parents"])
        return seen

    def is_ancestor(self, ancestor: str, descendant: str) -> bool:
        return ancestor in self.ancestors(descendant)

    def merge_bases(self, left: str, right: str) -> list[str]:
        common = self.ancestors(left) & self.ancestors(right)
        redundant: set[str] = set()
        for candidate in sorted(common, key=lambda item: self.commit(item)["generation"], reverse=True):
            if candidate not in redundant:
                redundant.update(self.ancestors(candidate) - set([candidate]))
        return sorted(common - redundant)

    def private(self, head: str, workspace_id: str) -> None:
        published = self.store.ref("refs/ue/published")
        if published and self.is_ancestor(head, published):
            raise SyncError("published_history", "published history is append-only; use revert or restore")
        workspaces = self.store.records("workspace")
        for workspace in workspaces:
            if workspace["id"] != workspace_id and not workspace.get("closed") and workspace["head"] == head:
                raise SyncError("shared_history", "history is also checked out by another workspace")

    def safety(self, workspace_id: str, head: str, operation: str) -> str:
        from uuid import uuid4

        name = f"refs/safety/{workspace_id}/{uuid4().hex}"
        self.store.move(name, head, None, workspace_id, operation)
        return name
