"""Project publication gate from final observation through durable ref updates."""

from __future__ import annotations

from ...sync_project import SyncError, ensure_schema
from ..history import History
from ..merge.sessions import Sessions
from ..report.proposals import create
from ..store.refs import move_ref
from ..workspace.files import discover, hashes
from . import planning, transactions
from .observe import capture


def push(bridge, context, workspace, paths, options: dict, proposal=None) -> dict:
    ensure_schema(bridge, context)
    workspace.schema = context.schema
    if not options.get("save", True):
        raise SyncError("save_required", "collaboration publication saves its committed result")
    with workspace.store.lock("publication"):
        from .recover import pending

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
    dependencies = planning.references(workspace, source, assets)
    observation = capture(bridge, context, store, sorted(set(assets) | dependencies), reference=source, persist=not options.get("dry_run", True))
    if proposal:
        expected = proposal["preview"]
        if expected.get("source") != source or expected.get("target_tree") != observation["tree"] or expected.get("revisions") != observation["revisions"]:
            raise SyncError("stale_proposal", "source or observed UE inputs changed since preview")
    merged = planning.merge(workspace, source, observation["commit"], assets)
    session = None
    if options.get("merge_id"):
        session = Sessions(workspace).get(options["merge_id"])
        if session["operation"] != "push" or session["metadata"]["source"] != source:
            raise SyncError("stale_session", "push session belongs to another source")
        if session["metadata"]["revisions"] != observation["revisions"]:
            session["status"] = "stale"
            Sessions(workspace).save(session)
            raise SyncError("stale_session", "UE or dependencies changed during conflict resolution")
        Sessions(workspace).check(session)
        if session["status"] != "ready":
            return Sessions(workspace).report(session)
        merged.update(candidate=session["candidates"]["head"], conflicts=[])
    batch = planning.preflight(workspace, merged, observation, assets, options)
    if options.get("dry_run", True):
        preview = dict(source=source, target=observation["commit"], target_tree=observation["tree"], revisions=observation["revisions"],
                       push_candidate=merged["candidate"], base=merged["base"], conflict_count=len(merged["conflicts"]), conflicts=merged["conflicts"],
                       plans=[batch["units"][asset].get("summary", batch["units"][asset]) for asset in batch["order"] if asset in batch["units"]], errors=batch["errors"])
        return create(workspace, "push", paths, options, preview)
    if merged["conflicts"]:
        report = Sessions(workspace).start("push", merged["base"], observation["commit"], [observation["commit"], source],
            ours=merged["ours"], selected=assets, deferred=True, source=source, revisions=observation["revisions"],
            paths=paths, options=options, refs=dict(((original["branch"], original["head"]),)))
        if options.get("stop_on_error", True):
            return report
    if batch["errors"] and options.get("stop_on_error", True):
        return dict(action="push", status="blocked", errors=batch["errors"], error_count=len(batch["errors"]), applied=0)
    candidate = history.create(merged["candidate"], [observation["commit"], source], "Fixed submitted candidate", original["agent_id"], "candidate")
    rows, succeeded = [], set()
    for asset in batch["order"]:
        if asset in batch["errors"]:
            continue
        item = batch["units"][asset]
        if set(item["dependencies"]) & batch["errors"].keys():
            batch["errors"][asset] = dict(code="dependency_failed", message="a required asset failed")
            continue
        try:
            if item["empty"]:
                consume_unchanged(workspace, source, candidate, observation["commit"], asset)
                rows.append(dict(asset=asset, action="unchanged"))
            else:
                record = transactions.request(workspace, item, source, batch.get("candidate", candidate), observation["commit"], observation, options)
                transactions.execute(bridge, workspace, record)
                published = transactions.publish(workspace, record)
                adopt(observation, record, published, history)
                rows.append(dict(asset=asset, action="pushed", apply_id=record["id"], commit_id=published))
                if item.get("interface_changed"):
                    from .refresh import callers

                    callers(bridge, context, workspace, asset, source, candidate, merged, observation, batch, options)
            succeeded.add(asset)
        except (SyncError, OSError) as exc:
            detail = dict(code=exc.code if isinstance(exc, SyncError) else "publication_io_failed", message=str(exc), details=getattr(exc, "details", dict()))
            batch["errors"][asset] = detail
            rows.append(dict(asset=asset, action="failed", **detail))
            if options.get("stop_on_error", True):
                break
    target = store.ref("refs/ue/observed") or observation["commit"]
    complete = full_source(workspace, source, target)
    if complete and not history.is_ancestor(source, target):
        final = history.create(history.commit(target)["tree"], [target, source], "Integrate published source", original["agent_id"], "publish_merge", source_commit=source)
        with store.db.connection(write=True) as connection:
            move_ref(connection, "refs/ue/published", final, store.ref("refs/ue/published"), original["id"], "complete integration")
            move_ref(connection, "refs/ue/observed", final, target, original["id"], "complete integration")
        target = final
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
                source_integrated=complete, workspace_rebase_required=workspace_rebase, workspace_id=original["id"])


def adopt(observation: dict, record: dict, published: str, history: History) -> None:
    raw = record["receipt"]["after"]
    observation.update(commit=published, tree=history.commit(published)["tree"])
    if raw.get("exists") is False:
        observation["raw"].pop(record["asset"], None)
        observation["revisions"].pop(record["asset"], None)
    else:
        observation["raw"][record["asset"]] = raw
        observation["revisions"][record["asset"]] = dict(revision=raw["live_revision"], editor_epoch=raw["editor_epoch"], dirty=raw.get("dirty", False),
                                                       saved_hash=raw.get("saved_hash", ""), content_revision=raw.get("content_revision"))


def consume_unchanged(workspace, source: str, candidate: str, target: str, asset: str) -> None:
    store = workspace.store
    key = workspace.state["id"] + ":" + asset
    previous = store.record("integration", key)
    record = dict(workspace_id=workspace.state["id"], asset=asset, source_commit=source,
                  source_snapshot=workspace.history.entries(source).get(asset), candidate_snapshot=workspace.history.entries(candidate).get(asset), published_commit=target)
    store.put_record("integration", key, record, [source, candidate, target], previous["generation"] if previous else 0)


def full_source(workspace, source: str, target: str) -> bool:
    history = workspace.history
    bases = history.merge_bases(source, target)
    if not bases:
        return False
    before, after = history.entries(bases[0]), history.entries(source)
    required = set()
    for asset in before.keys() | after.keys():
        left, right = planning.snapshot(workspace, before.get(asset)), planning.snapshot(workspace, after.get(asset))
        if (left or dict()).get("semantic_hash") != (right or dict()).get("semantic_hash"):
            required.add(asset)
    for record in workspace.store.records("integration"):
        if record["workspace_id"] == workspace.state["id"] and record.get("source_snapshot") == after.get(record["asset"]) and history.is_ancestor(record["published_commit"], target):
            required.discard(record["asset"])
    return not required
