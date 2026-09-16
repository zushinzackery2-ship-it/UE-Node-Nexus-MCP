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
                capacity(service, project["project_key"])
            except InstanceError as exc:
                if not payload.get("dry_run", True):
                    result = editors.reap(None, dict(dry_run=False, automatic=True))
                    exc.details["reclaiming"] = [row for row in result["items"] if row.get("state") == "DRAINING"]
                raise
            token = preview(service, project, payload, repository, launch=options)
            require(not payload.get("proposal_id") or payload["proposal_id"] == token, "stale_plan", "ensure preconditions changed")
            if payload.get("dry_run", True):
                return dict(dry_run=True, action="start", project=project, launch=options, repository=repository, proposal_id=token)
            repository = resolve_repository(project, service.root, payload.get("mirror_root"), persist=True)
            identifier = uuid.uuid4().hex
            item = dict(project, instance_id=identifier, start_intent_id=uuid.uuid4().hex, operation_id=uuid.uuid4().hex,
                        state="STARTING", ownership="managed", protected=options["launch_profile"] == "interactive",
                        generation=1, created_at=service.clock(), idle_since=None, launch=options, ready=False)
            service.instances[identifier] = item
            service.save(item)
            service.intents.record(client, payload, item)
            service.event("launch_reserved", instance_id=identifier, project_key=project["project_key"])
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


def capacity(service, project_key: str) -> None:
    active = [item for item in service.instances.values() if item["ownership"] == "managed" and item["state"] in LIVE_STATES]
    require(len(active) < service.policy.max_editors, "capacity_exceeded", "managed editor limit reached",
            instances=[item["instance_id"] for item in active], limit=service.policy.max_editors)
    require(sum(item["state"] == "STARTING" for item in active) < service.policy.max_startups,
            "capacity_exceeded", "another editor startup holds the launch slot")
    memory = service.platform.memory()
    threshold = max(service.policy.min_free_gib * 1024 ** 3, memory["total"] * service.policy.min_free_ratio)
    estimate = service.resources.estimate(project_key)
    require(memory["available"] >= threshold + estimate, "capacity_exceeded", "insufficient available physical memory",
            available_bytes=memory["available"], required_bytes=threshold + estimate, estimated_project_bytes=estimate)


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
