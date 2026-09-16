"""Bounded, durable launch idempotency and preview fingerprints."""

import hashlib
import json
import time

from ..errors import require

RETENTION_SECONDS = 86400
MAX_KEYS = 4096


def digest(value: dict) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def parameters(payload: dict) -> dict:
    return dict((key, value) for key, value in payload.items() if key not in ("dry_run", "proposal_id", "idempotency_key"))


def preview(service, project: dict, payload: dict, repository: dict, instance=None, launch=None) -> str:
    identity = dict((key, instance.get(key)) for key in ("instance_id", "process_created", "generation", "state")) if instance else None
    return digest(dict(project_key=project["project_key"], manager_epoch=service.epoch,
                       parameters=parameters(payload), repository=repository, instance=identity, launch=launch))


class Intents:
    def __init__(self, service):
        self.service = service
        self.records = dict((row["key"], row) for row in service.registry.all("ensure_request"))
        self.next_prune_at = 0

    def key(self, client: dict, payload: dict) -> str | None:
        key = payload.get("idempotency_key")
        if key is None:
            return None
        require(isinstance(key, str) and 0 < len(key) <= 128, "invalid_request", "idempotency_key must contain 1–128 characters")
        return digest(dict(client=client["client_session_id"], key=key))

    def lookup(self, client: dict, payload: dict) -> str | None:
        key = self.key(client, payload)
        if not key:
            return None
        self.prune()
        record = self.records.get(key)
        if record:
            require(record["parameters"] == digest(parameters(payload)), "idempotency_conflict", "idempotency key parameters changed")
            item = self.service.instances.get(record["instance_id"])
            require(item is not None and item["state"] != "EXITED", "stale_instance",
                    "this launch request has ended; use a new idempotency key for a new launch",
                    instance_id=record["instance_id"], operation_id=record["operation_id"])
            return record["instance_id"]
        require(len(self.records) < MAX_KEYS, "idempotency_capacity_exceeded", "24-hour idempotency ledger is full", limit=MAX_KEYS)
        return None

    def record(self, client: dict, payload: dict, item: dict) -> None:
        key = self.key(client, payload)
        if not key or key in self.records:
            return
        record = dict(key=key, parameters=digest(parameters(payload)), instance_id=item["instance_id"],
                      operation_id=item.get("operation_id"), created_at=time.time())
        self.service.registry.put("ensure_request", key, record)
        self.records[key] = record

    def prune(self) -> None:
        if self.service.clock() < self.next_prune_at:
            return
        self.next_prune_at = self.service.clock() + 60
        now = time.time()
        expired = [key for key, record in self.records.items() if now - record["created_at"] >= RETENTION_SECONDS]
        self.service.registry.delete_many("ensure_request", expired)
        for key in expired:
            self.records.pop(key)
