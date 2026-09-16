"""Fixed instance work scopes also cover queue time and uncertain transport results."""

from contextlib import contextmanager
import logging
import time
import uuid

from ..errors import InstanceError, require
from ...transport import make_pipe_name

LOG = logging.getLogger(__name__)


class WorkScope:
    def __init__(self, session, operation: str, exclusive: bool) -> None:
        self.session = session
        self.instance, self.lease = session.acquire()
        self.operation, self.exclusive = operation, exclusive
        self.scope_id = uuid.uuid4().hex
        self.data = dict(waiting=True)
        self.releasing = False
        self.accepted = False
        self.target = make_pipe_name(self.instance["pid"]) if self.instance.get("pid") else None
        self.cache_key = self.instance["instance_id"] + ":" + str(self.instance.get("process_created"))

    def admit(self, wait: bool = True) -> None:
        payload = dict(lease_id=self.lease["lease_id"], scope_id=self.scope_id, exclusive=self.exclusive, operation=self.operation)
        deadline = time.monotonic() + 240
        while True:
            try:
                self.data = self.session.call("scope_begin", payload)
            except InstanceError as exc:
                missing = exc.code == "lease_expired"
                exited = exc.code == "instance_unavailable" and exc.details.get("state") == "EXITED"
                if not (missing or exited) or not self.data.get("waiting"):
                    raise
                result = self._reacquire()
                self.instance = dict(result["instance"])
                require(self.instance["state"] in ("READY", "IDLE", "BLOCKED"), "instance_starting",
                        "the same project is starting; poll bridge_instance_status", instance=self.instance)
                self.target = make_pipe_name(self.instance["pid"])
                self.cache_key = self.instance["instance_id"] + ":" + str(self.instance["process_created"])
                self.lease = result["lease"]
                with self.session.lock:
                    if self.session.binding.get("instance_id") == self.instance["instance_id"]:
                        self.session.lease = dict(self.lease)
                payload["lease_id"] = self.lease["lease_id"]
                self.data = self.session.call("scope_begin", payload)
            self.accepted = True
            if not self.data.get("waiting") or not wait:
                return
            if time.monotonic() >= deadline:
                self.release()
                raise InstanceError("admission_timeout", "work is waiting for an exclusive project operation")
            if self.session.stop.wait(0.025):
                self.release()
                raise InstanceError("session_closed", "MCP session ended while waiting for work admission")

    def _reacquire(self) -> dict:
        payload = dict(self.session.options, project_path=self.instance["project_path"], dry_run=False)
        try:
            return self.session.call("ensure", dict(payload, instance_id=self.instance["instance_id"]))
        except InstanceError as exc:
            if exc.code != "stale_instance" or self.accepted or self.session.explicit_instance:
                raise
            return self.session.ensure(payload)

    def metadata(self) -> dict:
        if self.data.get("manager_epoch") != self.session.broker.epoch:
            self.admit()
        return dict(scope_id=self.scope_id, instance_id=self.instance["instance_id"],
                    client_session_id=self.session.broker.session_id, manager_epoch=self.data["manager_epoch"],
                    context_epoch=self.instance.get("context_epoch", 1))

    def observe(self, response: dict) -> None:
        if "context_epoch" in response:
            self.instance["context_epoch"] = response["context_epoch"]
            self.session.observe(dict(instance_id=self.instance["instance_id"], context_epoch=response["context_epoch"]))

    @contextmanager
    def activate(self):
        if self.data.get("waiting"):
            self.admit()
        require(self.target is not None, "instance_starting", "wait for the fixed target to become ready")
        token = self.session.work.set(self)
        try:
            yield self
        finally:
            self.session.work.reset(token)

    def release(self) -> None:
        self.releasing = True
        if self.session.closed and not self.session.broker.registered:
            return
        try:
            result = self.session.broker.call("scope_end", dict(scope_id=self.scope_id))
            if result.get("released"):
                with self.session.lock:
                    self.session.scopes.pop(self.scope_id, None)
        except InstanceError as exc:
            LOG.warning("scope release deferred scope=%s code=%s", self.scope_id, exc.code)
