"""Durable merge inputs, resolutions, three-layer replay and stale detection."""

from __future__ import annotations

from fnmatch import fnmatchcase
from uuid import uuid4

from ...sync_project import SyncError
from ..store.io import atomic_write, canonical
from ..workspace.files import capture_files, hashes
from .findings import Findings
from .resolutions import resolve_tree
from .trees import merge_trees


class Sessions:
    def __init__(self, workspace) -> None:
        self.workspace = workspace
        self.store, self.history = workspace.store, workspace.history

    def draft(self, operation: str, base: str, theirs: str, parents: list[str],
              replay_layers=False, source_ref: str | None = None, selected=None, **metadata) -> dict:
        """The session ``start`` would record, evaluated but not yet durable.

        A preview answers from this very object, so the conflicts it reports are
        the ones the session opens with, not an estimate computed another way.
        """
        workspace = self.workspace
        state = dict(workspace.state)
        if operation == "push":
            # A publication reads committed history and never replays the files
            # layer, so a half-edited local file is not its input and must not
            # be able to fail it.
            original_files, files = state["index"], state["files"]
        else:
            entries, files, _ = capture_files(workspace, delete=True)
            original_files = self.history.tree(entries)
        # The environment the merge below runs in. Recording the key the workspace
        # was checked out under made every session opened after a plugin rebuild
        # stale on arrival.
        schema_key = workspace.schema.key if workspace.schema else state.get("schema_key")
        session = dict(id=uuid4().hex, workspace_id=state["id"], operation=operation, status="preparing",
                       base=base, ours=metadata.get("ours", state["head"]), theirs=theirs, parents=parents, source_ref=source_ref,
                       original=state, original_files=original_files, original_hashes=hashes(workspace.root, files),
                       replay_layers=replay_layers, selected=selected, resolutions=dict(), metadata=metadata,
                       schema_key=schema_key, roles=metadata.get("roles") or dict(ours="workspace", theirs=operation), generation=0)
        return self.evaluate(session)

    def start(self, *args, **kwargs) -> dict:
        return self.save(self.draft(*args, **kwargs))

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

    def evaluate(self, session: dict) -> dict:
        candidates, conflicts, all_conflicts = dict(), [], []
        self._documents = dict()
        findings = Findings(self.history, self.workspace.schema)
        for layer, base, ours, theirs in self.stages(session):
            base, ours = candidates.get(base, base), candidates.get(ours, ours)
            tree, found = merge_trees(self.history, base, ours, theirs, self.workspace.schema, session["selected"], layer)
            tree = self.decide(session, tree, found, conflicts)
            all_conflicts.extend(found)
            candidates[layer] = findings.check_tree(session, tree, layer, (ours, theirs), conflicts, all_conflicts)
        session.update(candidates=candidates, conflicts=conflicts, all_conflicts=all_conflicts,
                       status="conflict" if conflicts else "ready")
        return session

    def refresh(self, session: dict) -> dict:
        return self.save(self.evaluate(session))

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

    def _line(self, conflict: dict) -> int | None:
        from ...parser import parse

        path = conflict.get("file")
        if not path:
            return None
        from pathlib import Path

        file = Path(path)
        if not file.is_file():
            return None
        # Conflicts cluster in a few files; parsing the file once per conflict
        # made a replay cost the conflict count times the file size.
        memo = getattr(self, "_documents", dict())
        if path not in memo:
            memo[path] = parse(file.read_text(encoding="utf-8"))[0]
        document = memo[path]
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
            # Its conflicts were computed under the old catalog. The inputs are
            # recorded, so running the operation again re-reads them under the
            # current one; nothing has to be redone by hand.
            raise SyncError("stale_session", "schema environment changed since this session was opened",
                            dict(merge_id=session["id"], session_schema=session["schema_key"], current_schema=self.workspace.schema.key,
                                 abort=dict(action="abort", options=dict(merge_id=session["id"], dry_run=False)),
                                 retry=dict(action=session["operation"], options=dict(dry_run=False))))
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
