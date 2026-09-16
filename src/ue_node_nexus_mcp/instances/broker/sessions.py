"""Client liveness and expiring use leases are intentionally separate."""

from __future__ import annotations

import uuid

from ..errors import require


class Sessions:
    def __init__(self, service) -> None:
        self.service = service
        self.clients = dict()
        self.leases = dict()
        for item in service.registry.all("lease"):
            item["last_use"] = service.clock()
            item["recovering"] = True
            self.leases[item["lease_id"]] = item

    def register(self, payload: dict, peer: int) -> dict:
        service = self.service
        actual = service.platform.inspect(peer)
        require(actual is not None, "stale_session", "client process has exited")
        require(payload.get("process_created") == actual["process_created"], "stale_session", "client creation identity changed")
        identifier = payload["client_session_id"]
        with service.lock:
            previous = self.clients.get(identifier)
            require(previous is None or previous["identity"] == actual, "stale_session", "session belongs to another process")
            record = dict(client_session_id=identifier, identity=actual, workspace=payload.get("workspace", ""),
                          last_seen=service.clock())
            self.clients[identifier] = record
            for lease in self.leases.values():
                if lease["client_session_id"] == identifier and lease["identity"] == actual:
                    lease["recovering"] = False
            service.event("client_registered", client_session_id=identifier, pid=peer)
        return dict(client_session_id=identifier, manager_epoch=service.epoch, policy=service.policy_dict())

    def client(self, identifier: str, peer: int) -> dict:
        record = self.clients.get(identifier)
        require(record is not None and record["identity"]["pid"] == peer,
                "stale_session", "register this MCP process before using the manager")
        require(self.service.platform.alive(record["identity"]), "stale_session", "client process creation identity changed")
        return record

    def health(self, identifier: str) -> str:
        client = self.clients.get(identifier)
        if not client:
            return "disconnected"
        return "suspect" if self.service.clock() - client["last_seen"] >= self.service.policy.suspect_seconds else "connected"

    def heartbeat(self, client: dict, _payload: dict) -> dict:
        with self.service.lock:
            client["last_seen"] = self.service.clock()
        return dict(alive=True, manager_epoch=self.service.epoch)

    def acquire(self, client: dict, instance: dict, mode: str) -> dict:
        service = self.service
        existing = next((item for item in self.leases.values()
                         if item["client_session_id"] == client["client_session_id"]
                         and item["project_key"] == instance["project_key"]), None)
        if existing:
            require(existing["instance_id"] == instance["instance_id"] or not service.scopes.for_lease(existing["lease_id"]),
                    "instance_in_use", "previous instance still has unresolved work")
        record = dict(lease_id=existing["lease_id"] if existing else uuid.uuid4().hex,
                      client_session_id=client["client_session_id"], identity=client["identity"], workspace=client["workspace"],
                      instance_id=instance["instance_id"], project_key=instance["project_key"], last_use=service.clock(),
                      launch_mode=mode, recovering=False, releasing=False)
        self.leases[record["lease_id"]] = record
        service.registry.put("lease", record["lease_id"], record)
        if instance["state"] in ("IDLE", "BLOCKED"):
            instance.update(state="READY", idle_since=None)
            service.save(instance)
        service.event("lease_acquired", lease_id=record["lease_id"], instance_id=instance["instance_id"],
                      client_session_id=client["client_session_id"])
        return dict(record)

    def require_lease(self, client: dict, identifier: str) -> dict:
        lease = self.leases.get(identifier)
        require(lease is not None and lease["client_session_id"] == client["client_session_id"],
                "lease_expired", "acquire this project again")
        require(not lease.get("releasing"), "lease_releasing", "this lease is being released")
        return lease

    def release(self, client: dict, payload: dict) -> dict:
        service = self.service
        with service.lock:
            identifier = payload.get("lease_id")
            items = [item for item in self.leases.values()
                     if item["client_session_id"] == client["client_session_id"]
                     and (not identifier or item["lease_id"] == identifier)]
            pending = []
            for lease in items:
                lease["releasing"] = True
                if service.scopes.for_lease(lease["lease_id"]):
                    service.registry.put("lease", lease["lease_id"], lease)
                    pending.append(lease["lease_id"])
                else:
                    self.drop(lease, "released")
            return dict(released=not pending, pending=pending)

    def drop(self, lease: dict, reason: str) -> None:
        service = self.service
        self.leases.pop(lease["lease_id"], None)
        service.registry.delete("lease", lease["lease_id"])
        instance = service.instances.get(lease["instance_id"])
        if instance and not self.for_instance(instance["instance_id"]):
            instance["idle_since"] = service.clock()
            if instance["state"] == "READY":
                instance["state"] = "IDLE"
            service.save(instance)
        service.event("lease_" + reason, lease_id=lease["lease_id"], instance_id=lease["instance_id"])

    def for_instance(self, identifier: str) -> list[dict]:
        return [item for item in self.leases.values() if item["instance_id"] == identifier]

    def end(self, client: dict, payload: dict) -> dict:
        self.service.scopes.release_client(client)
        result = self.release(client, dict())
        with self.service.lock:
            self.clients.pop(client["client_session_id"], None)
        return result

    def sweep(self) -> None:
        service = self.service
        for lease in list(self.leases.values()):
            if service.scopes.for_lease(lease["lease_id"]):
                continue
            try:
                alive = service.platform.alive(lease["identity"])
            except OSError:
                continue
            expired = service.clock() - lease["last_use"] >= service.policy.idle_seconds
            recovered = service.clock() >= service.recovery_until
            if (not alive or expired or lease.get("releasing")) and (recovered or not lease.get("recovering")):
                self.drop(lease, "expired")
        for identifier, client in list(self.clients.items()):
            try:
                if not service.platform.alive(client["identity"]):
                    self.clients.pop(identifier)
            except OSError:
                continue
