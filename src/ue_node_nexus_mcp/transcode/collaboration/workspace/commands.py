"""Local history command dispatch; all writes stay inside one workspace."""

from __future__ import annotations

from ...sync_project import SyncError
from ..history import actions, integrate, query, rebase
from ..merge.sessions import Sessions
from ..semantic.snapshot import text_of
from . import stash
from .files import capture_files, select


def revision(workspace, name: str) -> str:
    if name == "index":
        return workspace.state["index"]
    if name == "files":
        entries, _, _ = capture_files(workspace, delete=True)
        return workspace.history.tree(entries)
    return workspace.history.resolve(name, workspace.state["head"])


def run(workspace, action: str, paths, options: dict) -> dict:
    history, store, state = workspace.history, workspace.store, workspace.state
    target = options.get("revision", "HEAD")
    if action == "status":
        return workspace.status()
    if action == "stage":
        return workspace.stage(paths, options.get("delete", False))
    if action == "unstage":
        return workspace.unstage(paths)
    if action in ("commit", "amend"):
        return workspace.commit(options.get("message", ""), options.get("all", False), paths, options.get("delete", False), action == "amend")
    if action in ("branch", "tag"):
        parameters = dict(name=options.get("name"), revision=target, expected=options.get("expected"))
        parameters["delete" if action == "branch" else "message"] = options.get("delete", False) if action == "branch" else options.get("message", "")
        return getattr(actions, action)(workspace, **parameters)
    if action == "switch":
        return actions.switch(workspace, required(options, "name"))
    if action == "reset":
        return actions.reset(workspace, target, options.get("mode", "mixed"))
    if action == "restore":
        return actions.restore(workspace, target, paths, options.get("destination", "files"), options.get("entity"), options.get("field_path"))
    if action == "close":
        return actions.close(workspace)
    if action in ("merge", "pull", "revert", "cherry-pick"):
        source = options.get("source") or target
        selected = select(workspace.root, state["files"], paths) if paths else None
        source_ref = source if store.ref(source) else "refs/heads/" + source if store.ref("refs/heads/" + source) else None
        return integrate.integrate(workspace, action, source, options.get("mainline"), selected, options.get("message"), source_ref)
    if action == "resolve":
        decision = dict((key, options[key]) for key in ("choice", "value", "name") if key in options)
        required(options, "choice")
        return Sessions(workspace).resolve(required(options, "merge_id"), required(options, "conflict_id"), decision)
    if action == "abort":
        return rebase.abort(workspace, options["rebase_id"]) if options.get("rebase_id") else Sessions(workspace).abort(required(options, "merge_id"))
    if action == "continue":
        if options.get("rebase_id"):
            return rebase.resume(workspace, options["rebase_id"])
        session = Sessions(workspace).get(required(options, "merge_id"))
        if session["operation"].startswith("stash_"):
            return stash.finish(workspace, session["id"])
        if session["operation"] == "rebase":
            return rebase.resume(workspace, session["metadata"]["rebase_id"])
        return Sessions(workspace).finish(session["id"], options.get("message"))
    if action == "rebase":
        return rebase.start(workspace, required(options, "onto"), options.get("steps"))
    if action == "stash":
        mode = options.get("mode", "list")
        if mode == "list":
            return dict(stashes=store.records("stash"))
        if mode == "push":
            return stash.push(workspace, options.get("message", ""))
        if mode in ("apply", "pop"):
            return stash.apply(workspace, required(options, "stash_id"), mode == "pop")
        if mode == "drop":
            return stash.drop(workspace, required(options, "stash_id"))
        raise SyncError("invalid_option", "stash mode must be list, push, apply, pop or drop")
    if action == "log":
        return query.log(history, revision(workspace, target), options.get("limit", 50), options.get("cursor"),
                         paths[0] if paths else None, options.get("author"), options.get("entity"),
                         options.get("since"), options.get("until"))
    if action == "reflog":
        return dict(entries=store.reflog(options.get("ref"), options.get("before"), options.get("limit", 50)))
    if action == "blame":
        return query.blame(history, revision(workspace, target), options.get("asset") or (paths[0] if paths else required(options, "asset")), required(options, "field_path"))
    if action == "show":
        return show(workspace, paths, options)
    if action == "diff":
        if options.get("merge_id"):
            session = Sessions(workspace).get(options["merge_id"])
            aliases = dict(base=session["base"], ours=session["ours"], theirs=session["theirs"], candidate=session["candidates"]["head"])
            left, right = aliases[options.get("left", "ours")], aliases[options.get("right", "candidate")]
        else:
            left, right = (revision(workspace, options.get(key, default)) for key, default in (("left", "HEAD"), ("right", "files")))
        rows = query.diff(history, left, right, paths, options.get("entity"), options.get("field"))
        return paged(rows, options, left=left, right=right)
    if action == "lint":
        from .lint import lint
        return lint(workspace, paths)
    raise SyncError("invalid_action", action)


def required(options: dict, key: str):
    if key not in options:
        raise SyncError("invalid_request", f"{key} is required")
    return options[key]


def bounds(options: dict) -> tuple[int, int]:
    return max(0, int(options.get("cursor") or 0)), max(1, min(1000, int(options.get("limit", 40))))


def paged(rows: list, options: dict, **metadata) -> dict:
    start, limit = bounds(options)
    return dict(rows=rows[start:start + limit], total=len(rows), cursor=start + limit if start + limit < len(rows) else None, **metadata)


def page(items: list, options: dict, render, **metadata) -> dict:
    """Render only the requested page; whole-history text never materializes."""
    start, limit = bounds(options)
    return dict(rows=[render(item) for item in items[start:start + limit]], total=len(items),
                cursor=start + limit if start + limit < len(items) else None, **metadata)


def show(workspace, paths, options: dict) -> dict:
    store, history = workspace.store, workspace.history
    if options.get("merge_id"):
        session = Sessions(workspace).get(options["merge_id"])
        return paged(session["all_conflicts"], options, **Sessions(workspace).report(session))
    if options.get("apply_id"):
        record = store.record("apply", options["apply_id"])
        if not record:
            raise SyncError("apply_not_found", options["apply_id"])
        return record
    head = revision(workspace, options.get("revision", "HEAD"))
    commit = history.commit(head)
    items = [item for item in sorted(history.entries(head).items()) if paths is None or item[0] in paths]
    render = lambda item: dict(asset=item[0], snapshot=item[1], text=text_of(store.objects.data(item[1], "snapshot")))
    return page(items, options, render, commit_id=head, commit=commit,
                published=bool(store.ref("refs/ue/published") and history.is_ancestor(head, store.ref("refs/ue/published"))))
