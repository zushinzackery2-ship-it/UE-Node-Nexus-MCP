"""Keeps a stable project target while the Broker owns process and usage facts."""

from __future__ import annotations

from contextlib import contextmanager
from contextvars import ContextVar
import logging
import os
from pathlib import Path
import threading

from ..broker.client import BrokerClient
from ..errors import InstanceError, require
from ...transport import make_pipe_name
from . import startup
from .config import resolve_project

LOG = logging.getLogger(__name__)


class EditorSession:
    def __init__(self, broker=None, project: str | None = None, startup_wait: float = startup.DEFAULT_WAIT_SECONDS) -> None:
        self.broker = broker or BrokerClient()
        self.configured_project = project
        # How long a call is held while the editor it needs is starting.
        self.startup_wait = startup.budget(startup_wait)
        self.binding = dict()
        self.lease = dict()
        self.options = dict(mode="reuse_only")
        self.explicit_instance = False
        self.lock = threading.RLock()
        self.stop = threading.Event()
        self.thread = None
        self.on_bind_changed = None
        self.work = ContextVar("nexus-work-" + self.broker.session_id, default=None)
        self.scopes = dict()
        self.closed = False

    def set_on_bind_changed(self, callback) -> None:
        self.on_bind_changed = callback

    def start(self) -> None:
        with self.lock:
            require(not self.closed, "session_closed", "this MCP session has ended")
            if self.thread and self.thread.is_alive():
                return
            self.stop.clear()
            self.thread = threading.Thread(target=self._heartbeat, name="nexus-session-heartbeat", daemon=True)
            self.thread.start()

    def _heartbeat(self) -> None:
        while not self.stop.wait(self.broker.policy.get("heartbeat_seconds", 15)):
            if not self.broker.registered and not self.scopes and not self.lease:
                continue
            try:
                self.broker.call("heartbeat")
                with self.lock:
                    scopes = list(self.scopes.values())
                for scope in scopes:
                    if scope.releasing:
                        scope.release()
            except InstanceError as exc:
                LOG.warning("session heartbeat: %s", exc.code)

    def call(self, action: str, payload: dict | None = None) -> dict:
        self.start()
        return self.broker.call(action, payload)

    def current(self) -> dict:
        with self.lock:
            result = dict(self.binding)
            result.update(target=make_pipe_name(result["pid"]) if result.get("pid") else None,
                          mode="explicit" if self.explicit_instance else self.options["mode"])
            return result

    def list_instances(self, **filters) -> list[dict]:
        rows = self.call("list", filters)["instances"]
        for row in rows:
            row["active"] = row["instance_id"] == self.binding.get("instance_id")
            if row["active"]:
                self.observe(row)
        return rows

    def observe(self, row: dict) -> None:
        with self.lock:
            if row.get("instance_id") == self.binding.get("instance_id"):
                previous = self.binding.get("vfx_available")
                self.binding.update(row)
                if previous != self.binding.get("vfx_available") and self.on_bind_changed:
                    self.on_bind_changed()

    def poll(self, identifier: str) -> dict:
        """A fresh observation of one instance, for a call held on its start."""
        row = self.call("status", dict(instance_id=identifier, fresh=True))
        self.observe(row)
        return row

    def status(self, payload: dict | None = None) -> dict:
        payload = dict(payload or dict())
        wait = payload.pop("wait_seconds", None)
        if not payload.get("instance_id") and not payload.get("project_path") and self.binding.get("instance_id"):
            payload["instance_id"] = self.binding["instance_id"]
        result = self.call("status", payload)
        self.observe(result)
        if wait is None or "instance_id" not in result:
            return result
        row, report = startup.hold(self.poll, result, startup.budget(wait))
        return dict(row, startup=report)

    def ensure(self, payload: dict | None = None) -> dict:
        payload = dict(payload or dict())
        wait = startup.budget(payload.pop("wait_seconds", None), self.startup_wait)
        project = resolve_project(payload.get("project_path"), self.configured_project, self.binding, Path(self.broker.workspace))
        payload.update(project_path=project["project_path"])
        if os.environ.get("UE_NEXUS_TRANSCODE_DIR"):
            payload.setdefault("mirror_root", os.environ["UE_NEXUS_TRANSCODE_DIR"])
        result = self.call("ensure", payload)
        if payload.get("dry_run", True):
            return result
        with self.lock:
            previous = self.binding.get("instance_id")
            self.binding = dict(result["instance"])
            self.lease = dict(result["lease"])
            self.options = dict((key, value) for key, value in payload.items()
                                if key in ("mode", "engine_path", "launch_profile", "rhi", "project_path", "mirror_root"))
            self.options.setdefault("mode", "reuse_only")
            self.explicit_instance = bool(payload.get("instance_id") or payload.get("pid"))
            if previous != self.binding["instance_id"] and self.on_bind_changed:
                self.on_bind_changed()
        row, report = startup.hold(self.poll, result["instance"], wait)
        startup.require(row, report, startup.ENDED)
        return dict(result, instance=row, startup=report)

    def acquire(self) -> tuple[dict, dict]:
        """The bound editor and lease for a call, holding the call while that editor starts."""
        with self.lock:
            bound = self.lease and self.binding.get("state") not in ("EXITED", "STOPPING", "UNRESPONSIVE")
            row, lease = dict(self.binding), dict(self.lease)
            payload = dict(self.options, dry_run=False)
            if self.explicit_instance and self.binding.get("instance_id"):
                payload["instance_id"] = self.binding["instance_id"]
        if bound:
            row, report = startup.hold(self.poll, row, self.startup_wait)
        else:
            result = self.ensure(payload)
            row, lease, report = result["instance"], result["lease"], result["startup"]
        startup.require(row, report)
        return dict(row), dict(lease)

    def select(self, pid: int | None = None, project: str | None = None, project_path: str | None = None,
               instance_id: str | None = None) -> dict:
        matches = self.list_instances(**(dict(project_path=project_path) if project_path else dict()))
        if pid is not None:
            matches = [item for item in matches if item.get("pid") == pid]
        if instance_id:
            matches = [item for item in matches if item["instance_id"] == instance_id]
        if project:
            matches = [item for item in matches if project.lower() in item["project_name"].lower()]
        require(len(matches) == 1, "instance_ambiguous" if matches else "instance_missing",
                "select an exact project or instance", instances=matches)
        chosen = matches[0]
        return self.ensure(dict(project_path=chosen["project_path"], instance_id=chosen["instance_id"], dry_run=False))["instance"]

    def release(self, lease_id: str | None = None) -> dict:
        result = self.call("release", dict(lease_id=lease_id))
        if not lease_id or lease_id == self.lease.get("lease_id"):
            self.lease = dict()
        return result

    def project(self) -> dict:
        with self.lock:
            bound = dict(self.binding)
        return resolve_project(bound.get("project_path"), self.configured_project, bound, Path(self.broker.workspace))

    def repository(self) -> dict:
        from ..repository.binding import resolve
        return resolve(self.project(), self.broker.root, os.environ.get("UE_NEXUS_TRANSCODE_DIR"), persist=True)

    def reserve(self, operation: str, exclusive: bool = False, wait: bool = True):
        from .scopes import WorkScope
        scope = WorkScope(self, operation, exclusive)
        with self.lock:
            require(not self.closed, "session_closed", "this MCP session has ended")
            self.scopes[scope.scope_id] = scope
        try:
            scope.admit(wait=wait)
        except Exception:
            scope.release()
            raise
        return scope

    @contextmanager
    def work_scope(self, operation: str, exclusive: bool = False):
        parent = self.work.get()
        if parent:
            require(not exclusive or parent.exclusive, "exclusive_scope_required", "reserve the whole operation exclusively")
            yield parent
            return
        scope = self.reserve(operation, exclusive)
        with scope.activate():
            try:
                yield scope
            finally:
                scope.release()

    def close(self) -> None:
        with self.lock:
            if self.closed:
                return
            self.closed = True
            self.stop.set()
        if self.thread:
            self.thread.join(timeout=6)
        for scope in list(self.scopes.values()):
            scope.release()
        try:
            self.broker.close()
        except InstanceError as exc:
            LOG.warning("client end deferred to manager process check: %s", exc.code)
        self.lease = dict()
