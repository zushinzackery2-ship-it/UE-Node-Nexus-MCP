"""Small client with explicit registration and no replay of uncertain mutations."""

from __future__ import annotations

import os
from pathlib import Path
import threading
import time
import uuid

from ...errors import BridgeError
from ...coordination.file_lock import LockBusy
from ...transport import named_pipe_transport
from ..errors import InstanceError, require
from ..identity.paths import pipe_address, runtime_root
from ..identity.processes import inspect_process
from ..lifecycle.policy import PROTOCOL_VERSION
from .bootstrap import ensure_manager


class BrokerClient:
    def __init__(self, root: Path | None = None, workspace: str | None = None) -> None:
        self.root = root or runtime_root()
        self.workspace = workspace or str(Path.cwd())
        self.session_id = uuid.uuid4().hex
        self.epoch = 0
        self.registered = False
        self.closed = False
        self.policy = dict()
        self.lock = threading.RLock()

    def _send(self, action: str, payload: dict) -> dict:
        try:
            response = named_pipe_transport.send(pipe_address(self.root),
                         dict(protocol=PROTOCOL_VERSION, action=action, payload=payload, client_session_id=self.session_id), 5)
        except (BridgeError, OSError) as exc:
            self.registered = False
            raise InstanceError("manager_response_unknown", "manager response was interrupted; inspect status before retrying",
                                dict(action=action)) from exc
        valid = isinstance(response, dict) and isinstance(response.get("ok"), bool)
        require(valid, "manager_response_unknown", "manager returned an invalid response envelope", action=action)
        self.epoch = response.get("manager_epoch", self.epoch)
        if not response.get("ok"):
            error = response.get("error")
            require(isinstance(error, dict) and isinstance(error.get("code"), str) and isinstance(error.get("message"), str),
                    "manager_response_unknown", "manager returned an invalid error envelope", action=action)
            require(error.get("details") is None or isinstance(error["details"], dict),
                    "manager_response_unknown", "manager returned invalid error details", action=action)
            raise InstanceError(error["code"], error["message"], error.get("details"))
        require(isinstance(response.get("data"), dict), "manager_response_unknown", "manager returned invalid response data", action=action)
        return response["data"]

    def _admitted_send(self, action: str, payload: dict) -> dict:
        deadline = time.monotonic() + 5
        while True:
            try:
                return self._send(action, payload)
            except InstanceError as exc:
                if exc.code != "manager_busy" or exc.details.get("submitted") is not False or time.monotonic() >= deadline:
                    raise
                time.sleep(0.025)

    def connect(self) -> None:
        with self.lock:
            require(not self.closed, "session_closed", "this MCP session has ended")
            if self.registered:
                return
            try:
                ensure_manager(self.root)
                identity = inspect_process(os.getpid())
            except InstanceError:
                raise
            except LockBusy as exc:
                raise InstanceError("manager_unavailable", "manager startup arbitration timed out", exc.details) from exc
            except (OSError, KeyError, TypeError, ValueError) as exc:
                raise InstanceError("manager_unavailable", "manager bootstrap or process identity could not be verified",
                                    dict(runtime_dir=str(self.root), reason=str(exc))) from exc
            require(identity is not None, "session_closed", "the MCP process identity is unavailable")
            result = self._send("register", dict(client_session_id=self.session_id, workspace=self.workspace,
                                                process_created=identity["process_created"]))
            require(isinstance(result.get("policy"), dict), "manager_response_unknown", "manager registration has no valid policy")
            self.policy = result["policy"]
            self.registered = True

    def call(self, action: str, payload: dict | None = None) -> dict:
        with self.lock:
            self.connect()
            try:
                return self._admitted_send(action, payload or dict())
            except InstanceError as exc:
                if exc.code != "stale_session":
                    raise
                self.registered = False
                self.connect()
                return self._admitted_send(action, payload or dict())

    def close(self) -> None:
        with self.lock:
            self.closed = True
            if self.registered:
                try:
                    self._admitted_send("client_end", dict())
                finally:
                    self.registered = False
