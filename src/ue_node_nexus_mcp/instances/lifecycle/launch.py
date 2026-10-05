"""One durable launch intent per project, before a process can be created."""

from __future__ import annotations

import uuid

from ..errors import InstanceError, require
from ..identity.paths import project_identity
from .policy import LIVE_STATES
from ..repository.binding import resolve as resolve_repository
from ..broker.intents import preview


def ensure(editors, client: dict, payload: dict) -> dict:
    service = editors.service
    require(service.ready, "manager_recovering", "manager is reconciling existing processes")
    project = project_identity(payload["project_path"])
    payload = dict(payload, project_path=project["project_path"])
    mode = payload.get("mode", "reuse_only")
    require(mode in ("reuse_only", "reuse_or_start"), "invalid_request", "unknown launch mode")
    allow_low_memory = payload.get("allow_low_memory", False)
    require(isinstance(allow_low_memory, bool), "invalid_request", "allow_low_memory must be a boolean",
            field="allow_low_memory", received=allow_low_memory)
    repository = resolve_repository(project, service.root, payload.get("mirror_root"))
    with service.lock:
        known = candidates(service, project["project_key"])
    if not known:
        observed = service.platform.discover(project["project_key"])
        with service.lock:
            editors.ingest(observed)
    cancel = None
    with service.lock:
        resumed = service.intents.lookup(client, payload)
        matches = candidates(service, project["project_key"])
        if resumed:
            matches = [item for item in matches if item["instance_id"] == resumed]
        if payload.get("instance_id"):
            matches = [item for item in matches if item["instance_id"] == payload["instance_id"]]
            require(bool(matches), "stale_instance", "the requested instance is not live")
        if payload.get("pid"):
            matches = [item for item in matches if item.get("pid") == payload["pid"]]
            require(bool(matches), "stale_instance", "the requested PID is not live")
        require(len(matches) <= 1, "instance_ambiguous", "select an exact instance of this project",
                instances=[editors.row(item) for item in matches])
        if matches:
            item = matches[0]
            require(item.get("control_available") is not False or item["ownership"] != "external",
                    "instance_unverified", "existing editor has no responsive lifecycle guard; inspect startup and installed plugins",
                    instance_id=item["instance_id"], guard_protocol=item.get("guard_protocol"), control_error=item.get("control_error"))
            require(item["state"] != "STOPPING", "instance_stopping", "wait for this process to exit", instance_id=item["instance_id"])
            require(item["state"] != "UNRESPONSIVE", "instance_unresponsive", "process is alive; inspect its status", instance_id=item["instance_id"])
            if item.get("ready"):
                service.platform.compatible(item, payload)
            token = preview(service, project, payload, repository, instance=item)
            require(not payload.get("proposal_id") or payload["proposal_id"] == token, "stale_plan", "ensure preconditions changed")
            if payload.get("dry_run", True):
                return dict(dry_run=True, action="reuse", instance=editors.row(item), repository=repository, proposal_id=token)
            repository = resolve_repository(project, service.root, payload.get("mirror_root"), persist=True)
            require(not item.get("cancel_pending"), "instance_draining", "another acquire is cancelling the drain")
            if item["state"] == "DRAINING":
                cancel = item
                item.update(cancel_pending=True, generation=item["generation"] + 1)
                service.save(item)
            action = "starting" if item["state"] == "STARTING" else "reused"
        else:
            require(mode == "reuse_or_start", "instance_missing", "no editor is running for this project", **project)
            options = service.platform.launch_options(project, payload)
            try:
                admission = capacity(service, project["project_key"], allow_low_memory)
            except InstanceError as exc:
                service.event("launch_refused", project_key=project["project_key"], dry_run=payload.get("dry_run", True),
                              error=exc.envelope()["error"])
                if not payload.get("dry_run", True):
                    result = editors.reap(None, dict(dry_run=False, automatic=True))
                    exc.details["reclaiming"] = [row for row in result["items"] if row.get("state") == "DRAINING"]
                raise
            token = preview(service, project, payload, repository, launch=options)
            require(not payload.get("proposal_id") or payload["proposal_id"] == token, "stale_plan", "ensure preconditions changed")
            if payload.get("dry_run", True):
                return dict(dry_run=True, action="start", project=project, launch=options, admission=admission,
                            repository=repository, proposal_id=token)
            repository = resolve_repository(project, service.root, payload.get("mirror_root"), persist=True)
            identifier = uuid.uuid4().hex
            item = dict(project, instance_id=identifier, start_intent_id=uuid.uuid4().hex, operation_id=uuid.uuid4().hex,
                        state="STARTING", ownership="managed", protected=options["launch_profile"] == "interactive",
                        generation=1, created_at=service.clock(), idle_since=None, launch=options, admission=admission,
                        ready=False)
            service.instances[identifier] = item
            service.save(item)
            service.intents.record(client, payload, item)
            service.event("launch_reserved", instance_id=identifier, project_key=project["project_key"], admission=admission)
            editors.jobs[identifier] = editors.workers.submit(spawn, editors, item)
            action = "created"
        item.update(repository)
        service.intents.record(client, payload, item)
        service.save(item)
        if not cancel:
            lease = service.sessions.acquire(client, item, mode)
            return dict(action=action, instance=editors.row(item), lease=lease)
    if cancel:
        try:
            service.control(cancel, "cancel_close", dict(close_id=cancel["close_id"]))
        except InstanceError as exc:
            with service.lock:
                cancel.update(state="UNRESPONSIVE", cancel_pending=False, error=exc.envelope()["error"])
                service.save(cancel)
            raise
        with service.lock:
            cancel.update(state="READY", cancel_pending=False)
            lease = service.sessions.acquire(client, cancel, mode)
            service.save(cancel)
            response = dict(action="reused", instance=editors.row(cancel), lease=lease)
        service.event("drain_cancelled", instance_id=cancel["instance_id"])
    return response


def candidates(service, key: str) -> list[dict]:
    return [service.instances[identifier] for identifier in service.projects.get(key, ())
            if service.instances[identifier]["state"] in LIVE_STATES]


def capacity(service, project_key: str, allow_low_memory: bool) -> dict:
    """Admit one more managed start and return the memory facts it was judged on.

    The startup slot always applies. ``allow_low_memory`` only admits a start
    whose project needs more physical memory than is available now.
    """
    active = [item for item in service.instances.values() if item["ownership"] == "managed" and item["state"] in LIVE_STATES]
    require(sum(item["state"] == "STARTING" for item in active) < service.policy.max_startups,
            "capacity_exceeded", "another editor startup holds the launch slot", limit=service.policy.max_startups)
    memory = service.platform.memory()
    floor = int(max(service.policy.min_free_gib * 1024 ** 3, memory["total"] * service.policy.min_free_ratio))
    estimate = service.resources.estimate(project_key)
    measured = dict(available_bytes=memory["available"], required_bytes=floor + estimate, floor_bytes=floor,
                    estimated_project_bytes=estimate)
    short = memory["available"] < floor + estimate
    require(not short or allow_low_memory, "capacity_exceeded",
            "insufficient available physical memory; with the user's approval, call bridge_instance_ensure again "
            "with allow_low_memory=true to start anyway", override="allow_low_memory", **measured)
    return dict(measured, memory_override=short)


def spawn(editors, item: dict) -> None:
    service = editors.service
    try:
        identity = service.platform.launch(item)
        with service.lock:
            item.update(identity)
            service.save(item)
            service.event("spawned", instance_id=item["instance_id"], pid=item["pid"])
    except Exception as exc:
        with service.lock:
            # The platform reports uncertain creation separately so recovery owns it.
            uncertain = isinstance(exc, InstanceError) and exc.code == "launch_outcome_unknown"
            if uncertain:
                item.update(exc.details)
            item.update(state="UNRESPONSIVE" if uncertain else "EXITED",
                        error=dict(code="launch_outcome_unknown" if uncertain else "launch_failed", message=str(exc)))
            service.save(item)
            service.event("launch_failed", instance_id=item["instance_id"], error=item["error"])
