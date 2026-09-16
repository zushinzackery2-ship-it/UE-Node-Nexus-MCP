"""Drain/commit shutdown with identity and generation checks at both ends."""

from __future__ import annotations

import uuid
import time

from ..errors import InstanceError, require
from ..broker.editors import proposal


def observed_blockers(item: dict, automatic: bool, save_packages=()) -> list[str]:
    reasons = []
    if set(item.get("dirty_packages", [])) - set(save_packages):
        reasons.append("instance_dirty")
    if item.get("pie"):
        reasons.append("pie_active")
    if item.get("compiling") or item.get("saving") or "editor_busy" in item.get("blockers", []):
        reasons.append("editor_busy")
    if item.get("recovery_pending"):
        reasons.append("recovery_pending")
    if item.get("pending", 0) or item.get("inflight", 0):
        reasons.append("work_in_progress")
    if automatic and item.get("launch_profile") == "interactive":
        reasons.append("interactive_editor")
    return reasons


def eligible(service, item: dict, *, automatic: bool, check_grace: bool = True) -> list[str]:
    reasons = []
    if item["state"] not in ("READY", "IDLE", "BLOCKED"):
        reasons.append("state_" + item["state"].lower())
    if item["ownership"] != "managed":
        reasons.append("external")
    if automatic and (item.get("protected") or item.get("pin_until", 0) > service.clock()):
        reasons.append("protected")
    if service.sessions.for_instance(item["instance_id"]):
        reasons.append("instance_in_use")
    if service.scopes.for_instance(item["instance_id"]):
        reasons.append("work_in_progress")
    if check_grace and (item.get("idle_since") is None or service.clock() - item["idle_since"] < service.policy.grace_seconds):
        reasons.append("idle_grace")
    if service.clock() < service.recovery_until:
        reasons.append("recovery_quarantine")
    if automatic:
        reasons.extend(observed_blockers(item, True))
    return reasons


def close(editors, client: dict | None, payload: dict) -> dict:
    service = editors.service
    automatic = bool(payload.get("automatic"))
    with service.lock:
        item = service.instance(payload["instance_id"])
        require(item.get("guard_protocol") == 1, "instance_incompatible", "lifecycle guard is required for managed exit")
        token = proposal(item, payload, dict(manager_epoch=service.epoch,
            leases=sorted(lease["lease_id"] for lease in service.sessions.for_instance(item["instance_id"])),
            scopes=sorted(scope["scope_id"] for scope in service.scopes.for_instance(item["instance_id"]))))
        require(not payload.get("proposal_id") or payload["proposal_id"] == token, "stale_plan", "close preconditions changed")
        require(payload.get("generation", item["generation"]) == item["generation"], "stale_plan", "instance generation changed")
        blockers = eligible(service, item, automatic=automatic, check_grace=False)
        if payload.get("dry_run", True):
            own_only = client and all(lease["client_session_id"] == client["client_session_id"]
                                      for lease in service.sessions.for_instance(item["instance_id"]))
            if own_only and not service.scopes.for_instance(item["instance_id"]):
                blockers = [reason for reason in blockers if reason != "instance_in_use"]
            blockers = list(dict.fromkeys(blockers + observed_blockers(item, automatic, payload.get("save_packages", []))))
            return dict(dry_run=True, instance=editors.row(item), blockers=blockers, proposal_id=token)
        if item["state"] in ("DRAINING", "STOPPING"):
            return dict(instance_id=item["instance_id"], operation_id=item["close_id"], state=item["state"])
        other_blockers = [reason for reason in blockers if reason != "instance_in_use"]
        require(not other_blockers, "instance_in_use", "editor cannot be closed", blockers=other_blockers)
        if client and not payload.get("dry_run", True):
            own = [lease for lease in service.sessions.for_instance(item["instance_id"])
                   if lease["client_session_id"] == client["client_session_id"]]
            for lease in own:
                require(not service.scopes.for_lease(lease["lease_id"]), "instance_in_use", "this session still has active work")
            other = [lease for lease in service.sessions.for_instance(item["instance_id"])
                     if lease["client_session_id"] != client["client_session_id"]]
            require(not other, "instance_in_use", "other sessions still use this editor", users=other)
            for lease in own:
                service.sessions.drop(lease, "released")
        blockers = eligible(service, item, automatic=automatic, check_grace=False)
        require(not blockers, "instance_in_use", "editor cannot be closed", blockers=blockers)
        item.update(state="DRAINING", close_id=uuid.uuid4().hex, generation=item["generation"] + 1)
        generation = item["generation"]
        service.save(item)
        editors.jobs[item["instance_id"]] = editors.workers.submit(drain, service, item, generation, payload)
        return dict(instance_id=item["instance_id"], operation_id=item["close_id"], state="DRAINING")


def drain(service, item: dict, generation: int, payload: dict) -> None:
    try:
        close_id = item["close_id"]
        prepared = await_close(service, item, "prepare_close", dict(close_id=close_id,
                               save_packages=payload.get("save_packages", []), automatic=payload.get("automatic", False)))
        cancelled = False
        with service.lock:
            if item["state"] != "DRAINING" or item["generation"] != generation:
                cancelled = True
            elif prepared.get("blockers"):
                blockers = prepared["blockers"]
                item.update(prepared.get("editor", dict()))
                item.update(state="BLOCKED", blockers=blockers, idle_since=service.clock())
                service.save(item)
                service.event("close_blocked", instance_id=item["instance_id"], blockers=blockers)
                return
            else:
                item.update(state="STOPPING", stopping_since=service.clock())
                service.save(item)
        if cancelled:
            service.control(item, "cancel_close", dict(close_id=close_id))
            return
        committed = await_close(service, item, "commit_close", dict(close_id=close_id,
                                close_revision=prepared["close_revision"], automatic=payload.get("automatic", False)))
        with service.lock:
            item.update(committed.get("editor", dict()))
            if committed.get("blockers"):
                item.update(state="BLOCKED", blockers=committed["blockers"], idle_since=service.clock())
            else:
                item["exit_committed"] = True
            service.save(item)
        service.event("close_blocked" if committed.get("blockers") else "close_requested", instance_id=item["instance_id"])
    except InstanceError as exc:
        with service.lock:
            if item["generation"] == generation:
                item.update(state="BLOCKED" if item["state"] == "DRAINING" else "UNRESPONSIVE",
                            error=exc.envelope()["error"], idle_since=service.clock())
                service.save(item)
                service.event("close_failed", instance_id=item["instance_id"], code=exc.code)


def await_close(service, item: dict, action: str, payload: dict) -> dict:
    deadline = time.monotonic() + service.policy.shutdown_seconds
    while True:
        response = service.control(item, action, payload)
        if not response.get("close_pending"):
            return response
        if time.monotonic() >= deadline:
            raise InstanceError("shutdown_timeout", "game-thread close check did not finish before the deadline")
        time.sleep(0.05)
