"""Public collaboration workflow with separate workspace/publication locks."""

from __future__ import annotations

from ..sync_project import SyncError, ensure_schema, write_project_info
from .apply.observe import capture
from .history import History
from .merge.sessions import Sessions
from .report.options import mutates, validate
from .report.proposals import check, create
from .store.io import atomic_write, canonical
from .store.migration import enabled, migrate, survey
from .store.repository import PUBLICATION_WAIT_SECONDS, Store
from .workspace.commands import run as command
from .workspace.service import Workspace, checkout


def run(bridge, context, action: str, paths, options: dict) -> dict:
    validate(action, options)
    store = Store(context.project / ".nexus" / "collaboration", context.project_file)
    if action == "workspaces":
        rows = [dict(item, status=Workspace(store, item["id"], context.schema).status() if not item.get("closed") else None) for item in store.records("workspace")]
        return dict(workspaces=rows, project_id=store.project_id)
    if action == "checkout":
        return create_workspace(bridge, context, store, paths, options)
    identifier = options.get("workspace_id")
    if not identifier:
        raise SyncError("workspace_required", "use an independent workspace for this task", dict(workspaces=[row["id"] for row in store.records("workspace")], checkout=dict(action="checkout", options=dict(dry_run=False))))
    workspace = Workspace(store, identifier, context.schema)
    proposal = check(workspace, action, paths, options) if options.get("proposal_id") else None
    if proposal:
        options, paths = proposal["execution_options"], proposal["paths"]
    if action == "push" or (action == "continue" and options.get("merge_id") and Sessions(workspace).get(options["merge_id"])["operation"] == "push"):
        from .apply.publish import push

        return push(bridge, context, workspace, paths, options, proposal)
    if action == "recover":
        from .apply.recover import recover

        return recover(bridge, context, workspace, options)
    with store.lock("workspace-" + identifier):
        workspace.reload()
        if proposal:
            check(workspace, action, paths, options)
        ensure_idle(workspace, action)
        if action in ("fetch", "pull"):
            return fetch(bridge, context, workspace, action, paths, options, proposal)
        if mutates(action, options) and options.get("dry_run", True):
            return create(workspace, action, paths, options)
        result = command(workspace, action, paths, options)
        store.event(action, workspace_id=identifier, commit_id=workspace.state["head"])
        return dict(result, action=action, workspace_id=identifier)


def ensure_idle(workspace, action: str) -> None:
    guarded = set(("stage", "unstage", "commit", "amend", "switch", "reset", "restore", "merge", "revert", "cherry-pick", "rebase", "stash", "pull"))
    if action not in guarded:
        return
    identifier = workspace.state["id"]
    owned = lambda category: [item for item in workspace.store.records(category) if item["workspace_id"] == identifier]
    active = [item for item in owned("session") if item["status"] not in ("completed", "aborted", "stale")]
    if active:
        raise SyncError("unmerged_workspace", "resolve/continue or abort the current operation", dict(merge_ids=[item["id"] for item in active]))
    running = [item for item in owned("rebase") if item["status"] not in ("completed", "aborted")]
    if running:
        raise SyncError("unmerged_workspace", "continue or abort the running rebase", dict(rebase_ids=[item["id"] for item in running]))
    # A half-applied projection left the worktree between two versions; capturing
    # those bytes as an edit would record a state neither side ever authored.
    stuck = [item for item in owned("projection") if item["phase"] not in ("completed", "aborted")]
    if stuck:
        raise SyncError("projection_pending", "recover the interrupted file update first",
                        dict(projection_ids=[item["id"] for item in stuck], recover=dict(action="recover", options=dict(projection_id=stuck[0]["id"], dry_run=False))))


def create_workspace(bridge, context, store, paths, options: dict) -> dict:
    dry_run = options.get("dry_run", True)
    if context.bridge_available:
        ensure_schema(bridge, context)
    if dry_run and not enabled(context):
        return dict(action="checkout", dry_run=True, repository=str(store.root), revision=options.get("revision", "UE"),
                    paths=paths, migration=survey(context))
    with store.lock("initialize"):
        if not enabled(context):
            migrate(store, context)
        revision = options.get("revision")
        if not revision or revision == "UE":
            with store.lock("publication", PUBLICATION_WAIT_SECONDS):
                selectors = dict()
                scene = options.get("scene")
                if scene:
                    key = scene["map_path"] + "#" + scene["name"]
                    selectors[key] = scene
                selected = list(selectors) if selectors else checkout_paths(bridge, context, paths)
                observed = capture(bridge, context, store, selected, discover=paths is None and not selectors,
                                   selectors=selectors, persist=not dry_run)
            revision = observed["commit"]
        if dry_run:
            head = History(store).resolve(revision)
            return dict(action="checkout", dry_run=True, commit_id=head, assets=list(History(store).entries(head)))
        from .workspace.files import filename, select

        entries = History(store).entries(revision)
        files = dict((asset, filename(asset, store.objects.data(value, "snapshot"))) for asset, value in entries.items())
        assets = select(context.project, files, paths) if paths else None
        workspace = checkout(store, revision, context.schema, options.get("branch"), options.get("agent_id", ""), assets)
        atomic_write(store.root / "active.json", canonical(dict(format=1, project_id=store.project_id)))
        write_project_info(context, dict(collaboration_format=1, project_id=store.project_id))
        return dict(action="checkout", **workspace.info())


def checkout_paths(bridge, context, paths):
    if paths is None:
        return None
    from ..sync_status import query_ue
    from .workspace.files import select
    from ..paths import text_path

    infos, known = query_ue(bridge, context, [], discover=True, include_stubs=True)
    if not known:
        raise SyncError("observation_failed", "cannot resolve checkout selection")
    files = dict((asset, text_path(context.project, asset, info.kind).relative_to(context.project).as_posix()) for asset, info in infos.items())
    return select(context.project, files, paths)


def fetch(bridge, context, workspace, action: str, paths, options: dict, proposal) -> dict:
    from .workspace.files import select

    assets = select(workspace.root, workspace.state["files"], paths) if paths else None
    with workspace.store.lock("publication", PUBLICATION_WAIT_SECONDS, workspace_id=workspace.state["id"]):
        observation = capture(bridge, context, workspace.store, assets, reference=workspace.state["head"],
                              discover=options.get("discover", False), persist=not options.get("dry_run", True))
        if proposal and proposal["preview"].get("target_tree") != observation["tree"]:
            raise SyncError("stale_proposal", "UE state changed since preview")
    if options.get("dry_run", True):
        preview = dict(target=observation["commit"], target_tree=observation["tree"], revisions=observation["revisions"])
        return create(workspace, action, paths, options, preview)
    if action == "fetch":
        return dict(action=action, observation_id=observation["id"], commit_id=observation["commit"], revisions=observation["revisions"])
    return command(workspace, "pull", paths, dict(options, revision=observation["commit"]))
