"""Project publication gate from final observation through durable ref updates."""

from __future__ import annotations

from ...errors import SyncError
from ...sync.schema import ensure_schema
from ..merge.sessions import Sessions
from ..report.proposals import create
from ..report.publication import describe
from ..store.refs import move_ref
from ..store.repository import PUBLICATION_WAIT_SECONDS
from ..workspace.files import discover, hashes
from . import planning
from .observe import remember
from .prepare import prepare
from .units import run_units


def push(bridge, context, workspace, paths, options: dict, proposal=None) -> dict:
    ensure_schema(bridge, context)
    workspace.schema = context.schema
    if not options.get("save", True):
        raise SyncError("save_required", "collaboration publication saves its committed result")
    with workspace.store.lock("publication", PUBLICATION_WAIT_SECONDS, workspace_id=workspace.state["id"]):
        from .recover import outstanding, pending, pending_records

        unfinished = pending_records(workspace)
        if proposal and (unfinished or proposal["preview"].get("published") != workspace.store.ref("refs/ue/published")):
            raise SyncError("stale_proposal", "publication or pending executions changed since preview")
        if options.get("dry_run", True):
            if unfinished:
                # Naming the pending rows is not enough: every supported way out
                # has to be in the error, or the caller has no move to make.
                raise SyncError("recovery_required", "recover pending executions before previewing publication",
                                outstanding(unfinished))
        else:
            pending(bridge, context, workspace)
        return publish_locked(bridge, context, workspace, paths, options, proposal)


def publish_locked(bridge, context, workspace, paths, options: dict, proposal) -> dict:
    history, store = workspace.history, workspace.store
    source_name = options.get("source") or options.get("revision", "HEAD")
    source = history.resolve(source_name, workspace.state["head"])
    original = dict(workspace.state)
    original_hashes = hashes(workspace.root, discover(workspace.root, original["files"]))
    original_dirty = workspace.status()["dirty"]
    assets = planning.selected_assets(workspace, source, paths)
    if options.get("merge_id"):
        ancestry = Sessions(workspace).get(options["merge_id"])
        if ancestry["metadata"].get("base_pair"):
            from ..merge.ancestors import adopt_base

            adopt_base(workspace, ancestry)
            options = dict(options)
            options.pop("merge_id")
    prepared = prepare(bridge, context, workspace, source, assets, paths, options, proposal, original)
    if prepared.answer is not None:
        return prepared.answer
    observation, merged, batch, session = prepared.observation, prepared.merged, prepared.batch, prepared.session
    explanation = describe(workspace, merged, assets, paths, options)
    if options.get("dry_run", True):
        preview = dict(source=source, target=observation["commit"], target_tree=observation["tree"], revisions=observation["revisions"], published=store.ref("refs/ue/published"),
                       push_candidate=merged["candidate"], base=merged["base"], conflict_count=len(merged["conflicts"]), conflicts=merged["conflicts"],
                       plans=[batch["units"][asset].get("summary", batch["units"][asset]) for asset in batch["order"] if asset in batch["units"]], errors=batch["errors"], **explanation)
        if merged["conflicts"]:
            # A preview must not open a durable session, so it cannot hand out a
            # merge_id; say which call does, instead of returning merge_id=None.
            preview["merge_id"] = None
            preview["resolve_with"] = dict(action="push", paths=paths, options=dict(options, dry_run=False),
                                           note="a real push opens the merge session whose merge_id resolve/continue take")
        return create(workspace, "push", paths, options, preview)
    conflict_report = None
    if merged["conflicts"]:
        conflict_report = Sessions(workspace).save(prepared.draft)
        conflict_report.update(explanation)
        if options.get("stop_on_error", True):
            return conflict_report
    if batch["errors"] and options.get("stop_on_error", True):
        return dict(action="push", status="blocked", errors=batch["errors"], error_count=len(batch["errors"]), applied=0, **explanation)
    candidate = history.create(merged["candidate"], [observation["commit"], source], "Fixed submitted candidate", original["agent_id"], "candidate")
    rows = run_units(bridge, context, workspace, source, candidate, merged, observation, batch, options)
    target = store.ref("refs/ue/observed") or observation["commit"]
    complete = full_source(workspace, source, target)
    if complete and not history.is_ancestor(source, target):
        final = history.create(history.commit(target)["tree"], [target, source], "Integrate published source", original["agent_id"], "publish_merge", source_commit=source)
        with store.db.connection(write=True) as connection:
            move_ref(connection, "refs/ue/published", final, store.ref("refs/ue/published"), original["id"], "complete integration")
            move_ref(connection, "refs/ue/observed", final, target, original["id"], "complete integration")
        target = final
    if store.ref("refs/ue/observed") == target:
        # Each receipt states the memory the editor now holds, so publication
        # leaves the project fully measured instead of forcing a re-export. Only
        # revisions still current are asserted: one read before a later apply
        # compiled may describe memory that is gone. Whatever a failed unit
        # guarded is left out too: its last measurement predates an attempt that
        # may or may not have unwound, or is the very revision UE just refused.
        refused = set(batch["errors"])
        for asset in batch["errors"]:
            refused.update(batch["units"].get(asset, dict()).get("dependencies", ()))
        measured = dict((asset, value) for asset, value in observation["revisions"].items()
                        if asset in observation["current"] and asset not in refused)
        remember(store, context, target, observation["editor_epoch"], history.entries(target), measured)
    if session and not batch["errors"]:
        session.update(status="completed", result_commit=target)
        Sessions(workspace).save(session)
    workspace_rebase = True
    if complete and not original_dirty and not batch["errors"]:
        try:
            with store.lock("workspace-" + original["id"]):
                workspace.reload()
                files = discover(workspace.root, workspace.state["files"])
                if workspace.state["generation"] == original["generation"] and hashes(workspace.root, files) == original_hashes:
                    workspace.install(target, reason="push", base=target)
                    workspace_rebase = False
        except (SyncError, OSError):
            workspace_rebase = True
    return dict(action="push", source_commit=source, published_commit=target, status="partial" if batch["errors"] else "published",
                error_count=len(batch["errors"]), errors=batch["errors"], rows=rows, applied=len([row for row in rows if row["action"] == "pushed"]),
                source_integrated=complete, workspace_rebase_required=workspace_rebase, workspace_id=original["id"],
                merge_id=conflict_report["merge_id"] if conflict_report else None,
                conflicts=conflict_report["conflicts"] if conflict_report else [], **explanation)


def full_source(workspace, source: str, target: str) -> bool:
    history = workspace.history
    bases = history.merge_bases(source, target)
    if not bases:
        return False
    before, after = history.entries(bases[0]), history.entries(source)
    required = set()
    for asset in before.keys() | after.keys():
        # The same object is the same semantic state; only a differing pair is
        # worth loading, which keeps this out of the per-asset cost of a push.
        if before.get(asset) == after.get(asset):
            continue
        left, right = planning.snapshot(workspace, before.get(asset)), planning.snapshot(workspace, after.get(asset))
        if (left or dict()).get("semantic_hash") != (right or dict()).get("semantic_hash"):
            required.add(asset)
    for record in workspace.store.records("integration"):
        if record["workspace_id"] == workspace.state["id"] and record.get("source_snapshot") == after.get(record["asset"]) and history.is_ancestor(record["published_commit"], target):
            required.discard(record["asset"])
    return not required
