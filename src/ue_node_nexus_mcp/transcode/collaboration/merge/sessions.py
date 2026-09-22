"""Durable merge inputs, resolutions, three-layer replay and stale detection."""

from __future__ import annotations

from fnmatch import fnmatchcase
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

    def stages(self, session: dict) -> list[tuple]:
        stages = list(session["metadata"].get("layer_sources") or [("head", session["base"], session["ours"], session["theirs"])])
        if session["replay_layers"] and not session["metadata"].get("layer_sources"):
            stages.extend([("index", session["ours"], "head", session["original"]["index"]),
                           ("files", session["original"]["index"], "index", session["original_files"])])
        return stages

    def refresh(self, session: dict) -> dict:
        candidates, conflicts, all_conflicts = dict(), [], []
        for layer, base, ours, theirs in self.stages(session):
            base, ours = candidates.get(base, base), candidates.get(ours, ours)
            tree, found = merge_trees(self.history, base, ours, theirs, self.workspace.schema, session["selected"], layer)
            tree = self.decide(session, tree, found, conflicts)
            all_conflicts.extend(found)
            candidates[layer] = self.check_tree(session, tree, layer, conflicts, all_conflicts)
        session.update(candidates=candidates, conflicts=conflicts, all_conflicts=all_conflicts,
                       status="conflict" if conflicts else "ready")
        return self.save(session)

    def decide(self, session: dict, tree: str, found: list[dict], conflicts: list[dict]) -> str:
        """Apply the decisions already recorded; leave the rest open in ``conflicts``."""
        for conflict in found:
            conflict["file"] = str(self.workspace.root / session["original"]["files"].get(conflict["asset"], ""))
            conflict["line"] = self._line(conflict)
            decision = session["resolutions"].get(conflict["conflict_id"])
            if decision:
                tree = resolve_tree(self.history, tree, conflict, decision)
            else:
                conflicts.append(conflict)
        return tree

    def check_tree(self, session: dict, tree: str, layer: str, conflicts: list[dict], all_conflicts: list[dict]) -> str:
        """Schema findings on the merged result, scoped exactly like the merge.

        Reporting findings for assets this session never touches gives the caller
        conflicts it cannot act on and cannot publish past.
        """
        scope = set(session["selected"]) if session["selected"] is not None else None
        for asset, snapshot_id in self.history.entries(tree).items():
            if scope is None or asset in scope:
                tree = self.check_asset(session, tree, layer, asset, snapshot_id, conflicts, all_conflicts)
        return tree

    def check_asset(self, session: dict, tree: str, layer: str, asset: str, snapshot_id: str,
                    conflicts: list[dict], all_conflicts: list[dict]) -> str:
        snapshot = self.store.objects.data(snapshot_id, "snapshot")
        for issue in validate(snapshot, self.workspace.schema):
            if any(item["asset"] == asset and item["field_path"] == issue["path"] and item["layer"] == layer for item in conflicts):
                continue
            item = self.finding(layer, asset, snapshot, snapshot_id, issue)
            decision = session["resolutions"].get(item["conflict_id"])
            if decision:
                tree = resolve_tree(self.history, tree, item, decision)
                if self.unresolved(tree, asset, issue):
                    conflicts.append(item)
            else:
                conflicts.append(item)
            all_conflicts.append(item)
        return tree

    def finding(self, layer: str, asset: str, snapshot: dict, snapshot_id: str, issue: dict) -> dict:
        value = wire(at(snapshot["semantic"], issue["path"]))
        return dict(conflict_id=digest(dict(layer=layer, snapshot=snapshot_id, issue=issue))[:24], asset=asset,
                    field_path=issue["path"], entity_path=issue["path"][:4], kind=snapshot["semantic"]["kind"],
                    conflict_type=issue["conflict_type"], reason=issue["reason"], layer=layer,
                    base=value, ours=value, theirs=value,
                    snapshots=[snapshot_id] * 3, allowed_resolutions=["custom", "delete", "rename"])

    def unresolved(self, tree: str, asset: str, issue: dict) -> bool:
        """Whether the decision left the finding it answered still standing."""
        updated_id = self.history.entries(tree).get(asset)
        updated = self.store.objects.data(updated_id, "snapshot") if updated_id else None
        remaining = validate(updated, self.workspace.schema) if updated else []
        return any(issue["path"] == entry["path"] and issue["conflict_type"] == entry["conflict_type"] for entry in remaining)

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

    def select(self, session: dict, options: dict) -> list[dict]:
        """The open conflicts one decision addresses: ids, filters, or all of them.

        History pollution produces conflicts by the hundred that all take the same
        answer. Choosing them one at a time is not a smaller version of that job,
        it is a different one: every call replays all three layers.
        """
        pool = session["conflicts"]
        identifiers = list(options.get("conflict_ids") or [])
        if options.get("conflict_id"):
            identifiers.append(options["conflict_id"])
        if identifiers:
            known = dict((item["conflict_id"], item) for item in pool)
            missing = [key for key in identifiers if key not in known]
            if missing:
                raise SyncError("conflict_not_found", ", ".join(missing), dict(conflict_ids=missing))
            return [known[key] for key in dict.fromkeys(identifiers)]
        filters = [(key, options[key]) for key in ("asset", "conflict_type", "layer") if options.get(key)]
        if not filters and not options.get("all"):
            raise SyncError("invalid_request", "name conflict_id, conflict_ids, a filter (asset/conflict_type/layer) or all=true",
                            dict(conflict_count=len(pool), conflict_types=sorted(set(item["conflict_type"] for item in pool)),
                                 layers=sorted(set(item["layer"] for item in pool))))
        matched = [item for item in pool if all(fnmatchcase(str(item.get(key, "")), value) for key, value in filters)]
        if not matched:
            raise SyncError("conflict_not_found", "no open conflict matches this selection", dict(filters=dict(filters), conflict_count=len(pool)))
        return matched

    def resolve(self, identifier: str, options: dict) -> dict:
        session = self.get(identifier)
        if session["status"] in ("completed", "aborted"):
            raise SyncError("merge_closed", identifier)
        self.check(session)
        decision = dict((key, options[key]) for key in ("choice", "value", "name") if key in options)
        if not decision.get("choice"):
            raise SyncError("invalid_request", "choice is required", dict(merge_id=identifier))
        selected = self.select(session, options)
        if len(selected) > 1 and decision["choice"] in ("custom", "rename"):
            raise SyncError("invalid_resolution", "custom and rename name one conflict at a time",
                            dict(choice=decision["choice"], matched=len(selected)))
        for conflict in selected:
            resolve_tree(self.history, session["candidates"][conflict["layer"]], conflict, decision)
            session["resolutions"][conflict["conflict_id"]] = decision
        # One replay settles the whole batch; the replay is the expensive part.
        return dict(self.refresh(session), resolved=[item["conflict_id"] for item in selected], resolved_count=len(selected))

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
