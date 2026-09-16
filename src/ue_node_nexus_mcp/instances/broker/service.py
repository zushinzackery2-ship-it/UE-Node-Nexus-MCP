"""A single owner of machine-local editor lifecycle facts."""

from __future__ import annotations

from dataclasses import asdict
import json
import logging
import os
from pathlib import Path
import threading
import time
import uuid

from .registry import Registry
from .sessions import Sessions
from .scopes import Scopes
from .intents import Intents
from .resources import Resources
from ..errors import InstanceError, require
from ..lifecycle.policy import Policy, PROTOCOL_VERSION
from ...coordination.file_lock import LockBusy

LOG = logging.getLogger(__name__)


class BrokerService:
    def __init__(self, root: Path, platform, policy: Policy | None = None, clock=time.monotonic) -> None:
        from .editors import Editors
        self.root, self.platform, self.clock = root, platform, clock
        self.policy = policy or Policy.read(root)
        platform.resource_seconds = self.policy.resource_seconds
        self.registry = Registry(root)
        self.epoch = self.registry.epoch
        self.lock = threading.RLock()
        self.work_slots = threading.BoundedSemaphore(2)
        self.identity = platform.inspect(os.getpid())
        self.secret = uuid.uuid4().hex + uuid.uuid4().hex
        self.instances = dict((item["instance_id"], item) for item in self.registry.all("instance"))
        self.started = clock()
        self.projects = dict()
        for item in self.instances.values():
            self.projects.setdefault(item["project_key"], set()).add(item["instance_id"])
            item.update(created_at=self.started, idle_since=self.started, stopping_since=self.started)
            item["pin_until"] = self.started + max(0, min(3600, item.get("pin_expires_at", 0) - time.time()))
        self.recovery_until = self.started + (self.policy.recovery_seconds if self.instances else 0)
        self.ready = False
        self.sessions = Sessions(self)
        self.scopes = Scopes(self)
        self.resources = Resources(self)
        self.editors = Editors(self)
        self.intents = Intents(self)
        self.last_client = self.started
        self.last_sweep = self.started
        self.last_maintenance = self.started - 60

    def policy_dict(self) -> dict:
        return asdict(self.policy)

    def event(self, action: str, **fields) -> None:
        LOG.info(json.dumps(dict(event=action, time=time.time(), manager_epoch=self.epoch, **fields),
                            ensure_ascii=False, separators=(",", ":")))

    def save(self, instance: dict) -> None:
        if instance["state"] == "EXITED":
            instance.setdefault("exited_wall_time", time.time())
        self.projects.setdefault(instance["project_key"], set()).add(instance["instance_id"])
        self.registry.put("instance", instance["instance_id"], instance)

    def instance(self, identifier: str) -> dict:
        instance = self.instances.get(identifier)
        require(instance is not None, "stale_instance", "instance is no longer registered")
        return instance

    def control(self, instance: dict, action: str, payload: dict | None = None) -> dict:
        credentials = dict(manager_epoch=self.epoch, manager_identity=dict(sorted(self.identity.items())), manager_secret=self.secret,
                           instance_id=instance["instance_id"])
        return self.platform.control(instance, action, dict(payload or dict(), **credentials))

    def dispatch(self, request: dict, peer: int) -> dict:
        try:
            require(request.get("protocol") == PROTOCOL_VERSION, "manager_version_mismatch", "manager protocol mismatch",
                    required=PROTOCOL_VERSION, actual=request.get("protocol"))
            action = request.get("action")
            payload = request.get("payload") or dict()
            require(isinstance(payload, dict), "invalid_request", "payload must be an object")
            if action == "ping":
                result = dict(protocol=PROTOCOL_VERSION, manager_epoch=self.epoch, ready=self.ready, identity=self.identity)
            elif action == "register":
                result = self.sessions.register(payload, peer)
            else:
                with self.lock:
                    client = self.sessions.client(request.get("client_session_id", ""), peer)
                actions = dict(heartbeat=self.sessions.heartbeat, client_end=self.sessions.end,
                               release=self.sessions.release, scope_begin=self.scopes.begin, scope_end=self.scopes.end,
                               ensure=self.editors.ensure, list=self.editors.list, status=self.editors.status,
                               close=self.editors.close, reap=self.editors.reap, adopt=self.editors.adopt, pin=self.editors.pin)
                require(action in actions, "unknown_manager_action", "unknown manager action")
                result = self.invoke(action, actions[action], client, payload)
            return dict(ok=True, data=result, manager_epoch=self.epoch)
        except InstanceError as exc:
            return dict(exc.envelope(), manager_epoch=self.epoch)
        except LockBusy as exc:
            return InstanceError("coordination_busy", "another client holds the project coordination lock", exc.details).envelope()
        except (KeyError, TypeError, ValueError) as exc:
            return InstanceError("invalid_request", str(exc)).envelope()
        except OSError as exc:
            return InstanceError("instance_unverified", "process or filesystem identity could not be verified",
                                 dict(winerror=getattr(exc, "winerror", None))).envelope()
        except Exception:
            LOG.exception("manager request failed action=%s", request.get("action"))
            return InstanceError("manager_internal_error", "manager request failed; inspect lifecycle log").envelope()

    def invoke(self, action: str, handler, client: dict, payload: dict) -> dict:
        if action in ("heartbeat", "release", "list", "status"):
            return handler(client, payload)
        require(self.work_slots.acquire(blocking=False), "manager_busy",
                "manager work slots are occupied; control queries remain available", submitted=False, retry_after_ms=25)
        try:
            return handler(client, payload)
        finally:
            self.work_slots.release()

    def reconcile(self) -> None:
        self.editors.refresh()
        self.scopes.recover()
        with self.lock:
            self.ready = True
            self.event("recovery_observed", instances=len(self.instances))

    def tick(self) -> None:
        now = self.clock()
        if now - self.last_sweep < self.policy.sweep_seconds:
            return
        if now - self.last_sweep > max(60, self.policy.sweep_seconds * 4):
            self.recovery_until = now + self.policy.recovery_seconds
            with self.lock:
                for item in self.instances.values():
                    item["idle_since"] = now
                for lease in self.sessions.leases.values():
                    lease["last_use"] = now
            self.event("resume_reconcile")
        self.last_sweep = now
        self.editors.refresh()
        self.scopes.sweep()
        with self.lock:
            self.sessions.sweep()
            if self.sessions.clients:
                self.last_client = now
        if self.ready and now >= self.recovery_until:
            self.editors.reap(None, dict(dry_run=False, automatic=True))
        if now - self.last_maintenance >= 60:
            from .history import prune
            self.last_maintenance = now
            self.intents.prune()
            prune(self)

    def idle(self) -> bool:
        with self.lock:
            owned = any(item["ownership"] == "managed" and item["state"] != "EXITED" for item in self.instances.values())
            return not owned and not self.sessions.clients and self.clock() - self.last_client >= self.policy.broker_idle_seconds
