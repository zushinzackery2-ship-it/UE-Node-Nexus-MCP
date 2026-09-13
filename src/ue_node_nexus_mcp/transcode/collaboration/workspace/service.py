"""Workspace state and atomic local index/commit operations."""

from __future__ import annotations

from uuid import uuid4

from ...sync_project import SyncError
from ..history import History
from ..semantic.snapshot import text_of
from ..semantic.validation import validate
from ..store.io import confined
from ..store.refs import move_ref
from .files import capture_files, discover, filename, hashes, select


class Workspace:
    def __init__(self, store, identifier: str, schema=None) -> None:
        self.store, self.history, self.schema = store, History(store), schema
        state = store.record("workspace", identifier)
        if state is None:
            raise SyncError("workspace_not_found", identifier)
        if state.get("closed"):
            raise SyncError("workspace_closed", identifier)
        self.state = state
        self.root = confined(store.root / "workspaces", identifier) / "files"

    def reload(self) -> None:
        self.state = self.store.record("workspace", self.state["id"])

    def persist(self, state: dict, expected: int | None = None, connection=None) -> None:
        roots = [state["head"], state["index"], state["base"]]
        generation = self.store.put_record("workspace", state["id"], state, roots,
                                          self.state["generation"] if expected is None else expected, connection)
        self.state = dict(state, generation=generation)

    def snapshot(self, snapshot: dict) -> str:
        return self.store.snapshot(snapshot, self.schema)

    def info(self) -> dict:
        return dict(self.state, files_root=str(self.root), file_paths=dict((asset, str(confined(self.root, relative))) for asset, relative in self.state["files"].items()),
                    schema_path=str(self.schema.directory / "index.md") if self.schema else None)

    def status(self) -> dict:
        state = self.state
        head, index = self.history.entries(state["head"]), self.history.entries(state["index"])
        files = discover(self.root, state["files"])
        deleted = [asset for asset, path in files.items() if not confined(self.root, path).is_file()]
        try:
            working, _, _ = capture_files(self, delete=True)
            unstaged = [asset for asset in working.keys() | index.keys() if working.get(asset) != index.get(asset)]
            errors = []
        except (SyncError, UnicodeError) as exc:
            unstaged, errors = sorted(files), [str(exc)]
        staged = [asset for asset in head.keys() | index.keys() if head.get(asset) != index.get(asset)]
        branch_head = self.store.ref(state["branch"])
        observations = self.store.ref("refs/ue/observed")
        sessions = [item for item in self.store.records("session") if item.get("workspace_id") == state["id"] and item["status"] not in ("completed", "aborted")]
        applies = [item for item in self.store.records("apply") if item.get("workspace_id") == state["id"] and item["phase"] not in ("completed", "rejected", "rolled_back")]
        ahead = len(self.history.ancestors(state["head"]) - self.history.ancestors(observations)) if observations else 0
        behind = len(self.history.ancestors(observations) - self.history.ancestors(state["head"])) if observations else 0
        return dict(workspace_id=state["id"], head=state["head"], index=state["index"], branch=state["branch"], generation=state["generation"],
                    staged=sorted(staged), unstaged=sorted(unstaged), local_deleted=deleted, errors=errors,
                    dirty=bool(staged or unstaged), branch_moved=branch_head != state["head"], ahead=ahead, behind=behind,
                    sessions=sessions, applies=applies, schema_key=state.get("schema_key"), files_root=str(self.root),
                    schema_path=str(self.schema.directory / "index.md") if self.schema else None)

    def require_clean(self) -> None:
        status = self.status()
        if status["dirty"]:
            raise SyncError("dirty_workspace", "commit or stash the affected local layers first", status)
        if status["branch_moved"]:
            raise SyncError("branch_moved", "branch advanced in another workspace", status)

    def stage(self, paths=None, delete=False) -> dict:
        entries, files, _ = capture_files(self, paths, delete)
        for asset in select(self.root, files, paths):
            if asset in entries:
                findings = validate(self.store.objects.data(entries[asset], "snapshot"), self.schema)
                if findings:
                    raise SyncError("candidate_invalid", "staged semantic state is invalid", dict(asset=asset, diagnostics=findings))
        self.persist(dict(self.state, index=self.history.tree(entries), files=files))
        self.store.event("stage", workspace_id=self.state["id"], index=self.state["index"])
        return self.status()

    def unstage(self, paths=None) -> dict:
        entries, head = self.history.entries(self.state["index"]), self.history.entries(self.state["head"])
        files = discover(self.root, self.state["files"])
        for asset in select(self.root, files, paths):
            if asset in head:
                entries[asset] = head[asset]
            else:
                entries.pop(asset, None)
        self.persist(dict(self.state, index=self.history.tree(entries)))
        return self.status()

    def commit(self, message: str, all_files=False, paths=None, delete=False, amend=False) -> dict:
        if not message.strip() and not amend:
            raise SyncError("message_required", "commit message is required")
        before = dict(self.state)
        index, files = before["index"], before["files"]
        if all_files:
            entries, files, _ = capture_files(self, paths, delete)
            for asset, identifier in entries.items():
                findings = validate(self.store.objects.data(identifier, "snapshot"), self.schema)
                if findings:
                    raise SyncError("candidate_invalid", asset, dict(diagnostics=findings))
            index = self.history.tree(entries)
        previous = self.history.commit(before["head"])
        if index == previous["tree"] and not amend:
            return dict(action="unchanged", commit_id=before["head"])
        parents = [before["head"]]
        if amend:
            self.history.private(before["head"], before["id"])
            self.history.safety(before["id"], before["head"], "amend")
            parents = previous["parents"]
        identifier = self.history.create(index, parents, message or previous["message"], before["agent_id"], "amend" if amend else "commit", schema_key=before.get("schema_key"))
        after = dict(before, head=identifier, index=index, files=files)
        with self.store.db.connection(write=True) as connection:
            move_ref(connection, before["branch"], identifier, before["head"], before["id"], "commit")
            self.persist(after, expected=before["generation"], connection=connection)
        self.store.event("commit", workspace_id=before["id"], commit_id=identifier)
        return dict(action="committed", commit_id=identifier, parents=parents, workspace_id=before["id"])

    def install(self, head: str, index: str | None = None, files_tree: str | None = None,
                reason="checkout", branch: str | None = None, base: str | None = None, projection_id: str | None = None) -> dict:
        from .projection import execute, prepare

        pending = self.store.record("projection", projection_id) if projection_id else None
        if pending:
            execute(self, pending)
            return self.info()
        index = index or self.history.commit(head)["tree"]
        entries = self.history.entries(files_tree or index)
        included = set(self.state["files"]) if self.state.get("sparse") else set(entries)
        selected = dict((asset, identifier) for asset, identifier in entries.items() if asset in included)
        files, texts = dict(), dict()
        for asset, identifier in selected.items():
            snapshot = self.store.objects.data(identifier, "snapshot")
            relative = self.state["files"].get(asset, filename(asset, snapshot))
            if relative in texts:
                raise SyncError("duplicate_asset", "two semantic assets map to the same workspace file", dict(file=relative))
            files[asset] = relative
            texts[relative] = text_of(snapshot).encode("utf-8")
        for asset, relative in self.state["files"].items():
            if relative not in texts:
                texts[relative] = None
        after = dict(self.state, head=head, index=index, files=files, base=base or self.state["base"], branch=branch or self.state["branch"])
        execute(self, prepare(self, after, texts, reason, projection_id))
        return self.info()


def checkout(store, revision: str, schema=None, branch: str | None = None, agent_id="",
             assets: list[str] | None = None, identifier: str | None = None) -> Workspace:
    history = History(store)
    head = history.resolve(revision)
    workspace_id = identifier or uuid4().hex
    branch = branch or f"refs/heads/agents/{workspace_id}"
    if not branch.startswith("refs/heads/"):
        branch = "refs/heads/" + branch
    current = store.ref(branch)
    if current and current != head:
        raise SyncError("branch_exists", "branch points to a different version")
    entries = history.entries(head)
    files = dict((asset, filename(asset, store.objects.data(snapshot, "snapshot"))) for asset, snapshot in entries.items() if assets is None or asset in assets)
    state = dict(id=workspace_id, agent_id=agent_id, head=head, index=history.commit(head)["tree"], base=head,
                 branch=branch, files=files, sparse=assets is not None, schema_key=schema.key if schema else "", closed=False)
    with store.db.connection(write=True) as connection:
        move_ref(connection, branch, head, current, agent_id, "checkout")
        store.put_record("workspace", workspace_id, state, [head, state["index"]], expected=0, connection=connection)
    workspace = Workspace(store, workspace_id, schema)
    workspace.install(head)
    store.event("checkout", workspace_id=workspace_id, branch=branch, commit_id=head)
    return workspace
