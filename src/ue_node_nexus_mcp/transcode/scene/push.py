"""Scene preflight, application and recoverable local commit."""

from __future__ import annotations

import logging
import uuid
from collections import Counter
from typing import Any

from ..sync_files import read_json, read_text, write_json
from ..sync_project import BridgeCall, ProjectContext, SyncError, ensure_root_registered
from . import recovery
from .backend import export_scene, selector, validate_snapshot
from .diff import build_plan
from .files import ensure_unchanged, read_base, read_document, write_snapshot
from .model import from_document
from .paths import identity, storage
from .selection import check_membership
from .reconcile import preserve_created_defaults
from .state import SceneRecord, SceneState

LOGGER = logging.getLogger(__name__)


def desired_scene(context: ProjectContext, item: SceneRecord, base: dict[str, Any] | None,
                  allow_delete: bool) -> tuple[str | None, dict[str, Any], dict[str, str]]:
    file = context.project / item.file
    text = read_text(file)
    if text is None:
        if not base or not allow_delete:
            raise SyncError("scene_missing", f"deleting a managed scene file requires allow_delete=true: {item.file}")
        return None, dict(map_path=item.map_path, name=item.name, actors=[]), base.get("aliases", dict())
    document, text, sink = read_document(context.project, file)
    if sink.has_errors:
        raise SyncError("scene_invalid", "\n".join(message.format() for message in sink.errors()))
    if document.header.schema and context.schema_key and document.header.schema != context.schema_key:
        raise SyncError("schema_stale", f"scene schema differs: {item.file}")
    desired, aliases = from_document(document, base)
    if identity(desired["map_path"], desired["name"]) != item.key:
        raise SyncError("scene_identity_changed", f"file path, map and group must agree: {item.file}")
    return text, desired, aliases


def _current(bridge: BridgeCall, context: ProjectContext, item: SceneRecord, base: dict[str, Any] | None,
             journal: dict[str, Any] | None, options: dict[str, Any]) -> tuple[dict[str, Any], dict[str, Any]]:
    adoption = bool((base or dict()).get("allow_identity_adoption"))
    request = selector(item, base, (journal or dict()).get("desired"), rebind=adoption)
    if context.bridge_available:
        raw = export_scene(bridge, context, item, request, "before")
    elif options.get("dry_run", True) and base:
        raw = base
    else:
        raise SyncError("bridge_unavailable", "scene push requires a live editor or an existing baseline for offline planning")
    if raw.get("unavailable"):
        raise SyncError("scene_unavailable", f"scene contains unloaded actors: {item.key}")
    if journal and not journal.get("after_revision"):
        late = read_json(storage(context.project, item.key, "after"))
        token = (journal.get("plan") or dict()).get("request_token")
        if late and token and late.get("request_token") == token:
            journal["after_revision"] = validate_snapshot(late, item)["revision"]
    if base and raw["revision"] != base["revision"] and options.get("force") != "local":
        if not journal or not recovery.can_resume(journal, raw):
            raise SyncError("scene_conflict", f"scene changed in UE: {item.key}")
    return raw, request


def _apply(bridge: BridgeCall, context: ProjectContext, state: SceneState, item: SceneRecord,
           text: str | None, before: dict[str, Any], desired: dict[str, Any], aliases: dict[str, str],
           plan: dict[str, Any], options: dict[str, Any], deferred: list[str]) -> dict[str, Any]:
    dry_run = bool(options.get("dry_run", True))
    plan_file = storage(context.project, item.key, "plans")
    after_file = storage(context.project, item.key, "after")
    row = dict(scene=item.key, file=item.file, kind="scene", operations=len(plan["ops"]), action="planned")
    row["verb_counts"] = dict(Counter(op["op"] for op in plan["ops"]))
    row["plan_file"] = plan_file.relative_to(context.project).as_posix()
    row["preview"] = [dict((key, value) for key, value in op.items() if key not in ("instances", "properties"))
                      for op in plan["ops"][:12]]
    ensure_unchanged(context.project / item.file, text)
    write_json(plan_file, plan)
    if dry_run and (not context.bridge_available or deferred):
        return dict(row, offline=not context.bridge_available, preflight="pending_dependencies" if deferred else "offline", dependencies=deferred)
    ensure_root_registered(bridge, context)
    ensure_unchanged(context.project / item.file, text)
    after_file.unlink(missing_ok=True)
    if not dry_run:
        recovery.record(context, item, text, before, desired, aliases, plan, dict(phase="applying"))
    LOGGER.info("scene phase=apply scene=%s operations=%d dry_run=%s", item.key, len(plan["ops"]), dry_run)
    try:
        response = bridge("scene_apply", dict(plan_file=str(plan_file), out_file=str(after_file),
                                             dry_run=dry_run, save=bool(options.get("save", True))))
    except (OSError, SyncError) as exc:
        response = dict(ok=False, error=dict(code="mcp_bridge_error", message=str(exc)))
    data = response.get("data") or dict()
    after = read_json(after_file)
    failure = None
    try:
        if not response.get("ok") or not data.get("ok"):
            detail = response.get("error") or dict()
            raise SyncError(str(detail.get("code") or "scene_incomplete"), str(detail.get("message") or data.get("error") or "scene apply did not complete"))
        if not dry_run:
            if options.get("save", True) and not data.get("saved"):
                raise SyncError("scene_save_failed", "scene changes were applied but not all packages were saved")
            snapshot = validate_snapshot(after, item)
            if snapshot.get("request_token") != plan["request_token"]:
                raise SyncError("scene_result_mismatch", "raw result belongs to a different apply attempt")
            check_membership(context.project, state, item, snapshot)
            write_snapshot(context.project, state, snapshot, aliases, item, text)
    except (OSError, ValueError, SyncError) as exc:
        failure = exc
    row.update(applied=data.get("applied", 0), saved=bool(data.get("saved")),
               saved_packages=data.get("saved_packages", []), failed_packages=data.get("failed_packages", []))
    if failure:
        row.update(action="failed", error=failure.code if isinstance(failure, SyncError) else "mirror_commit_failed",
                   message=str(failure), local_preserved=True)
        if not dry_run:
            row["recovery_file"] = recovery.record(context, item, text, before, desired, aliases, plan, response, after)
        LOGGER.warning("scene phase=incomplete scene=%s error=%s", item.key, row["error"])
        return row
    if not dry_run:
        row["action"] = "pushed" if data.get("changed") or data.get("saved_packages") else "unchanged"
        for kind in ("recovery", "after", "before"):
            storage(context.project, item.key, kind).unlink(missing_ok=True)
    if not dry_run:
        plan_file.unlink(missing_ok=True)
        row.pop("plan_file", None)
    LOGGER.info("scene phase=committed scene=%s action=%s", item.key, row["action"])
    return row


def push_one(bridge: BridgeCall, context: ProjectContext, state: SceneState, item: SceneRecord,
             options: dict[str, Any], deferred: list[str] | None = None) -> dict[str, Any]:
    if options.get("force") == "ue":
        raise SyncError("scene_pull_required", "use pull(force=ue) to adopt scene identity before pushing")
    base = read_base(context.project, item)
    journal = recovery.load(context, item)
    bindings = dict(base or dict(map_path=item.map_path, name=item.name, actors=[]))
    if journal:
        bindings["aliases"] = dict(bindings.get("aliases", dict()), **journal.get("aliases", dict()))
    text, desired, aliases = desired_scene(context, item, bindings if base or journal else None, bool(options.get("allow_delete")))
    current, request = _current(bridge, context, item, base, journal, options)
    check_membership(context.project, state, item, desired)
    desired = preserve_created_defaults(base, current, desired)
    plan = build_plan(current, desired, request, bool(options.get("allow_delete")))
    plan["adopt_identities"] = bool((base or dict()).get("allow_identity_adoption"))
    plan["request_token"] = uuid.uuid4().hex
    return _apply(bridge, context, state, item, text, current, desired, aliases, plan, options, deferred or [])


def push_scenes(bridge: BridgeCall, context: ProjectContext, state: SceneState, items: list[SceneRecord],
                options: dict[str, Any], dependencies: dict[str, set[str]], blocked: set[str], stopped: bool,
                preflight_errors: dict[str, str], deferred: set[str] | None = None) -> dict[str, Any]:
    rows, diagnostics = [], []
    for item in items:
        unavailable = sorted(dependencies.get(item.key, set()) & blocked)
        try:
            if item.key in preflight_errors:
                raise SyncError("scene_preflight_failed", preflight_errors[item.key])
            if unavailable:
                raise SyncError("dependency_failed", "blocked by: " + ", ".join(unavailable))
            if stopped:
                rows.append(dict(scene=item.key, file=item.file, kind="scene", action="skipped"))
                continue
            pending = sorted(dependencies.get(item.key, set()) & (deferred or set()))
            row = push_one(bridge, context, state, item, options, pending)
        except (OSError, ValueError, SyncError) as exc:
            row = dict(scene=item.key, file=item.file, kind="scene", action="failed", local_preserved=True,
                       error=exc.code if isinstance(exc, SyncError) else "scene_io_failed", message=str(exc))
        rows.append(row)
        if row["action"] == "failed":
            diagnostics.append(f"{item.file}: {row['error']}: {row['message']}")
            stopped = stopped or (not options.get("dry_run", True) and bool(options.get("stop_on_error", True)))
    return dict(rows=rows, diagnostics=diagnostics, error_count=len(diagnostics), stopped=stopped,
                dry_run=bool(options.get("dry_run", True)))
