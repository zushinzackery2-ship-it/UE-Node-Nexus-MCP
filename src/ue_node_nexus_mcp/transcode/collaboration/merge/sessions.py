"""Durable merge inputs, resolutions, three-layer replay and stale detection."""

from __future__ import annotations

from uuid import uuid4

from ...sync_project import SyncError
from ..semantic.validation import validate
from ..store.io import atomic_write, canonical, digest
from ..workspace.files import capture_files, hashes
from .resolutions import resolve_tree
from .engine import at, wire
from .trees import merge_trees


class Sessions:
    def __init__(self, workspace) -> None:
        self.workspace = workspace
        self.store, self.history = workspace.store, workspace.history

    def start(self, operation: str, base: str, theirs: str, parents: list[str],
              replay_layers=False, source_ref: str | None = None, selected=None, **metadata) -> dict:
        workspace = self.workspace
        entries, files, captured = capture_files(workspace, delete=True)
        state = dict(workspace.state)
        session = dict(id=uuid4().hex, workspace_id=state["id"], operation=operation, status="preparing",
                       base=base, ours=metadata.get("ours", state["head"]), theirs=theirs, parents=parents, source_ref=source_ref,
                       original=state, original_files=self.history.tree(entries), original_hashes=hashes(workspace.root, files),
                       replay_layers=replay_layers, selected=selected, resolutions=dict(), metadata=metadata,
                       schema_key=state.get("schema_key"), roles=metadata.get("roles") or dict(ours="workspace", theirs=operation), generation=0)
        return self.refresh(session)

    def save(self, session: dict) -> dict:
        roots = [session["base"], session["ours"], session["theirs"], session["original"]["index"], session["original_files"]]
        roots.extend(session.get("candidates", dict()).values())
        if session.get("result_commit"):
            roots.append(session["result_commit"])
        session["generation"] = self.store.put_record("session", session["id"], session, roots, session["generation"])
        directory = self.store.root / "sessions" / session["id"]
        atomic_write(directory / "session.json", canonical(session))
        atomic_write(directory / "conflicts.json", canonical(session.get("conflicts", [])))
        self.store.event(session["operation"], workspace_id=session["workspace_id"], merge_id=session["id"], status=session["status"])
        return self.report(session)

    def refresh(self, session: dict) -> dict:
        candidates, conflicts, all_conflicts = dict(), [], []
        stages = session["metadata"].get("layer_sources") or [("head", session["base"], session["ours"], session["theirs"])]
        if session["replay_layers"] and not session["metadata"].get("layer_sources"):
            stages.extend([("index", session["ours"], "head", session["original"]["index"]),
                           ("files", session["original"]["index"], "index", session["original_files"])])
        for layer, base, ours, theirs in stages:
            base, ours = candidates.get(base, base), candidates.get(ours, ours)
            tree, found = merge_trees(self.history, base, ours, theirs, self.workspace.schema, session["selected"], layer)
            for conflict in found:
                conflict["file"] = str(self.workspace.root / session["original"]["files"].get(conflict["asset"], ""))
                conflict["line"] = self._line(conflict)
                decision = session["resolutions"].get(conflict["conflict_id"])
                if decision:
                    tree = resolve_tree(self.history, tree, conflict, decision)
                else:
                    conflicts.append(conflict)
            all_conflicts.extend(found)
            for asset, snapshot_id in self.history.entries(tree).items():
                snapshot = self.store.objects.data(snapshot_id, "snapshot")
                for issue in validate(snapshot, self.workspace.schema):
                    if any(item["asset"] == asset and item["field_path"] == issue["path"] and item["layer"] == layer for item in conflicts):
                        continue
                    item = dict(conflict_id=digest(dict(layer=layer, snapshot=snapshot_id, issue=issue))[:24], asset=asset,
                        field_path=issue["path"], entity_path=issue["path"][:4], kind=snapshot["semantic"]["kind"],
                        conflict_type=issue["conflict_type"], reason=issue["reason"], layer=layer,
                        base=wire(at(snapshot["semantic"], issue["path"])), ours=wire(at(snapshot["semantic"], issue["path"])), theirs=wire(at(snapshot["semantic"], issue["path"])),
                        snapshots=[snapshot_id] * 3, allowed_resolutions=["custom", "delete", "rename"])
                    decision = session["resolutions"].get(item["conflict_id"])
                    if decision:
                        tree = resolve_tree(self.history, tree, item, decision)
                        updated_id = self.history.entries(tree).get(asset)
                        updated = self.store.objects.data(updated_id, "snapshot") if updated_id else None
                        remaining = validate(updated, self.workspace.schema) if updated else []
                        if any(issue["path"] == entry["path"] and issue["conflict_type"] == entry["conflict_type"] for entry in remaining):
                            conflicts.append(item)
                    else:
                        conflicts.append(item)
                    all_conflicts.append(item)
            candidates[layer] = tree
        session.update(candidates=candidates, conflicts=conflicts, all_conflicts=all_conflicts,
                       status="conflict" if conflicts else "ready")
        return self.save(session)

    def _line(self, conflict: dict) -> int | None:
        from ...parser import parse

        path = conflict.get("file")
        if not path:
            return None
        from pathlib import Path

        file = Path(path)
        if not file.is_file():
            return None
        document, _ = parse(file.read_text(encoding="utf-8"))
        identifier = conflict["field_path"][3] if len(conflict["field_path"]) > 3 else None
        before = self.history.entries(self.workspace.state["head"]).get(conflict["asset"])
        binding = self.store.objects.data(before, "snapshot").get("bindings", dict()).get(identifier) if before else None
        return next((decl.line for _, decl in document.iter_decls() if binding and decl.id == binding["alias"]), None)

    def get(self, identifier: str) -> dict:
        session = self.store.record("session", identifier)
        if not session or session["workspace_id"] != self.workspace.state["id"]:
            raise SyncError("merge_not_found", identifier)
        return session

    def check(self, session: dict) -> None:
        self.workspace.reload()
        current = self.workspace.state
        if current["head"] == session.get("result_commit") and current["head"] != session["original"]["head"]:
            return
        original = session["original"]
        if self.workspace.schema and session["schema_key"] != self.workspace.schema.key:
            raise SyncError("stale_session", "schema environment changed", dict(merge_id=session["id"]))
        changed = session["operation"] != "push" and (current["generation"] != original["generation"] or hashes(self.workspace.root, current["files"]) != session["original_hashes"])
        if any(self.store.ref(ref) != value for ref, value in session["metadata"].get("refs", dict()).items()):
            changed = True
        if session.get("source_ref") and self.store.ref(session["source_ref"]) != session["theirs"]:
            changed = True
        if changed:
            session["status"] = "stale"
            self.save(session)
            raise SyncError("stale_session", "session inputs changed; fixed inputs and resolutions remain available", dict(merge_id=session["id"]))

    def resolve(self, identifier: str, conflict_id: str, decision: dict) -> dict:
        session = self.get(identifier)
        if session["status"] in ("completed", "aborted"):
            raise SyncError("merge_closed", identifier)
        self.check(session)
        conflict = next((item for item in session["conflicts"] if item["conflict_id"] == conflict_id), None)
        if not conflict:
            raise SyncError("conflict_not_found", conflict_id)
        resolve_tree(self.history, session["candidates"][conflict["layer"]], conflict, decision)
        session["resolutions"][conflict_id] = decision
        return self.refresh(session)

    def finish(self, identifier: str, message: str | None = None) -> dict:
        session = self.get(identifier)
        if session["status"] == "completed":
            return self.report(session)
        projection_id = session["metadata"].get("projection_id")
        if projection_id:
            from ..workspace.projection import execute

            journal = self.store.record("projection", projection_id)
            if journal:
                execute(self.workspace, journal)
                session["status"] = "completed"
                return self.save(session)
        self.check(session)
        if session["status"] != "ready":
            raise SyncError("unresolved_conflicts", "resolve all conflicts before continuing", self.report(session))
        candidate = session["candidates"]["head"]
        if session["metadata"].get("no_commit"):
            session["metadata"]["projection_id"] = uuid4().hex
            self.save(session)
            self.workspace.install(session["original"]["head"], index=candidate,
                                   files_tree=session["candidates"].get("files", candidate), reason=session["operation"], projection_id=session["metadata"]["projection_id"])
            session["status"] = "completed"
            return self.save(session)
        result = session.get("result_commit")
        if not result:
            result = self.history.create(candidate, session["parents"], message or session["metadata"].get("message") or session["operation"],
                                         self.workspace.state["agent_id"], session["operation"], merge_id=identifier, source=session["theirs"])
            session["result_commit"] = result
            self.save(session)
        if self.workspace.state["head"] != result and not session["metadata"].get("deferred"):
            self.workspace.install(result, index=session["candidates"].get("index", candidate),
                                   files_tree=session["candidates"].get("files", candidate), reason=session["operation"], base=result)
        session["status"] = "completed"
        return self.save(session)

    def abort(self, identifier: str) -> dict:
        session = self.get(identifier)
        if session.get("result_commit") and self.workspace.state["head"] == session["result_commit"]:
            raise SyncError("merge_completed", "use revert to undo a completed merge")
        session["status"] = "aborted"
        return self.save(session)

    def report(self, session: dict) -> dict:
        return dict(merge_id=session["id"], workspace_id=session["workspace_id"], operation=session["operation"], status=session["status"],
                    base=session["base"], ours=session["ours"], theirs=session["theirs"], roles=session["roles"],
                    conflict_count=len(session.get("conflicts", [])), conflicts=session.get("conflicts", [])[:40],
                    commit_id=session.get("result_commit"), artifact=str(self.store.root / "sessions" / session["id"] / "session.json"))
