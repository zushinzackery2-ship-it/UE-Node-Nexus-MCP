"""Instance catalog and public lifecycle actions."""

from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import time

from ..errors import InstanceError, require
from ..identity.paths import project_identity
from .discovery import accept as accept_discovery

PIN_FIELDS = frozenset(("instance_id", "reason", "seconds", "dry_run", "proposal_id"))


class Editors:
    def __init__(self, service) -> None:
        self.service = service
        self.workers = ThreadPoolExecutor(max_workers=4, thread_name_prefix="nexus-lifecycle")
        self.jobs = dict()
        self.seen = dict()
        self.refreshed_at = float("-inf")

    def observe(self) -> None:
        """Readiness a reader sees must be the readiness a writer would enforce.

        Nothing refreshed state on a read, so ``status`` could answer STARTING
        for an editor that ``pin`` and every write path already treated as ready,
        and kept answering it until some unrelated call happened to sweep.
        """
        if self.service.clock() - self.refreshed_at < self.service.policy.sweep_seconds:
            return
        self.refresh()

    def refresh(self) -> None:
        service = self.service
        self.refreshed_at = service.clock()
        discovered = service.platform.discover()
        with service.lock:
            self.ingest(discovered)
            for item in service.instances.values():
                if item["state"] == "EXITED":
                    continue
                try:
                    alive = not item.get("pid") or service.platform.alive(item)
                except (OSError, InstanceError):
                    item["error"] = dict(code="instance_unverified", message="process identity could not be checked")
                    continue
                if not alive:
                    if hasattr(service.platform, "exit_result"):
                        item.update(service.platform.exit_result(item))
                    item.update(state="EXITED", exited_at=service.clock(), generation=item.get("generation", 0) + 1)
                    service.save(item)
                    service.event("exit_confirmed", instance_id=item["instance_id"], pid=item["pid"])
                elif (not item.get("pid") and item["instance_id"] not in self.jobs
                      and not getattr(service.platform, "discovery_errors", [])):
                    item.update(state="EXITED", error=dict(code="startup_interrupted", message="no process matches the durable launch intent"))
                    service.save(item)
                elif item["state"] == "STARTING" and service.clock() - item["created_at"] > service.policy.startup_seconds:
                    item.update(state="UNRESPONSIVE", error=dict(code="startup_timeout", message="process is still alive"))
                    service.save(item)
                elif item["state"] == "STOPPING" and service.clock() - item["stopping_since"] > service.policy.shutdown_seconds:
                    item.update(state="UNRESPONSIVE", exit_committed=True,
                                error=dict(code="shutdown_timeout", message="normal exit requested; OS process remains alive"))
                    service.save(item)
            for key, job in list(self.jobs.items()):
                if job.done():
                    self.jobs.pop(key)

    def ingest(self, discovered: list[dict]) -> None:
        service = self.service
        for observed in discovered:
            observed = dict((key, value) for key, value in observed.items() if key in (
                "instance_id", "pid", "process_created", "executable", "project_key", "project_path", "project_name",
                "guard_protocol", "contract_version", "engine_dir", "launch_profile", "rhi", "ready", "phase",
                "repository", "collaboration_project_id", "working_set_bytes", "private_bytes", "context_epoch",
                "pending", "inflight", "snapshot_at", "runtime_dir", "start_intent_id", "dirty_packages",
                "recovery_pending", "state_sampled_at", "compiling", "saving", "pie", "collaboration_binding", "stopping", "vfx_available",
                "private_working_set_bytes", "handle_count", "cpu_seconds", "resources_sampled_at", "control_available", "control_error",
                "blockers", "failed_packages", "close_revision", "guard_build"))
            if not accept_discovery(service, observed):
                continue
            identifier = observed["instance_id"]
            previous = service.instances.get(identifier)
            if previous and (previous["project_key"] != observed["project_key"]
                             or (previous.get("pid") and previous["process_created"] != observed["process_created"])):
                service.event("identity_conflict", instance_id=identifier)
                continue
            if previous is None:
                previous = dict(observed, ownership="external", protected=True, generation=1, state="STARTING",
                                created_at=service.clock(), idle_since=service.clock())
                service.instances[identifier] = previous
                service.save(previous)
                service.event("discovered", instance_id=identifier, pid=observed["pid"], ownership="external")
            # Transient, so it stays out of the durable record and out of the
            # equality check that decides whether this sweep has to write at all.
            self.seen[identifier] = time.time()
            before = dict(previous)
            previous.update(observed)
            service.resources.observe(previous)
            if observed.get("control_available") is False and before.get("ready") and before["state"] not in ("DRAINING", "STOPPING"):
                previous.update(state="UNRESPONSIVE", error=observed.get("control_error"))
                service.save(previous)
                continue
            if (observed.get("ready") and before.get("state") not in ("DRAINING", "STOPPING")
                    and not before.get("exit_committed") and not observed.get("stopping")):
                previous["state"] = "READY" if service.sessions.for_instance(identifier) else "IDLE"
                if before.get("state") in ("STARTING", "UNRESPONSIVE"):
                    previous["idle_since"] = service.clock()
                    previous.pop("error", None)
                    service.event("ready", instance_id=identifier)
            else:
                previous["state"] = before.get("state", "STARTING")
            if previous != before:
                service.save(previous)

    def row(self, instance: dict) -> dict:
        service = self.service
        leases = service.sessions.for_instance(instance["instance_id"])
        scopes = service.scopes.for_instance(instance["instance_id"])
        result = dict(instance)
        result.pop("manager_secret", None)
        result["users"] = [dict(lease_id=item["lease_id"], client_session_id=item["client_session_id"],
                                workspace=item["workspace"], last_use=item["last_use"], releasing=item.get("releasing", False),
                                health=service.sessions.health(item["client_session_id"])) for item in leases]
        result.update(use_count=len(leases), scope_count=len(scopes), manager_epoch=service.epoch,
                      idle_deadline=instance["idle_since"] + service.policy.grace_seconds if instance.get("idle_since") is not None else None,
                      last_use=max((item["last_use"] for item in leases), default=instance.get("idle_since")),
                      log_path=str(service.root / "Logs/lifecycle.jsonl"))
        result.update(self.freshness(instance))
        return result

    def freshness(self, instance: dict) -> dict:
        """How old the evidence behind this row is, and whether it still counts.

        A snapshot from an hour ago says nothing about a process that may have
        exited since. Reporting ``ready`` from one is how a caller learns the
        editor is gone only by trying to use it.
        """
        seen = self.seen.get(instance["instance_id"])
        if instance["state"] == "EXITED":
            return dict(snapshot_age_seconds=None, stale=False)
        if seen is None:
            return dict(snapshot_age_seconds=None, stale=True, ready=False)
        age = max(0.0, time.time() - seen)
        limit = max(self.service.policy.suspect_seconds, self.service.policy.sweep_seconds * 3)
        if age <= limit:
            return dict(snapshot_age_seconds=round(age, 3), stale=False)
        return dict(snapshot_age_seconds=round(age, 3), stale=True, ready=False,
                    error=instance.get("error") or dict(code="snapshot_stale", message="no lifecycle observation within the freshness window"))

    def list(self, _client, payload: dict) -> dict:
        self.observe()
        key = project_identity(payload["project_path"])["project_key"] if payload.get("project_path") else None
        with self.service.lock:
            items = [self.row(item) for item in self.service.instances.values()
                     if (not key or item["project_key"] == key) and (payload.get("include_exited") or item["state"] != "EXITED")]
            return dict(instances=items, policy=self.service.policy_dict(), ready=self.service.ready)

    def status(self, client, payload: dict) -> dict:
        if payload.get("instance_id"):
            self.observe()
            with self.service.lock:
                return self.row(self.service.instance(payload["instance_id"]))
        return self.list(client, payload)

    def ensure(self, client, payload: dict) -> dict:
        from ..lifecycle.launch import ensure
        return ensure(self, client, payload)

    def close(self, client, payload: dict) -> dict:
        from ..lifecycle.shutdown import close
        return close(self, client, payload)

    def reap(self, client, payload: dict) -> dict:
        from ..lifecycle.shutdown import eligible
        service = self.service
        with service.lock:
            selected = list(service.instances.values())
        results = []
        for item in selected:
            if payload.get("instance_id") and item["instance_id"] != payload["instance_id"]:
                continue
            if payload.get("project_path") and item["project_key"] != project_identity(payload["project_path"])["project_key"]:
                continue
            blockers = eligible(service, item, automatic=True, check_grace=payload.get("automatic", False))
            if blockers:
                if item["state"] != "EXITED":
                    results.append(dict(instance_id=item["instance_id"], blockers=blockers))
                continue
            try:
                results.append(self.close(client, dict(instance_id=item["instance_id"], dry_run=payload.get("dry_run", True), automatic=True)))
            except InstanceError as exc:
                results.append(dict(instance_id=item["instance_id"], error=exc.envelope()["error"]))
        return dict(items=results)

    def adopt(self, _client, payload: dict) -> dict:
        service = self.service
        with service.lock:
            item = service.instance(payload["instance_id"])
            require(payload.get("process_created") == item["process_created"], "stale_instance", "confirm the process creation identity")
            require(project_identity(payload["project_path"])["project_key"] == item["project_key"], "project_mismatch", "wrong project")
            require(bool(payload.get("reason")), "reason_required", "state why this external editor is being adopted")
            require(item.get("guard_protocol") == 1, "instance_incompatible", "install the lifecycle guard before adopting")
            require(item["state"] in ("READY", "IDLE", "BLOCKED"), "instance_unavailable", "adoption requires a ready editor")
            generation = item["generation"]
            if payload.get("dry_run", True):
                return dict(dry_run=True, instance=self.row(item), ownership="managed", proposal_id=proposal(item, payload))
            require(not payload.get("proposal_id") or payload["proposal_id"] == proposal(item, payload),
                    "stale_plan", "adoption preconditions changed")
        service.control(item, "adopt")
        with service.lock:
            require(item["generation"] == generation, "stale_plan", "instance changed during adoption")
            item.update(ownership="managed", protected=bool(payload.get("protected", True)), generation=item["generation"] + 1)
            service.save(item)
            service.event("adopted", instance_id=item["instance_id"], reason=payload["reason"])
            return self.row(item)

    def pin(self, _client, payload: dict) -> dict:
        service = self.service
        limit = service.policy.max_pin_seconds
        unknown = sorted(set(payload) - PIN_FIELDS)
        # An ignored field looks exactly like a field that did nothing, so the
        # caller retries the same mistake against the same error.
        require(not unknown, "invalid_pin", "unsupported pin field(s): " + ", ".join(unknown),
                unsupported=unknown, fields=sorted(PIN_FIELDS))
        seconds = payload.get("seconds", 0)
        require(isinstance(seconds, (float, int)) and not isinstance(seconds, bool) and 0 < seconds <= limit,
                "invalid_pin", f"seconds must be a number greater than 0 and at most {limit}",
                field="seconds", received=payload.get("seconds"), max_seconds=limit)
        require(bool(payload.get("reason")), "invalid_pin", "reason must say why this editor is being held", field="reason")
        with service.lock:
            item = service.instance(payload["instance_id"])
            # A cold start is exactly the window worth protecting: the editor is
            # not ready yet, and losing it costs the whole startup again.
            require(item["state"] in ("STARTING", "READY", "IDLE", "BLOCKED"), "instance_unavailable",
                    "pin requires a live editor", state=item["state"])
            if not payload.get("dry_run", True):
                require(not payload.get("proposal_id") or payload["proposal_id"] == proposal(item, payload),
                        "stale_plan", "pin preconditions changed")
                item["pin_until"] = service.clock() + seconds
                item["pin_expires_at"] = time.time() + seconds
                item["generation"] += 1
                service.save(item)
                service.event("pinned", instance_id=item["instance_id"], seconds=seconds, reason=payload["reason"])
            return dict(instance_id=item["instance_id"], pin_seconds=seconds, max_pin_seconds=limit,
                        state=item["state"], renewable=True, dry_run=payload.get("dry_run", True),
                        proposal_id=proposal(item, payload))


def proposal(item: dict, payload: dict, facts: dict | None = None) -> str:
    material = dict(instance_id=item["instance_id"], process_created=item.get("process_created"), generation=item["generation"],
                    state=item["state"], ownership=item["ownership"], protected=item.get("protected"),
                    context_epoch=item.get("context_epoch"), dirty_packages=item.get("dirty_packages", []),
                    facts=facts,
                    payload=dict((key, value) for key, value in payload.items() if key not in ("dry_run", "proposal_id")))
    return hashlib.sha256(json.dumps(material, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
