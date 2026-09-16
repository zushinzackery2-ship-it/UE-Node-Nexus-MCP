"""Fair, bounded project admission; UE keeps the authoritative work counters."""

from __future__ import annotations

import uuid

from ..errors import InstanceError, require
from ...coordination.keyed_lock import KeyedLocks


class Scopes:
    def __init__(self, service) -> None:
        self.service = service
        records = service.registry.all("scope")
        self.active = dict((item["scope_id"], item) for item in records if not item.get("waiting"))
        self.queue = dict((item["scope_id"], item) for item in sorted(records, key=lambda row: row.get("submitted", 0)) if item.get("waiting"))
        self.operations = KeyedLocks()

    def for_lease(self, identifier: str) -> list[dict]:
        with self.service.lock:
            return [item for item in list(self.active.values()) + list(self.queue.values()) if item["lease_id"] == identifier]

    def for_instance(self, identifier: str) -> list[dict]:
        with self.service.lock:
            return [item for item in list(self.active.values()) + list(self.queue.values()) if item["instance_id"] == identifier]

    def begin(self, client: dict, payload: dict) -> dict:
        identifier = payload.get("parent_scope_id") or payload.get("scope_id") or uuid.uuid4().hex
        require(isinstance(identifier, str) and 0 < len(identifier) <= 128, "invalid_request", "invalid scope_id")
        with self.operations.hold(identifier):
            return self._begin(client, dict(payload, scope_id=identifier))

    def _begin(self, client: dict, payload: dict) -> dict:
        service = self.service
        with service.lock:
            lease = service.sessions.require_lease(client, payload["lease_id"])
            instance = service.instance(lease["instance_id"])
            require(instance["state"] in ("READY", "IDLE", "BLOCKED"), "instance_unavailable", "instance is not ready", state=instance["state"])
            parent = payload.get("parent_scope_id")
            if parent:
                scope = self.active.get(parent)
                require(scope is not None and scope["lease_id"] == lease["lease_id"]
                        and scope["client_session_id"] == client["client_session_id"] and not scope.get("releasing"),
                        "stale_scope", "parent scope is no longer held")
                require(not payload.get("exclusive") or scope["exclusive"], "exclusive_scope_required", "parent scope is shared")
            else:
                scope = self._admit(client, lease, payload)
                if scope.get("waiting"):
                    return scope
            lease["last_use"] = service.clock()
            scope["manager_epoch"] = service.epoch
            service.registry.put("scope", scope["scope_id"], scope)
        try:
            service.control(instance, "scope_grant", dict(scope))
        except InstanceError as exc:
            if not parent:
                with service.lock:
                    if exc.code in ("instance_unresponsive", "manager_response_unknown"):
                        scope["releasing"] = True
                        service.registry.put("scope", scope["scope_id"], scope)
                    else:
                        self._drop(scope)
            raise
        return dict(scope)

    def _admit(self, client: dict, lease: dict, payload: dict) -> dict:
        service = self.service
        identifier = payload.get("scope_id") or uuid.uuid4().hex
        existing = self.active.get(identifier)
        if existing:
            self._validate(existing, client, lease, payload)
            return existing
        if identifier not in self.queue:
            policy = service.policy
            require(len(self.queue) < policy.max_queue
                    and sum(item["client_session_id"] == client["client_session_id"] for item in self.queue.values()) < policy.per_client_queue
                    and sum(item["instance_id"] == lease["instance_id"] for item in self.queue.values()) < policy.per_project_queue,
                    "admission_queue_full", "project admission queue reached its limit")
            self.queue[identifier] = dict(scope_id=identifier, lease_id=lease["lease_id"], instance_id=lease["instance_id"],
                                          client_session_id=client["client_session_id"], identity=client["identity"],
                                          exclusive=bool(payload.get("exclusive")), operation=payload.get("operation", ""),
                                          submitted=service.clock(), waiting=True)
            service.registry.put("scope", identifier, self.queue[identifier])
        scope = self.queue[identifier]
        self._validate(scope, client, lease, payload)
        active = [item for item in self.active.values() if item["instance_id"] == lease["instance_id"]]
        predecessors = []
        for item in self.queue.values():
            if item["scope_id"] == identifier:
                break
            if item["instance_id"] == lease["instance_id"]:
                predecessors.append(item)
        if (scope["exclusive"] and (active or predecessors)) or any(item["exclusive"] for item in active + predecessors):
            return dict(waiting=True, scope_id=identifier, queued_before=len(predecessors))
        self.queue.pop(identifier)
        scope["waiting"] = False
        self.active[identifier] = scope
        return scope

    def _validate(self, scope: dict, client: dict, lease: dict, payload: dict) -> None:
        require(scope["lease_id"] == lease["lease_id"] and scope["client_session_id"] == client["client_session_id"]
                and scope["exclusive"] == bool(payload.get("exclusive")) and scope["operation"] == payload.get("operation", ""),
                "stale_scope", "scope identity or admission mode changed")
        require(not scope.get("releasing"), "scope_releasing", "this scope is already being released")

    def end(self, client: dict, payload: dict) -> dict:
        with self.operations.hold(payload["scope_id"]):
            return self._end(client, payload)

    def _end(self, client: dict, payload: dict) -> dict:
        service = self.service
        identifier = payload["scope_id"]
        with service.lock:
            scope = self.active.get(identifier) or self.queue.get(identifier)
            if scope is None:
                return dict(released=True)
            require(scope["client_session_id"] == client["client_session_id"], "stale_scope", "scope belongs to another session")
            if identifier in self.queue:
                self._drop(scope)
                return dict(released=True)
            instance = service.instance(scope["instance_id"])
            if instance["state"] == "EXITED":
                self._drop(scope)
                return dict(released=True, outcome="instance_exited")
            scope["releasing"] = True
            service.registry.put("scope", identifier, scope)
        state = service.control(instance, "scope_release", dict(scope_id=identifier))
        with service.lock:
            if state.get("pending", 0) or state.get("inflight", 0):
                scope["releasing"] = True
                service.registry.put("scope", identifier, scope)
                return dict(released=False, pending=True)
            self._drop(scope)
        return dict(released=True, outcome=state.get("outcome"))

    def _drop(self, scope: dict) -> None:
        service = self.service
        if (self.active.get(scope["scope_id"]) or self.queue.get(scope["scope_id"])) is not scope:
            return
        self.active.pop(scope["scope_id"], None)
        self.queue.pop(scope["scope_id"], None)
        service.registry.delete("scope", scope["scope_id"])
        lease = service.sessions.leases.get(scope["lease_id"])
        if lease:
            lease["last_use"] = service.clock()
            if lease.get("releasing") and not self.for_lease(lease["lease_id"]):
                service.sessions.drop(lease, "released")

    def sweep(self) -> None:
        service = self.service
        with service.lock:
            scopes = list(self.active.values()) + list(self.queue.values())
        for scope in scopes:
            try:
                with self.operations.hold(scope["scope_id"]):
                    with service.lock:
                        instance = service.instances.get(scope["instance_id"])
                        gone = instance is None or instance["state"] == "EXITED"
                        if gone:
                            self._drop(scope)
                    if gone:
                        continue
                    if scope.get("releasing") or not service.platform.alive(scope["identity"]):
                        self._end(scope, dict(scope_id=scope["scope_id"]))
            except (InstanceError, OSError):
                continue

    def recover(self) -> None:
        service = self.service
        with service.lock:
            scopes = list(self.active.values())
        for scope in scopes:
            try:
                with self.operations.hold(scope["scope_id"]):
                    with service.lock:
                        instance = service.instances.get(scope["instance_id"])
                        if instance is None or instance["state"] == "EXITED":
                            self._drop(scope)
                            continue
                        if not service.platform.alive(scope["identity"]) or scope.get("releasing"):
                            scope["releasing"] = True
                        scope["manager_epoch"] = service.epoch
                        service.registry.put("scope", scope["scope_id"], scope)
                    if not scope.get("releasing"):
                        service.control(instance, "scope_grant", dict(scope))
            except (InstanceError, OSError):
                continue
        self.sweep()

    def release_client(self, client: dict) -> None:
        with self.service.lock:
            scopes = list(self.active.values()) + list(self.queue.values())
        for scope in scopes:
            if scope["client_session_id"] != client["client_session_id"]:
                continue
            try:
                self.end(client, dict(scope_id=scope["scope_id"]))
            except (InstanceError, OSError):
                with self.service.lock:
                    if scope["scope_id"] in self.active or scope["scope_id"] in self.queue:
                        scope["releasing"] = True
                        self.service.registry.put("scope", scope["scope_id"], scope)
